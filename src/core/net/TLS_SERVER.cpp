// HTTPS/WSS сервер устройства (IDF httpd + собственный TLS-транспорт).
//
// Зачем: PWA открывается по HTTPS, а браузер запрещает ws:// со страницы HTTPS (mixed content).
// Значит блок должен отдавать wss://, а для этого нужен сертификат, которому доверяет браузер.
//
// Сертификат выпускаем МЫ (Let's Encrypt DNS-01 на core.dedbox.pro) и кладём на устройство
// файлами /cert.pem и /key.pem в файловой системе. В офлайне DNS/интернет не нужны:
// имя резолвит AP-DNS
// устройства (initApDns в servers/WIFI_SERVER.h), доверие проверяет браузер телефона.
//
// Плейн-HTTP (80) и WS (81) остаются fallback-ом для уже выпущенных фронтов.
//
// Реализация в отдельном .cpp: заголовки IDF (esp_http_server.h) и ESPAsyncWebServer
// объявляют несовместимые перечисления HTTP_*, в одной единице трансляции они не уживаются.

#include <Arduino.h>
#include <Preferences.h>
#include "core/storage/DEVICE_FS.h"
#include <ArduinoJson.h>
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include <time.h>
#include <lwip/sockets.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/pk.h>
#include <mbedtls/platform.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include "config/PROPS.h"
#include "core/ota/HYBRID_OTA.h"
#include "core/net/TLS_SERVER.h"
#include "core/net/AI_GRANT.h"
#include "core/net/SERVICE_CLIENT.h"

// Общий протокол с WS на 81 — определения в WEB_SOCKET_FUNCS.h (в этой единице трансляции их нет).
String getDataJson();
bool handleClientCommand(const String &dataStr, String &out, bool authorized);

// Реализация — core/ble/BLE_SERVER.h.
bool releaseBleMemory();
void restoreBleMemory();
// Возврат куче резерва DRAM контроллера Bluetooth (56 КБ). После вызова Bluetooth
// поднимется только после перезагрузки, поэтому вызываем на заглушенном двигателе.
bool purgeBleMemory();
bool bleMemoryReleased();

// Освобождение разобранного сертификата и конфигурации TLS (см. tlsTransportPrepare).
static void tlsTransportRelease();
// Материал TLS в NVS (см. tlsMaterialToNvs).
bool tlsMaterialToNvs(const char *cert, const char *key);
bool tlsMaterialPresent();

extern Preferences preferences;
extern HybridOTAManager otaManager;

// Домен сертификата; AP-DNS резолвит его на адрес устройства.
#define DED_CORE_TLS_CERT_FILE    "/cert.pem"
#define DED_CORE_TLS_KEY_FILE     "/key.pem"
// Материал TLS дублируем в NVS: раздел LittleFS перезаписывается при обновлении
// интерфейса (uploadfs, flash-device.sh --fs-only), и вместе с файлами пропадал
// сертификат — блок оставался без HTTPS, пока сервис не выдаст его заново.
#define DED_CORE_TLS_CERT_NVS     "tls_cert"
#define DED_CORE_TLS_KEY_NVS      "tls_key"
#define DED_CORE_TLS_MAX_CLIENTS  4
#define DED_CORE_TLS_BROADCAST_MS 200
#define DED_CORE_TLS_MAX_UPLOAD   8192

// Стек http-задачи: рукопожатие mbedTLS с ECDSA P-256 (~4–5 КБ) плюс разбор запроса
// и StaticJsonBuffer телеметрии. 7 КБ вместо 9 КБ: каждый килобайт стека — это
// килобайт кучи, которой рядом с Bluetooth не хватает на TLS-сессию.
#define DED_CORE_TLS_STACK 7168

// Одновременных TLS-сессий: документ, статика и wss. Каждая держит контекст mbedTLS
// и его буферы, поэтому больше трёх рядом с Bluetooth не проходит.
#define DED_CORE_TLS_OPEN_SOCKETS 4

// Сколько ждём завершения рукопожатия. Сокет уже с таймаутами httpd, но WANT_READ
// на них не отличить от «клиент ещё думает», поэтому считаем сами.
#define DED_CORE_TLS_HANDSHAKE_MS 10000UL

// Минимальный непрерывный запас кучи, при котором HTTPS имеет смысл поднимать:
// стек сервера (14 КБ) + два буфера mbedTLS (по 16 КБ) + рукопожатие.
#define DED_CORE_TLS_MIN_HEAP 24000

static httpd_handle_t tlsServer = nullptr;
static int tlsClients[DED_CORE_TLS_MAX_CLIENTS] = { -1, -1, -1, -1 };
// Клиент подтвердил ключ устройства кадром auth. TLS-канал шифрует трафик,
// поэтому ключ можно принимать и из домашней сети, а не только с точки доступа.
static bool tlsAuthorized[DED_CORE_TLS_MAX_CLIENTS] = { false, false, false, false };
static String tlsCertPem;
static String tlsKeyPem;
static bool tlsRestartPending = false;
static bool tlsBootRefreshRequested = false;
static unsigned long tlsLastBroadcast = 0;

// Срок действия сертификата и наличие файла меняются раз в сутки, а
// tlsHandleCertificateMaintenance() вызывается из цикла задачи сервера каждые 2 мс.
// Без кеша это чтение NVS (под общим мьютексом) и обход ФС сотни раз в секунду.
static bool tlsCertCacheValid = false;
static unsigned long tlsCertCacheAt = 0;
static bool tlsCertCachePresent = false;
static int tlsCertCacheDays = -1;

void tlsInvalidateCertificateCache() {
  tlsCertCacheValid = false;
}

bool tlsIsRunning() {
  return tlsServer != nullptr;
}

static bool tlsHasWsClients() {
  for (int i = 0; i < DED_CORE_TLS_MAX_CLIENTS; i++) {
    if (tlsClients[i] >= 0) {
      return true;
    }
  }

  return false;
}

// Сравнение ключа без раннего выхода: длина известна, а посимвольный возврат на
// первом несовпадении даёт лишний канал о значении ключа.
static bool tlsKeyEquals(const String &a, const String &b) {
  if (a.length() == 0 || a.length() != b.length()) {
    return false;
  }

  unsigned char diff = 0;

  for (unsigned int i = 0; i < a.length(); i++) {
    diff |= static_cast<unsigned char>(a[i] ^ b[i]);
  }

  return diff == 0;
}

const char *tlsMimeType(const String &path) {
  if (path.endsWith(".html")) return "text/html";
  if (path.endsWith(".js")) return "application/javascript";
  if (path.endsWith(".css")) return "text/css";
  if (path.endsWith(".json")) return "application/json";
  if (path.endsWith(".ico")) return "image/x-icon";
  if (path.endsWith(".txt")) return "text/plain";
  if (path.endsWith(".svg")) return "image/svg+xml";
  if (path.endsWith(".png")) return "image/png";
  return "application/octet-stream";
}

void tlsTrackClient(int fd) {
  for (int i = 0; i < DED_CORE_TLS_MAX_CLIENTS; i++) {
    if (tlsClients[i] == fd) {
      return;
    }
  }

  for (int i = 0; i < DED_CORE_TLS_MAX_CLIENTS; i++) {
    if (tlsClients[i] < 0) {
      tlsClients[i] = fd;
      return;
    }
  }
}

void tlsSetAuthorized(int fd, bool authorized) {
  for (int i = 0; i < DED_CORE_TLS_MAX_CLIENTS; i++) {
    if (tlsClients[i] == fd) {
      tlsAuthorized[i] = authorized;
      return;
    }
  }
}

bool tlsIsAuthorized(int fd) {
  for (int i = 0; i < DED_CORE_TLS_MAX_CLIENTS; i++) {
    if (tlsClients[i] == fd) {
      return tlsAuthorized[i];
    }
  }

  return false;
}

void tlsForgetClient(int fd) {
  for (int i = 0; i < DED_CORE_TLS_MAX_CLIENTS; i++) {
    if (tlsClients[i] == fd) {
      tlsClients[i] = -1;
      tlsAuthorized[i] = false;
    }
  }
}

esp_err_t tlsWsHandler(httpd_req_t *req) {
  int fd = httpd_req_to_sockfd(req);

  if (req->method == HTTP_GET) {
    // Рукопожатие уже сделал httpd — запоминаем клиента для рассылки телеметрии.
    tlsTrackClient(fd);
    tlsSetAuthorized(fd, false);
    Serial.printf("WSS client connected (fd=%d)\n", fd);
    return ESP_OK;
  }

  httpd_ws_frame_t frame = {};
  frame.type = HTTPD_WS_TYPE_TEXT;

  esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
  if (ret != ESP_OK) {
    return ret;
  }

  if (frame.len == 0) {
    return ESP_OK;
  }

  if (frame.len > 1024) {
    tlsForgetClient(fd);
    return ESP_ERR_INVALID_SIZE;
  }

  uint8_t *buffer = (uint8_t *)malloc(frame.len + 1);
  if (buffer == nullptr) {
    return ESP_ERR_NO_MEM;
  }

  frame.payload = buffer;
  ret = httpd_ws_recv_frame(req, &frame, frame.len);
  if (ret != ESP_OK) {
    free(buffer);
    return ret;
  }

  buffer[frame.len] = 0;
  String payloadStr = String((char *)buffer);
  free(buffer);

  // Рукопожатие владельца: ключ устройства приходит один раз на соединение.
  StaticJsonBuffer<256> authBuffer;
  JsonObject &auth = authBuffer.parseObject(payloadStr);

  if (auth.success() && auth["event"] == "auth") {
    const char *key = auth["data"]["key"];
    const String deviceKey = otaManager.getDeviceKey();
    const bool ok = key != nullptr && deviceKey.length() > 0 &&
                    tlsKeyEquals(String(key), deviceKey);

    tlsSetAuthorized(fd, ok);

    String reply = ok
                       ? String("{\"event\":\"auth\",\"data\":{\"ok\":1}}")
                       : String("{\"event\":\"auth\",\"data\":{\"error\":\"unauthorized\"}}");

    httpd_ws_frame_t out = {};
    out.type = HTTPD_WS_TYPE_TEXT;
    out.payload = (uint8_t *)reply.c_str();
    out.len = reply.length();
    httpd_ws_send_frame(req, &out);

    Serial.printf("WSS auth (fd=%d): %s\n", fd, ok ? "ok" : "denied");
    return ESP_OK;
  }

  String response;
  if (handleClientCommand(payloadStr, response, tlsIsAuthorized(fd))) {
    httpd_ws_frame_t out = {};
    out.type = HTTPD_WS_TYPE_TEXT;
    out.payload = (uint8_t *)response.c_str();
    out.len = response.length();
    httpd_ws_send_frame(req, &out);
  }

  return ESP_OK;
}

esp_err_t tlsStaticHandler(httpd_req_t *req) {
  String path = String(req->uri);

  if (path == "/" || path.length() == 0) {
    path = "/index.html";
  }

  if (path.indexOf("..") >= 0) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad path");
  }

  // Материал TLS и служебные файлы лежат в том же разделе, что и фронт, поэтому
  // статический обработчик обязан их не отдавать: /key.pem — приватный ключ устройства,
  // и с ним можно выдать себя за блок перед приложением.
  String lower = path;
  lower.toLowerCase();

  if (lower.endsWith(".pem") || lower.endsWith(".key") || lower.endsWith(".p12")
      || lower.endsWith(".pfx") || lower.endsWith(".der") || lower.endsWith(".token")) {
    return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
  }

  // Фронт лежит двумя копиями: обычной и .gz (см. registerStaticFiles на стороне HTTP).
  // Сжатая отдаётся только тем, кто сам её попросил.
  bool gzip = false;
  char encoding[32] = {0};

  if (httpd_req_get_hdr_value_str(req, "Accept-Encoding", encoding, sizeof(encoding)) == ESP_OK &&
      strstr(encoding, "gzip") != nullptr && DEVICE_FS.exists(path + ".gz")) {
    path += ".gz";
    gzip = true;
  }

  File file = DEVICE_FS.open(path, "r");
  if (!file || file.isDirectory()) {
    return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
  }

  httpd_resp_set_hdr(req, "Connection", "close");
  // Тип считаем по исходному имени: у .gz расширение не то, что нужно браузеру.
  httpd_resp_set_type(req, tlsMimeType(gzip ? path.substring(0, path.length() - 3) : path));
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");

  if (gzip) {
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  }

  // Фронт отдаём потоком, не поднимая файл целиком в RAM. Кусок равен пределу записи
  // mbedTLS (OUT_CONTENT_LEN): мельче — это отдельный TLS-запись и лишний круг задержки
  // на каждый килобайт, из-за чего страница на 300 КБ грузилась десятки секунд.
  char chunk[4096];
  size_t readBytes = 0;
  while ((readBytes = file.read((uint8_t *)chunk, sizeof(chunk))) > 0) {
    if (httpd_resp_send_chunk(req, chunk, readBytes) != ESP_OK) {
      file.close();
      return ESP_FAIL;
    }
  }

  file.close();
  return httpd_resp_send_chunk(req, nullptr, 0);
}

bool tlsRequestAuthorized(httpd_req_t *req) {
  String deviceKey = otaManager.getDeviceKey();

  // Ключа нет (штучное устройство без провижининга) — сертификат кладём только по кабелю.
  if (deviceKey.length() == 0) {
    return false;
  }

  size_t headerLen = httpd_req_get_hdr_value_len(req, "X-Device-Key");
  if (headerLen == 0 || headerLen != deviceKey.length() || headerLen >= 128) {
    return false;
  }

  char header[128];
  if (httpd_req_get_hdr_value_str(req, "X-Device-Key", header, sizeof(header)) != ESP_OK) {
    return false;
  }

  // Сравнение в постоянном времени: длина известна и так, а ранний выход на первом
  // несовпадении даёт лишний канал для подбора ключа.
  unsigned char diff = 0;
  for (size_t i = 0; i < headerLen; i++) {
    diff |= static_cast<unsigned char>(header[i] ^ deviceKey[i]);
  }

  return diff == 0;
}

// Приём сертификата:
//   {"cert":"-----BEGIN CERTIFICATE-----...","key":"-----BEGIN PRIVATE KEY-----...","notAfter":1790000000}
// notAfter (время окончания в секундах UNIX) необязателен, но с ним устройство само
// понимает, что сертификат пора менять, и успевает это сделать до истечения.
// Заголовок X-Device-Key обязателен: иначе сертификат мог бы подменить любой из той же Wi-Fi сети.
bool applyTlsMaterial(const String &body, String &error) {
  // Serial-провижининг может сработать раньше, чем задача сервера смонтирует ФС.
  if (!DEVICE_FS.begin(true)) {
    error = "filesystem is not available";
    return false;
  }

  DynamicJsonBuffer jsonBuffer;
  JsonObject &root = jsonBuffer.parseObject(body);

  if (!root.success()) {
    error = "invalid json";
    return false;
  }

  // Плоский {cert,key} — для POST /api/tls; {"event":"tls","data":{cert,key}} — для Serial/WebSerial.
  const char *cert = root["cert"];
  const char *key = root["key"];

  if (cert == nullptr || key == nullptr) {
    cert = root["data"]["cert"];
    key = root["data"]["key"];
  }

  if (cert == nullptr || key == nullptr) {
    error = "cert or key missing";
    return false;
  }

  if (strstr(cert, "-----BEGIN CERTIFICATE-----") == nullptr) {
    error = "cert is not PEM";
    return false;
  }

  if (strstr(key, "-----BEGIN") == nullptr || strstr(key, "PRIVATE KEY-----") == nullptr) {
    error = "key is not PEM";
    return false;
  }

  // Основное хранилище — NVS. Файлы пишем тоже: так материал читают уже прошитые
  // блоки, и файл остаётся запасным путём, если в NVS не нашлось места.
  if (!tlsMaterialToNvs(cert, key)) {
    error = "cannot store cert in NVS";
    return false;
  }

  File certFile = DEVICE_FS.open(DED_CORE_TLS_CERT_FILE, "w");
  if (certFile) {
    certFile.print(cert);
    certFile.close();
  }

  File keyFile = DEVICE_FS.open(DED_CORE_TLS_KEY_FILE, "w");
  if (keyFile) {
    keyFile.print(key);
    keyFile.close();
  }

  // Срок действия: берём из запроса, если он есть. Иначе устройство о сроке не знает
  // и будет полагаться на проверку со стороны приложения.
  uint32_t notAfter = 0;

  if (root.containsKey("notAfter")) {
    notAfter = root["notAfter"].as<uint32_t>();
  } else if (root.containsKey("data")) {
    notAfter = root["data"]["notAfter"].as<uint32_t>();
  }

  // Если срок не передали, прежнее значение не затираем: по нему блок понимает,
  // что сертификат пора менять. Иначе автопродление выключается до следующей
  // установки материала с указанным сроком.
  if (notAfter != 0) {
    preferences.putUInt(DED_CORE_TLS_EXPIRY_KEY, notAfter);
  }

  tlsRestartPending = true;
  tlsInvalidateCertificateCache();
  return true;
}

// Сколько дней осталось сертификату. -1 — срок неизвестен (не передан при установке),
// -2 — часы ещё не выставлены, поэтому сравнить не с чем.
int tlsCertificateDaysLeft() {
  uint32_t notAfter = preferences.getUInt(DED_CORE_TLS_EXPIRY_KEY, 0);

  if (notAfter == 0) {
    return -1;
  }

  time_t now = time(nullptr);

  if (now < 1700000000) {
    return -2;
  }

  return (int)(((long)notAfter - (long)now) / 86400L);
}

uint32_t tlsCertificateExpiry() {
  return preferences.getUInt(DED_CORE_TLS_EXPIRY_KEY, 0);
}

esp_err_t tlsCertUploadHandler(httpd_req_t *req) {
  if (!tlsRequestAuthorized(req)) {
    return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "X-Device-Key required");
  }

  if (req->content_len <= 0 || req->content_len > DED_CORE_TLS_MAX_UPLOAD) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body size");
  }

  String body;
  body.reserve(req->content_len + 1);

  int remaining = req->content_len;
  char buffer[512];
  while (remaining > 0) {
    int chunk = remaining > (int)sizeof(buffer) ? sizeof(buffer) : remaining;
    int received = httpd_req_recv(req, buffer, chunk);
    if (received <= 0) {
      return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body read failed");
    }

    body.concat(buffer, received);
    remaining -= received;
  }

  String error;
  if (!applyTlsMaterial(body, error)) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, error.c_str(), HTTPD_RESP_USE_STRLEN);
  }

  return httpd_resp_send(req, "cert installed, restarting TLS", HTTPD_RESP_USE_STRLEN);
}

esp_err_t tlsStatusHandler(httpd_req_t *req) {
  StaticJsonBuffer<640> jsonBuffer;
  JsonObject &json = jsonBuffer.createObject();

  json["host"] = DED_CORE_TLS_HOST;
  json["firmware"] = FIRMWARE_VERSION;
  json["tls"] = tlsIsRunning();
  json["cert"] = tlsMaterialPresent();
  json["certExpiry"] = tlsCertificateExpiry();
  json["certDaysLeft"] = tlsCertificateDaysLeft();
  json["wifiConnected"] = (WiFi.status() == WL_CONNECTED);
  json["active_cart"] = active_cart;
  json["rpm"] = RPM;
  json["wifi_update_status"] = WIFI_UPDATE_STATUS;
  // MAC и признак привязки нужны вкладке «Люси»: по MAC сервис чата выдаёт nonce.
  // Код сопряжения по HTTPS не отдаём — это секрет, по нему блок привязывают.
  json["mac"] = aiGrantMac();
  json["paired"] = otaManager.getDeviceKey().length() > 0;

  String body = "";
  json.printTo(body);

  httpd_resp_set_hdr(req, "Connection", "close");
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, body.c_str(), HTTPD_RESP_USE_STRLEN);
}

// Подпись nonce для чата «Люси» на странице, открытой по HTTPS. Тело: {"nonce":"<hex>"}.
esp_err_t tlsAiGrantHandler(httpd_req_t *req) {
  if (req->content_len <= 0 || req->content_len > 512) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body size");
  }

  String body;
  body.reserve(req->content_len + 1);

  int remaining = req->content_len;
  char buffer[256];
  while (remaining > 0) {
    int chunk = remaining > (int)sizeof(buffer) ? sizeof(buffer) : remaining;
    int received = httpd_req_recv(req, buffer, chunk);
    if (received <= 0) {
      return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body read failed");
    }

    body.concat(buffer, received);
    remaining -= received;
  }

  StaticJsonBuffer<256> jsonBuffer;
  JsonObject &json = jsonBuffer.parseObject(body);

  const char *nonce = json.success() ? json["nonce"].as<const char *>() : nullptr;
  String signature;
  String error = "nonce";

  if (nonce == nullptr || !aiGrantSignature(String(nonce), signature, error)) {
    httpd_resp_set_status(req, error == "not_paired" ? "409 Conflict" : "400 Bad Request");
    return httpd_resp_send(req, error.c_str(), HTTPD_RESP_USE_STRLEN);
  }

  String reply = "{\"result\":\"ok\",\"mac\":\"" + aiGrantMac() +
                 "\",\"signature\":\"" + signature + "\"}";

  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, reply.c_str(), HTTPD_RESP_USE_STRLEN);
}

// Часы нужны, чтобы понимать, истёк ли сертификат. Берём время из интернета, когда
// устройство подключено к домашней сети; без часов срок считается неизвестным.
void tlsEnsureTimeConfigured() {
  static bool configured = false;

  if (configured || WiFi.status() != WL_CONNECTED) {
    return;
  }

  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
  configured = true;
  Serial.println("TLS: NTP time requested");
}

// Пробуем забрать свежий сертификат у сервиса. Работает, когда у устройства есть
// домашняя сеть с интернетом; иначе остаются пути из приложения и по проводу.
bool tlsFetchFromService(String &error) {
  String deviceKey = otaManager.getDeviceKey();

  if (deviceKey.length() == 0) {
    error = "device key not set";
    return false;
  }

  String url = String(SERVICE_BASE_URL) + "/device/tls";

  WiFiClientSecure client;

  // Вызывается из задачи веб-сервера: не ждём часы подолгу, чтобы не держать интерфейс.
  if (!serviceClientSetup(client, 3000)) {
    error = "no clock for certificate check";
    return false;
  }

  HTTPClient http;

  if (!http.begin(client, url)) {
    error = "cannot start request";
    return false;
  }

  http.addHeader("X-Device-Key", deviceKey);
  http.setTimeout(15000);

  int code = http.GET();

  if (code != HTTP_CODE_OK) {
    // Без этого сообщения ошибку не отличить от нехватки памяти и от неверного
    // сертификата: HTTPClient возвращает общий код, а причину знает только TLS-слой.
    char sslError[128] = {0};
    client.lastError(sslError, sizeof(sslError));
    error = "service returned " + String(code) + " (" + http.errorToString(code) + ") tls=" +
            String(sslError) + " heap=" + String(ESP.getFreeHeap()) +
            "/" + String(ESP.getMaxAllocHeap());
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();

  if (!applyTlsMaterial(body, error)) {
    return false;
  }

  Serial.println("TLS: certificate refreshed from service");
  return true;
}

// Стартовая проверка сертификата. Здесь ещё не подняты HTTP/WebSocket/точка доступа,
// поэтому в куче есть непрерывный участок под буферы mbedTLS (33 КБ) — в рабочем
// режиме его уже нет, и рукопожатие с сервисом падает.
void tlsStartupCertificateCheck() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (!DEVICE_FS.begin(true)) {
    return;
  }

  bool forced = preferences.getBool("tls_force", false);
  if (forced) {
    preferences.putBool("tls_force", false);
  }

  bool present = tlsMaterialPresent();
  int daysLeft = tlsCertificateDaysLeft();

  if (present && !forced && daysLeft >= DED_CORE_TLS_RENEW_DAYS) {
    return;
  }

  String error;
  bool fetched;

  disableCore0WDT();
  fetched = tlsFetchFromService(error);
  enableCore0WDT();

  if (fetched) {
    Serial.println("TLS: certificate received at startup");
  } else {
    Serial.print("TLS: startup certificate fetch failed: ");
    Serial.println(error);
  }
}

void tlsRequestRefreshOnBoot() {
  preferences.putBool("tls_force", true);
  tlsBootRefreshRequested = true;
}

// Обслуживание сертификата: вызывается из задачи сервера.
// Меняем сертификат заранее (за две недели до истечения или сразу, если его нет),
// чтобы HTTPS не успел отвалиться: без него приложение по WiFi не подключится.
void tlsHandleCertificateMaintenance() {
  static unsigned long lastAttempt = 0;

  tlsEnsureTimeConfigured();

  if (tlsRestartPending) {
    // Смена сертификата уже запрошена — этим займётся handleTlsServer().
    return;
  }

  if (!tlsCertCacheValid || millis() - tlsCertCacheAt >= 60000UL) {
    tlsCertCacheValid = true;
    tlsCertCacheAt = millis();
    tlsCertCacheDays = tlsCertificateDaysLeft();
    tlsCertCachePresent = tlsMaterialPresent();
  }

  int daysLeft = tlsCertCacheDays;
  bool needRefresh = !tlsCertCachePresent ||
                     (daysLeft >= 0 && daysLeft < DED_CORE_TLS_RENEW_DAYS);

  if (!needRefresh) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  // За сертификатом идём только когда HTTPS уже поднят: в этом режиме Bluetooth отпущен,
  // и в куче есть непрерывный участок под буферы рукопожатия с сервисом. Рядом с
  // Bluetooth памяти хватает не на всякий случай, а рукопожатие занимает ядро 0 так
  // надолго, что сторожевик задачи роняет блок. Пустой сертификат забирается на старте
  // (tlsStartupCertificateCheck), когда Bluetooth ещё не поднят.
  if (!tlsIsRunning()) {
    return;
  }

  // Не долбим сервис: раз в час достаточно.
  if (lastAttempt != 0 && millis() - lastAttempt < 3600000UL) {
    return;
  }

  lastAttempt = millis();

  String error;
  bool fetched;

  // Вызывается из задачи сервера (ядро 0): проверка подписи и расшифровка держат ядро
  // занятым дольше пяти секунд, и сторожевой таймер ядра успевает сработать.
  disableCore0WDT();
  fetched = tlsFetchFromService(error);
  enableCore0WDT();

  if (!fetched) {
    Serial.print("TLS: refresh failed: ");
    Serial.println(error);
  }
}

void tlsSendFrameToClients(const String &json) {
  httpd_ws_frame_t frame = {};
  frame.type = HTTPD_WS_TYPE_TEXT;
  frame.payload = (uint8_t *)json.c_str();
  frame.len = json.length();

  for (int i = 0; i < DED_CORE_TLS_MAX_CLIENTS; i++) {
    int fd = tlsClients[i];
    if (fd < 0) {
      continue;
    }

    if (httpd_ws_get_fd_info(tlsServer, fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
      tlsClients[i] = -1;
      continue;
    }

    httpd_ws_send_frame_async(tlsServer, fd, &frame);
  }
}

// Кадр приходит из другой задачи, а httpd рассылает только из своей: кадр кладём в кучу
// и передаём владельцем задания — иначе строку мог бы перезаписать следующий вызов.
static void tlsBroadcastFrameWork(void *arg) {
  String *payload = static_cast<String *>(arg);
  if (payload == nullptr) {
    return;
  }

  if (tlsServer != nullptr) {
    tlsSendFrameToClients(*payload);
  }

  delete payload;
}

void tlsBroadcastFrame(const String &json) {
  // Без подключённых wss-клиентов кадр не нужен: сборка JSON и задание в httpd — это
  // десятки выделений кучи в секунду, а рукопожатию TLS нужен целый участок памяти.
  if (tlsServer == nullptr || !tlsHasWsClients()) {
    return;
  }

  String *payload = new String(json);
  if (httpd_queue_work(tlsServer, tlsBroadcastFrameWork, payload) != ESP_OK) {
    delete payload;
  }
}

void tlsBroadcastWork(void *arg) {
  if (tlsServer == nullptr || !tlsHasWsClients()) {
    return;
  }

  tlsSendFrameToClients(getDataJson());
}

// Корневой сертификат в цепочке не нужен: он уже есть у клиента. Служба отдаёт
// leaf + два промежуточных + корень; лишний PEM стоит ~1.6 КБ в куче и столько же
// при разборе — в сборке с Bluetooth каждый килобайт на счету.
static void tlsTrimTrailingRoot(String &pem) {
  int index = 0;
  int starts[6];
  int count = 0;

  while (count < 6) {
    int found = pem.indexOf("-----BEGIN CERTIFICATE-----", index);
    if (found < 0) {
      break;
    }

    starts[count++] = found;
    index = found + 1;
  }

  if (count <= 3) {
    return;
  }

  pem = pem.substring(0, starts[3]);
}

// Материал TLS в NVS: сертификат вместе с цепочкой ~3.5 КБ, ключ ~0.3 КБ.
// Preferences отдаёт их как blob: строкой нельзя, у NVS предел 4000 байт на значение.
bool tlsMaterialToNvs(const char *cert, const char *key) {
  size_t certLength = strlen(cert) + 1;
  size_t keyLength = strlen(key) + 1;

  if (preferences.putBytes(DED_CORE_TLS_CERT_NVS, cert, certLength) != certLength) {
    Serial.printf("TLS: cannot store certificate in NVS, free entries=%u\n",
                  (unsigned)preferences.freeEntries());
    return false;
  }

  if (preferences.putBytes(DED_CORE_TLS_KEY_NVS, key, keyLength) != keyLength) {
    Serial.println("TLS: cannot store key in NVS");
    return false;
  }

  return true;
}

static bool tlsMaterialFromNvs(String &cert, String &key) {
  size_t certLength = preferences.getBytesLength(DED_CORE_TLS_CERT_NVS);
  size_t keyLength = preferences.getBytesLength(DED_CORE_TLS_KEY_NVS);

  if (certLength < 2 || keyLength < 2 || certLength > DED_CORE_TLS_MAX_UPLOAD || keyLength > 2048) {
    return false;
  }

  char *certBuffer = (char *)malloc(certLength);
  char *keyBuffer = (char *)malloc(keyLength);

  if (certBuffer == nullptr || keyBuffer == nullptr) {
    free(certBuffer);
    free(keyBuffer);
    return false;
  }

  bool loaded = preferences.getBytes(DED_CORE_TLS_CERT_NVS, certBuffer, certLength) == certLength &&
                preferences.getBytes(DED_CORE_TLS_KEY_NVS, keyBuffer, keyLength) == keyLength;

  if (loaded) {
    certBuffer[certLength - 1] = 0;
    keyBuffer[keyLength - 1] = 0;
    cert = certBuffer;
    key = keyBuffer;
  }

  free(certBuffer);
  free(keyBuffer);
  return loaded && cert.length() > 0 && key.length() > 0;
}

bool tlsMaterialPresent() {
  if (preferences.getBytesLength(DED_CORE_TLS_CERT_NVS) > 1) {
    return true;
  }

  return DEVICE_FS.exists(DED_CORE_TLS_CERT_FILE);
}

bool tlsReadFile(const char *path, String &out) {
  File file = DEVICE_FS.open(path, "r");
  if (!file) {
    return false;
  }

  out = file.readString();
  file.close();
  return out.length() > 0;
}

// Откуда брать материал: сначала NVS, потом файлы (и тогда переносим их в NVS,
// чтобы следующее обновление интерфейса сертификат уже не стёрло).
static bool tlsLoadMaterial(String &cert, String &key) {
  if (tlsMaterialFromNvs(cert, key)) {
    return true;
  }

  if (!tlsReadFile(DED_CORE_TLS_CERT_FILE, cert) || !tlsReadFile(DED_CORE_TLS_KEY_FILE, key)) {
    return false;
  }

  tlsMaterialToNvs(cert.c_str(), key.c_str());
  return true;
}

void stopTlsServer() {
  if (tlsServer == nullptr) {
    return;
  }

  httpd_stop(tlsServer);
  tlsServer = nullptr;
  tlsTransportRelease();

  for (int i = 0; i < DED_CORE_TLS_MAX_CLIENTS; i++) {
    tlsClients[i] = -1;
  }
}

// --- API устройства на HTTPS ------------------------------------------------
//
// Пока HTTPS активен, обычный HTTP-сервер остановлен (иначе не хватает памяти),
// поэтому страница по HTTPS обращается к тем же эндпоинтам здесь.

// Копия jsonEscape из DEVICE_COMMANDS.h: включать этот заголовок здесь нельзя (он
// определяет функции, и в этой единице трансляции появились бы дубликаты).
static String tlsJsonEscape(const String &value) {
  String out;

  for (unsigned int index = 0; index < value.length(); index++) {
    char symbol = value[index];

    if (symbol == '"' || symbol == '\\') {
      out += '\\';
      out += symbol;
    } else if (symbol == '\n') {
      out += "\\n";
    } else if (symbol >= 0x20) {
      out += symbol;
    }
  }

  return out;
}

static esp_err_t tlsSendBody(httpd_req_t *req, const char *type, const String &body) {
  // Без keep-alive: каждый открытый сокет держит буферы mbedTLS, а их в сборке с
  // Bluetooth мало. Курл и браузер переподключатся, зато следующий запрос не упадёт.
  httpd_resp_set_hdr(req, "Connection", "close");
  httpd_resp_set_type(req, type);
  return httpd_resp_send(req, body.c_str(), body.length());
}

esp_err_t tlsDeviceStatusHandler(httpd_req_t *req) {
  bool sta_connected = (WiFi.status() == WL_CONNECTED && WiFi.SSID() == WIFI_UPDATE_SSID);
  FirmwareInfo info = otaManager.getLastFirmwareInfo();

  String response = "{";
  response += "\"wifi\":{";
  response += "\"mode\":\"" + String((WiFi.getMode() == WIFI_MODE_AP) ? "ap" : (WiFi.getMode() == WIFI_MODE_STA ? "sta" : "ap_sta")) + "\",";
  response += "\"ap_ssid\":\"" + tlsJsonEscape(APSSID1) + "\",";
  response += "\"sta_ssid\":\"" + tlsJsonEscape(WIFI_UPDATE_SSID) + "\",";
  response += "\"sta_connected\":" + String(sta_connected ? "true" : "false") + ",";
  response += "\"status\":\"" + tlsJsonEscape(WIFI_UPDATE_STATUS) + "\",";
  response += "\"last_check_time\":" + String(WIFI_LAST_CHECK_TIME) + ",";
  response += "\"last_attempt_time\":" + String(WIFI_LAST_ATTEMPT_TIME) + ",";
  unsigned long next_check = 0;
  if (WIFI_LAST_ATTEMPT_TIME != 0) next_check = (WIFI_LAST_ATTEMPT_TIME + WIFI_RETRY_INTERVAL_MS > millis()) ? (WIFI_LAST_ATTEMPT_TIME + WIFI_RETRY_INTERVAL_MS - millis()) : 0;
  response += "\"next_check_ms\":" + String(next_check);
  response += "},";

  response += "\"ota\":{";
  response += "\"currentVersion\":\"" + otaManager.getCurrentVersion() + "\",";
  response += "\"mode\":\"" + otaManager.getModeString() + "\",";
  response += "\"initialized\":" + String(otaManager.isInitialized() ? "true" : "false") + ",";
  response += "\"updateMode\":" + String(UPDATE_MODE ? "true" : "false") + ",";
  response += "\"updateAvailable\":" + String(info.updateAvailable ? "true" : "false") + ",";
  response += "\"lastFirmwareInfo\":{";
  response += "\"version\":\"" + info.version + "\",";
  response += "\"size\":\"" + String(info.size) + "\",";
  response += "\"md5\":\"" + info.md5 + "\",";
  response += "\"sha256\":\"" + info.sha256 + "\",";
  response += "\"changelog\":\"" + info.changelog + "\"";
  response += "}";
  response += "},";

  response += "\"motor\":{";
  response += "\"rpm\":" + String(RPM) + ",";
  response += "\"throttle\":" + String(Throttle) + ",";
  response += "\"motor_running\":" + String(RPM > 0 ? "true" : "false");
  response += "},";

  response += "\"config\":{";
  response += "\"autoUpdateEnabled\":" + String(WIFI_AUTO_UPDATE_ENABLED ? "true" : "false") + ",";
  response += "\"wifiUpdateSSID\":\"" + tlsJsonEscape(WIFI_UPDATE_SSID) + "\",";
  response += "\"wifiPasswordSet\":" + String(WIFI_UPDATE_PASSWORD.length() > 0 ? "true" : "false") + ",";
  response += "\"wifiConnected\":" + String(sta_connected ? "true" : "false") + ",";
  response += "\"retryIntervalMs\":" + String(WIFI_RETRY_INTERVAL_MS) + ",";
  response += "\"nextCheckMs\":" + String(next_check);
  response += "}";

  response += "}";

  return tlsSendBody(req, "application/json", response);
}

esp_err_t tlsDeviceConfigHandler(httpd_req_t *req) {
  String resp = "{";
  resp += "\"activeCart\":" + String(active_cart) + ",";
  resp += "\"type\":" + String(type) + ",";
  resp += "\"autoUpdateEnabled\":" + String(WIFI_AUTO_UPDATE_ENABLED ? "true" : "false") + ",";
  resp += "\"wifiUpdateSSID\":\"" + tlsJsonEscape(WIFI_UPDATE_SSID) + "\",";
  resp += "\"wifiPasswordSet\":" + String(WIFI_UPDATE_PASSWORD.length() > 0 ? "true" : "false") + ",";
  resp += "\"quickShifterEnabled\":" + String(QUICKSHIFTER ? "true" : "false") + ",";
  resp += "\"angleAdvance\":" + String(ANGLE_ADVANCE ? "true" : "false") + ",";
  resp += "\"servoEnabled\":" + String(SERVO ? "true" : "false") + ",";
  resp += "\"servoType\":" + String(Servo_type) + ",";
  resp += "\"servoRpm\":" + String(Servo_rpm) + ",";
  resp += "\"servoPercent\":" + String(Servo_percent) + ",";
  resp += "\"invertSensor\":" + String(INVERT_SENSOR ? "true" : "false") + ",";
  resp += "\"invertThrottle\":" + String(INVERT_THROTTLE ? "true" : "false") + ",";
  resp += "\"maxRpm\":" + String(max_rpm) + ",";
  resp += "\"maxRpmAngleKicker\":" + String(max_rpm_angle_kiker);
  resp += "}";
  return tlsSendBody(req, "application/json", resp);
}

esp_err_t tlsVersionHandler(httpd_req_t *req) {
  return tlsSendBody(req, "text/plain", String(FIRMWARE_VERSION));
}

esp_err_t tlsRebootHandler(httpd_req_t *req) {
  if (!tlsRequestAuthorized(req)) {
    return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "X-Device-Key required");
  }

  httpd_resp_send(req, "REBOOT", HTTPD_RESP_USE_STRLEN);
  delay(200);
  ESP.restart();
  return ESP_OK;
}

static void tlsOtaUpdateJob() {
  if (!otaManager.performUpdate()) {
    Serial.println("OTA: update from HTTPS failed");
  }
}

static void tlsOtaUpdateEntry(void *argument) {
  tlsOtaUpdateJob();
  vTaskDelete(nullptr);
}

esp_err_t tlsOtaUpdateHandler(httpd_req_t *req) {
  if (!tlsRequestAuthorized(req)) {
    return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "X-Device-Key required");
  }

  if (RPM > 100) {
    httpd_resp_set_status(req, "409 Conflict");
    return tlsSendBody(req, "text/plain", String("Двигатель работает — обновление отложено"));
  }

  if (otaManager.isUpdating()) {
    httpd_resp_set_status(req, "409 Conflict");
    return tlsSendBody(req, "text/plain", String("Обновление уже идёт"));
  }

  if (xTaskCreatePinnedToCore(tlsOtaUpdateEntry, "TaskOtaUpdateHttps", 24576, nullptr, 1, nullptr, 0) != pdPASS) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    return tlsSendBody(req, "text/plain", String("Не хватает памяти на обновление"));
  }

  httpd_resp_set_status(req, "202 Accepted");
  return tlsSendBody(req, "text/plain", String("Update initiated, rebooting..."));
}

esp_err_t tlsModuleForgetHandler(httpd_req_t *req) {
  if (!tlsRequestAuthorized(req)) {
    return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "X-Device-Key required");
  }

  dedModuleForget();
  return tlsSendBody(req, "application/json", String("{\"event\":\"module_forget\",\"data\":{\"ok\":1}}"));
}

esp_err_t tlsModulePairStartHandler(httpd_req_t *req) {
  if (!tlsRequestAuthorized(req)) {
    return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "X-Device-Key required");
  }

  return tlsSendBody(req, "application/json", dedModulePairStart());
}

esp_err_t tlsModulePairStopHandler(httpd_req_t *req) {
  if (!tlsRequestAuthorized(req)) {
    return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "X-Device-Key required");
  }

  return tlsSendBody(req, "application/json", dedModulePairStop());
}

// true — HTTPS/WSS поднялся. Без сертификата устройство остаётся на HTTP (80) + WS (81).
// --- TLS-транспорт поверх httpd ---------------------------------------------
//
// esp_https_server разбирает PEM-цепочку и ключ заново на каждое соединение
// (esp_tls_server_session_create → mbedtls_x509_crt_parse + mbedtls_pk_parse_key)
// и держит ради этого esp_tls_t. На ESP32 без PSRAM это 8–11 КБ на каждую сессию,
// причём при обрыве рукопожатия память возвращается не полностью.
//
// Здесь сертификат, ключ и mbedtls_ssl_config разбираются один раз на старте сервера,
// а на соединение остаются mbedtls_ssl_context (552 Б) и его буферы. httpd при этом
// продолжает обрабатывать запросы штатно — мы подменяем только транспорт.

struct DedCoreTlsTransport {
  mbedtls_ssl_context ssl;
  // mbedtls_net_send/recv ждут mbedtls_net_context, внутри это обычный fd.
  int fd;
};

static int tlsTransportRecv(httpd_handle_t handle, int sockfd, char *buffer, size_t bufferLength, int flags);
static int tlsTransportSend(httpd_handle_t handle, int sockfd, const char *buffer, size_t bufferLength, int flags);
static int tlsTransportPending(httpd_handle_t handle, int sockfd);

static mbedtls_ssl_config tlsTransportConfig;
static mbedtls_x509_crt tlsTransportCert;
static mbedtls_pk_context tlsTransportKey;
static mbedtls_entropy_context tlsTransportEntropy;
static mbedtls_ctr_drbg_context tlsTransportDrbg;
static bool tlsTransportReady = false;

// Диагностика нехватки памяти: mbedTLS считает своим только то, что выделил сам,
// и при отказе рукопожатие возвращает общий ALLOC_FAILED. По этой строке видно, какой
// именно блок не поместился и что оставалось в куче — по ней и разбирались с сессиями.
static void *tlsTrackedCalloc(size_t count, size_t size) {
  void *block = calloc(count, size);

  if (block == nullptr) {
    Serial.printf("TLS: mbedtls calloc failed count=%u size=%u free=%u max=%u\n",
                  (unsigned)count, (unsigned)size, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    Serial.printf("TLS: heap 8bit=%u/%u int=%u/%u dma=%u/%u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
  }

  return block;
}

static void tlsTransportRelease() {
  if (!tlsTransportReady) {
    return;
  }

  mbedtls_ssl_config_free(&tlsTransportConfig);
  mbedtls_x509_crt_free(&tlsTransportCert);
  mbedtls_pk_free(&tlsTransportKey);
  mbedtls_ctr_drbg_free(&tlsTransportDrbg);
  mbedtls_entropy_free(&tlsTransportEntropy);
  tlsTransportReady = false;
}

static bool tlsTransportPrepare(const String &certPem, const String &keyPem) {
  tlsTransportRelease();

  // mbedTLS выделяет память только через эти функции. Свой calloc нужен, чтобы в лог
  // попадал размер блока, которого не хватило: без него рукопожатие падает молча.
  static bool allocatorInstalled = false;
  if (!allocatorInstalled) {
    mbedtls_platform_set_calloc_free(tlsTrackedCalloc, free);
    allocatorInstalled = true;
  }

  mbedtls_ssl_config_init(&tlsTransportConfig);
  mbedtls_x509_crt_init(&tlsTransportCert);
  mbedtls_pk_init(&tlsTransportKey);
  mbedtls_entropy_init(&tlsTransportEntropy);
  mbedtls_ctr_drbg_init(&tlsTransportDrbg);

  int ret = mbedtls_x509_crt_parse(&tlsTransportCert, (const unsigned char *)certPem.c_str(),
                                   certPem.length() + 1);
  if (ret < 0) {
    Serial.printf("TLS: certificate parse failed -0x%04X\n", -ret);
    tlsTransportRelease();
    return false;
  }

  ret = mbedtls_pk_parse_key(&tlsTransportKey, (const unsigned char *)keyPem.c_str(),
                             keyPem.length() + 1, nullptr, 0);
  if (ret != 0) {
    Serial.printf("TLS: key parse failed -0x%04X\n", -ret);
    tlsTransportRelease();
    return false;
  }

  ret = mbedtls_ssl_config_defaults(&tlsTransportConfig, MBEDTLS_SSL_IS_SERVER,
                                    MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
  if (ret != 0) {
    Serial.printf("TLS: config defaults failed -0x%04X\n", -ret);
    tlsTransportRelease();
    return false;
  }

  // Клиент не представляется сертификатом: пара «ключ устройства» проверяется кадром auth.
  mbedtls_ssl_conf_authmode(&tlsTransportConfig, MBEDTLS_SSL_VERIFY_NONE);

  ret = mbedtls_ctr_drbg_seed(&tlsTransportDrbg, mbedtls_entropy_func, &tlsTransportEntropy, nullptr, 0);
  if (ret != 0) {
    Serial.printf("TLS: rng seed failed -0x%04X\n", -ret);
    tlsTransportRelease();
    return false;
  }

  mbedtls_ssl_conf_rng(&tlsTransportConfig, mbedtls_ctr_drbg_random, &tlsTransportDrbg);
  mbedtls_ssl_conf_own_cert(&tlsTransportConfig, &tlsTransportCert, &tlsTransportKey);

#if defined(MBEDTLS_SSL_SESSION_TICKETS)
  // Сессии не переиспользуются (Connection: close), а билеты держат буфер на каждое
  // соединение — в сборке с Bluetooth это лишние сотни байт.
  mbedtls_ssl_conf_session_tickets(&tlsTransportConfig, MBEDTLS_SSL_SESSION_TICKETS_DISABLED);
#endif

#if defined(MBEDTLS_SSL_RENEGOTIATION)
  mbedtls_ssl_conf_renegotiation(&tlsTransportConfig, MBEDTLS_SSL_RENEGOTIATION_DISABLED);
#endif

  tlsTransportReady = true;

  Serial.printf("TLS: certificate and key loaded once, heap=%u max=%u\n",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  return true;
}

static void tlsTransportFree(void *context) {
  DedCoreTlsTransport *transport = (DedCoreTlsTransport *)context;
  if (transport == nullptr) {
    return;
  }

  mbedtls_ssl_free(&transport->ssl);
  free(transport);

  Serial.printf("TLS: session closed heap=%u max=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

static esp_err_t tlsTransportOpen(httpd_handle_t handle, int sockfd) {
  if (!tlsTransportReady) {
    return ESP_ERR_INVALID_STATE;
  }

  DedCoreTlsTransport *transport = (DedCoreTlsTransport *)calloc(1, sizeof(DedCoreTlsTransport));
  if (transport == nullptr) {
    Serial.printf("TLS: no memory for session, heap=%u max=%u\n",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    return ESP_ERR_NO_MEM;
  }

  transport->fd = sockfd;
  mbedtls_ssl_init(&transport->ssl);

  int ret = mbedtls_ssl_setup(&transport->ssl, &tlsTransportConfig);
  if (ret == 0) {
    mbedtls_ssl_set_bio(&transport->ssl, &transport->fd, mbedtls_net_send, mbedtls_net_recv, nullptr);

    unsigned long started = millis();
    while ((ret = mbedtls_ssl_handshake(&transport->ssl)) != 0) {
      if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
        break;
      }

      if (millis() - started >= DED_CORE_TLS_HANDSHAKE_MS) {
        ret = MBEDTLS_ERR_SSL_TIMEOUT;
        break;
      }

      delay(1);
    }
  }

  if (ret != 0) {
    Serial.printf("TLS: handshake failed -0x%04X heap=%u max=%u\n",
                  -ret, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    tlsTransportFree(transport);
    return ESP_FAIL;
  }

  httpd_sess_set_transport_ctx(handle, sockfd, transport, tlsTransportFree);
  httpd_sess_set_recv_override(handle, sockfd, tlsTransportRecv);
  httpd_sess_set_send_override(handle, sockfd, tlsTransportSend);
  httpd_sess_set_pending_override(handle, sockfd, tlsTransportPending);

  // Nagle складывает мелкие TLS-записи в ожидании подтверждения, и каждая отдача
  // статики превращается в «килобайт за 50 мс». Нам важнее отзывчивость, чем экономия пакетов.
  int noDelay = 1;
  setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &noDelay, sizeof(noDelay));

  Serial.printf("TLS: session open fd=%d heap=%u max=%u\n",
                sockfd, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  return ESP_OK;
}

// close_fn httpd вызывается вместо close(fd): сокет закрываем сами, канал шифрованный.
static void tlsTransportShutdown(httpd_handle_t handle, int sockfd) {
  DedCoreTlsTransport *transport = (DedCoreTlsTransport *)httpd_sess_get_transport_ctx(handle, sockfd);
  if (transport != nullptr) {
    mbedtls_ssl_close_notify(&transport->ssl);
  }

  close(sockfd);
}

static int tlsTransportReadError(int error) {
  if (error == MBEDTLS_ERR_SSL_WANT_READ || error == MBEDTLS_ERR_SSL_WANT_WRITE) {
    return HTTPD_SOCK_ERR_TIMEOUT;
  }

  return HTTPD_SOCK_ERR_FAIL;
}

static int tlsTransportRecv(httpd_handle_t handle, int sockfd, char *buffer, size_t bufferLength, int flags) {
  DedCoreTlsTransport *transport = (DedCoreTlsTransport *)httpd_sess_get_transport_ctx(handle, sockfd);
  if (transport == nullptr) {
    return HTTPD_SOCK_ERR_FAIL;
  }

  int received = mbedtls_ssl_read(&transport->ssl, (unsigned char *)buffer, bufferLength);

  // Штатное закрытие потока — это конец данных, а не ошибка.
  if (received == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
    return 0;
  }

  return received < 0 ? tlsTransportReadError(received) : received;
}

static int tlsTransportSend(httpd_handle_t handle, int sockfd, const char *buffer, size_t bufferLength, int flags) {
  DedCoreTlsTransport *transport = (DedCoreTlsTransport *)httpd_sess_get_transport_ctx(handle, sockfd);
  if (transport == nullptr) {
    return HTTPD_SOCK_ERR_FAIL;
  }

  int written = mbedtls_ssl_write(&transport->ssl, (const unsigned char *)buffer, bufferLength);
  return written < 0 ? tlsTransportReadError(written) : written;
}

static int tlsTransportPending(httpd_handle_t handle, int sockfd) {
  DedCoreTlsTransport *transport = (DedCoreTlsTransport *)httpd_sess_get_transport_ctx(handle, sockfd);
  return transport == nullptr ? HTTPD_SOCK_ERR_FAIL : mbedtls_ssl_get_bytes_avail(&transport->ssl);
}

bool startTlsServer() {

  if (!DEVICE_FS.begin(true)) {
    Serial.println("TLS: filesystem is not available");
    return false;
  }

  if (!tlsLoadMaterial(tlsCertPem, tlsKeyPem)) {
    Serial.println("TLS: certificate is not installed — HTTPS disabled");
    return false;
  }

  tlsTrimTrailingRoot(tlsCertPem);

  Serial.printf("TLS: heap before start free=%u maxAlloc=%u int=%u dma=%u dmaMax=%u\n",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_free_size(MALLOC_CAP_DMA),
                heap_caps_get_largest_free_block(MALLOC_CAP_DMA));

  // Одному TLS-соединению нужны два буфера mbedTLS по 16 КБ и запас на рукопожатие,
  // а самому серверу — 14 КБ стека. В сборке с Bluetooth такого запаса нет: после
  // старта HTTPS в куче остаётся ~15 КБ, и перестаёт отвечать даже обычный HTTP.
  // Поэтому при нехватке памяти HTTPS не поднимаем, а блок продолжает работать по HTTP.
  if (ESP.getMaxAllocHeap() < DED_CORE_TLS_MIN_HEAP) {
    Serial.printf("TLS: not enough heap for HTTPS (free=%u maxAlloc=%u) — HTTPS disabled\n",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    return false;
  }

  if (!tlsTransportPrepare(tlsCertPem, tlsKeyPem)) {
    Serial.println("TLS: certificate rejected — HTTPS disabled");
    return false;
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = DED_CORE_TLS_PORT;
  config.stack_size = DED_CORE_TLS_STACK;
  config.max_uri_handlers = 13;
  config.uri_match_fn = httpd_uri_match_wildcard;
  // Каждое TLS-соединение держит контекст mbedTLS и его буферы, поэтому очередь
  // открытых сокетов урезана: странице нужны документ, статика и wss — и всё.
  config.max_open_sockets = DED_CORE_TLS_OPEN_SOCKETS;
  // Новые соединения ждут в очереди, а не вытесняют живые: вытеснение рвёт уже
  // открытую сессию (wss-поток), а ее память возвращается не сразу.
  config.lru_purge_enable = false;
  // Без этого брошенное соединение держит сессию и её буферы, пока клиент сам не
  // отвалится по таймауту, и очередь заполняется мусором.
  config.recv_wait_timeout = 5;
  config.send_wait_timeout = 5;
  // Транспорт подменяем здесь, а не через esp_https_server: тот разбирает сертификат
  // и ключ на каждое соединение (см. tlsTransportPrepare).
  config.open_fn = tlsTransportOpen;
  config.close_fn = tlsTransportShutdown;
  // Соединение между запросами не держим: каждая лишняя сессия — это ещё один набор
  // буферов, а памяти рядом с Bluetooth в обрез. Заголовок Connection: close ставит
  // каждый ответ (tlsSendBody и статика).

  if (httpd_start(&tlsServer, &config) != ESP_OK) {
    tlsServer = nullptr;
    tlsTransportRelease();
    Serial.println("TLS: httpd_start failed");
    return false;
  }

  // Сертификат и ключ разобраны в структуры mbedTLS; PEM в куче больше не нужен
  // (при смене сертификата файлы читаются заново).
  tlsCertPem = String();
  tlsKeyPem = String();
  Serial.printf("TLS: heap after start free=%u maxAlloc=%u int=%u dma=%u dmaMax=%u\n",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_free_size(MALLOC_CAP_DMA),
                heap_caps_get_largest_free_block(MALLOC_CAP_DMA));

  httpd_uri_t wsUri = {};
  wsUri.uri = DED_CORE_TLS_WS_PATH;
  wsUri.method = HTTP_GET;
  wsUri.handler = tlsWsHandler;
  wsUri.is_websocket = true;
  wsUri.handle_ws_control_frames = false;
  httpd_register_uri_handler(tlsServer, &wsUri);

  httpd_uri_t statusUri = {};
  statusUri.uri = "/api/status";
  statusUri.method = HTTP_GET;
  statusUri.handler = tlsStatusHandler;
  httpd_register_uri_handler(tlsServer, &statusUri);

  httpd_uri_t certUri = {};
  certUri.uri = "/api/tls";
  certUri.method = HTTP_POST;
  certUri.handler = tlsCertUploadHandler;
  httpd_register_uri_handler(tlsServer, &certUri);

  httpd_uri_t aiGrantUri = {};
  aiGrantUri.uri = "/api/ai/grant";
  aiGrantUri.method = HTTP_POST;
  aiGrantUri.handler = tlsAiGrantHandler;
  httpd_register_uri_handler(tlsServer, &aiGrantUri);

  httpd_uri_t deviceStatusUri = {};
  deviceStatusUri.uri = "/status";
  deviceStatusUri.method = HTTP_GET;
  deviceStatusUri.handler = tlsDeviceStatusHandler;
  httpd_register_uri_handler(tlsServer, &deviceStatusUri);

  httpd_uri_t deviceConfigUri = {};
  deviceConfigUri.uri = "/device/config";
  deviceConfigUri.method = HTTP_GET;
  deviceConfigUri.handler = tlsDeviceConfigHandler;
  httpd_register_uri_handler(tlsServer, &deviceConfigUri);

  httpd_uri_t versionUri = {};
  versionUri.uri = "/core/version";
  versionUri.method = HTTP_GET;
  versionUri.handler = tlsVersionHandler;
  httpd_register_uri_handler(tlsServer, &versionUri);

  httpd_uri_t rebootUri = {};
  rebootUri.uri = "/core/reboot";
  rebootUri.method = HTTP_GET;
  rebootUri.handler = tlsRebootHandler;
  httpd_register_uri_handler(tlsServer, &rebootUri);

  httpd_uri_t otaUpdateUri = {};
  otaUpdateUri.uri = "/ota/update";
  otaUpdateUri.method = HTTP_POST;
  otaUpdateUri.handler = tlsOtaUpdateHandler;
  httpd_register_uri_handler(tlsServer, &otaUpdateUri);

  httpd_uri_t moduleForgetUri = {};
  moduleForgetUri.uri = "/module/forget";
  moduleForgetUri.method = HTTP_POST;
  moduleForgetUri.handler = tlsModuleForgetHandler;
  httpd_register_uri_handler(tlsServer, &moduleForgetUri);

  httpd_uri_t modulePairStartUri = {};
  modulePairStartUri.uri = "/module/pair/start";
  modulePairStartUri.method = HTTP_POST;
  modulePairStartUri.handler = tlsModulePairStartHandler;
  httpd_register_uri_handler(tlsServer, &modulePairStartUri);

  httpd_uri_t modulePairStopUri = {};
  modulePairStopUri.uri = "/module/pair/stop";
  modulePairStopUri.method = HTTP_POST;
  modulePairStopUri.handler = tlsModulePairStopHandler;
  httpd_register_uri_handler(tlsServer, &modulePairStopUri);

  httpd_uri_t staticUri = {};
  staticUri.uri = "/*";
  staticUri.method = HTTP_GET;
  staticUri.handler = tlsStaticHandler;
  httpd_register_uri_handler(tlsServer, &staticUri);

  // Сервер уже читает актуальные файлы: отложенный перезапуск больше не нужен.
  tlsRestartPending = false;

  Serial.printf("TLS: https/wss on port %d (%s)\n", DED_CORE_TLS_PORT, DED_CORE_TLS_HOST);
  return true;
}

// Вызывается из задачи сервера: рассылка телеметрии и отложенный перезапуск после смены сертификата.
// Перезапуск делаем здесь, а не в обработчике запроса: нельзя останавливать httpd из его же задачи.
void handleTlsServer() {
  if (tlsBootRefreshRequested) {
    // Ответ на запрос уже ушёл; перезагружаемся, чтобы забрать сертификат на старте.
    Serial.println("TLS: rebooting to fetch certificate");
    delay(300);
    ESP.restart();
  }

  if (tlsRestartPending) {
    tlsRestartPending = false;
    Serial.println("TLS: applying new certificate");
    startTlsServer();
  }

  if (!tlsIsRunning()) {
    return;
  }

  if (millis() - tlsLastBroadcast < DED_CORE_TLS_BROADCAST_MS) {
    return;
  }

  tlsLastBroadcast = millis();
  httpd_queue_work(tlsServer, tlsBroadcastWork, nullptr);
}


// --- Арбитраж памяти: Bluetooth или HTTPS --------------------------------
//
// Замеры на этом ядре: с поднятым NimBLE после старта TLS-сервера остаётся ~16 КБ,
// а mbedtls_ssl_setup требует больше — рукопожатие падает с ALLOC_FAILED. Если же
// отпустить Bluetooth (NimBLEDevice::deinit, обратимо), свободно ~78 КБ и HTTPS/WSS
// работают вместе с HTTP. Поэтому владелец памяти один, а переключение идёт по запросу.

#define DED_CORE_TLS_IDLE_MS     120000UL   // без клиентов столько — возвращаем Bluetooth
#define DED_CORE_TLS_PROBE_STACK 3072

static volatile bool tlsHttpsRequested = false;
static volatile bool tlsBleRequested = false;
static volatile bool tlsProbeWanted = false;
static volatile bool tlsProbeListening = false;
static bool tlsProbeTaskStarted = false;
static unsigned long tlsIdleSince = 0;

// Пробник живёт всё время: его стек выделяется один раз на старте, когда куча ещё
// велика. Создавать задачу на каждое переключение нельзя — освобождение 3 КБ стека
// прямо перед стартом HTTPS дробит кучу, и рукопожатию не хватает непрерывного блока.
void tlsProbeTask(void *argument) {
  int listener = -1;

  for (;;) {
    if (tlsProbeWanted && listener < 0) {
      listener = socket(AF_INET, SOCK_STREAM, 0);

      if (listener >= 0) {
        int reuse = 1;
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        address.sin_port = htons(DED_CORE_TLS_PORT);

        if (bind(listener, (sockaddr *)&address, sizeof(address)) < 0 || listen(listener, 1) < 0) {
          Serial.println("TLS: HTTPS probe bind failed");
          close(listener);
          listener = -1;
          tlsProbeWanted = false;
        } else {
          tlsProbeListening = true;
        }
      }
    }

    if (!tlsProbeWanted && listener >= 0) {
      close(listener);
      listener = -1;
      tlsProbeListening = false;
    }

    if (listener < 0) {
      vTaskDelay(50);
      continue;
    }

    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(listener, &readable);

    timeval timeout = { 0, 200000 };

    if (select(listener + 1, &readable, nullptr, nullptr, &timeout) > 0) {
      int client = accept(listener, nullptr, nullptr);
      if (client >= 0) {
        close(client);
      }

      close(listener);
      listener = -1;
      tlsProbeListening = false;
      tlsProbeWanted = false;
      tlsHttpsRequested = true;
    }
  }
}

static void tlsModeArmProbe() {
  if (tlsProbeWanted || tlsIsRunning()) {
    return;
  }

  // Проверку файла кэшируем: tlsModeHandle вызывается каждые 2 мс, а обращение к LittleFS
  // не бесплатное.
  static unsigned long checkedAt = 0;
  static bool present = false;

  if (checkedAt == 0 || millis() - checkedAt >= 60000UL) {
    checkedAt = millis();
    present = tlsMaterialPresent();
  }

  if (!present) {
    return;
  }

  if (!tlsProbeTaskStarted) {
    tlsProbeTaskStarted = xTaskCreatePinnedToCore(tlsProbeTask, "TaskTlsProbe",
                                                  DED_CORE_TLS_PROBE_STACK, nullptr, 1,
                                                  nullptr, 0) == pdPASS;
    if (!tlsProbeTaskStarted) {
      Serial.println("TLS: HTTPS probe task failed");
      return;
    }
  }

  tlsProbeWanted = true;
}

void tlsModeInit() {
#ifdef DED_CORE_NO_BLE
  // Bluetooth в этой сборке нет — держим HTTPS постоянно.
  startTlsServer();
#else
  // Сертификат проверен на старте (tlsStartupCertificateCheck), HTTPS поднимется по
  // первому запросу: держать его постоянно вместе с Bluetooth памяти не хватает.
  tlsModeArmProbe();
#endif
}

void tlsModeRequestHttps() {
  tlsHttpsRequested = true;
}

void tlsModeRequestBle() {
  tlsBleRequested = true;
}

bool tlsModeHttpsActive() {
  return tlsIsRunning();
}

static void tlsModeToHttps() {
  // Пробник может ещё держать 443 — даём ему закрыть сокет до старта httpd.
  tlsProbeWanted = false;
  unsigned long started = millis();
  while (tlsProbeListening && millis() - started < 3000UL) {
    vTaskDelay(20);
  }

  Serial.println("TLS: HTTPS requested, releasing Bluetooth");

  // Обычные серверы на время HTTPS не нужны: их сокеты и буферы — та самая память,
  // которой не хватает TLS-рукопожатию рядом с Bluetooth.
  stopPlainWebServers();

#ifndef DED_CORE_NO_BLE
  releaseBleMemory();

  // Возвращаем куче резерв контроллера Bluetooth (56 КБ). Без него после старта HTTPS
  // остаётся ~54 КБ и второе TLS-соединение уже не поднимается — страница не может
  // забрать статику, пока открыт wss. Bluetooth после этого вернётся только
  // перезагрузкой, поэтому двигатель должен быть заглушен.
  if (RPM == 0) {
    purgeBleMemory();
  } else {
    Serial.println("TLS: engine is running, BLE memory kept — only one TLS session will fit");
  }
#endif

  if (!startTlsServer()) {
    Serial.println("TLS: HTTPS start failed, restoring Bluetooth");
#ifndef DED_CORE_NO_BLE
    restoreBleMemory();
#endif
    startPlainWebServers();
    tlsModeArmProbe();
    return;
  }

  tlsIdleSince = millis();
}

static void tlsModeToBle() {
#ifndef DED_CORE_NO_BLE
  // Резерв контроллера уже в куче: поднять Bluetooth без перезагрузки нельзя.
  // Перезагрузку делаем только на заглушенном двигателе — иначе оборвём зажигание.
  if (bleMemoryReleased()) {
    if (RPM != 0) {
      Serial.println("TLS: engine is running, staying in HTTPS mode");
      tlsIdleSince = millis();
      return;
    }

    Serial.println("TLS: rebooting to restore Bluetooth");
    delay(200);
    ESP.restart();
  }
#endif

  if (tlsIsRunning()) {
    stopTlsServer();
    Serial.println("TLS: HTTPS stopped, restoring Bluetooth");
  }

  startPlainWebServers();

#ifndef DED_CORE_NO_BLE
  restoreBleMemory();
#endif

  tlsIdleSince = 0;
  tlsModeArmProbe();
}

void tlsModeHandle() {
  if (tlsHttpsRequested) {
    tlsHttpsRequested = false;
    tlsModeToHttps();
  }

  if (tlsBleRequested) {
    tlsBleRequested = false;
    tlsModeToBle();
    return;
  }

#ifdef DED_CORE_NO_BLE
  // Bluetooth в этой сборке нет, HTTPS — единственный режим. Сертификат может появиться
  // уже после старта (POST /api/tls), поэтому пробуем поднять сервер и здесь, но не чаще
  // раза в пять секунд: без файлов на флешке попытки бессмысленны.
  if (!tlsIsRunning() && !tlsRestartPending) {
    static unsigned long retriedAt = 0;

    if (retriedAt == 0 || millis() - retriedAt >= 5000UL) {
      retriedAt = millis();
      startTlsServer();
    }
  }

  handleTlsServer();
  return;
#endif

  // Новый сертификат при не поднятом HTTPS применять некуда: файлы и NVS уже записаны,
  // сервер прочитает их при следующем включении HTTPS. Снимаем отметку, иначе она
  // останется висеть до конца работы.
  if (tlsRestartPending && !tlsIsRunning()) {
    tlsRestartPending = false;
  }

  // Запрос «перезагрузиться и забрать сертификат» приходит именно в режиме Bluetooth:
  // рукопожатие с сервисом требует памяти, которой рядом с NimBLE нет. Поэтому
  // обслуживаем его до проверки «HTTPS не запущен».
  handleTlsServer();

  if (!tlsIsRunning()) {
    tlsModeArmProbe();
    return;
  }

  // Возврат к Bluetooth, когда HTTPS никто не держит: иначе WSS-поток оборвался бы.
  //
  // Список сокетов спрашиваем не чаще двух раз в секунду: httpd ведёт его сам и не
  // ждёт, пока мы дочитаем, — частый опрос из другой задачи лезет в его состояние
  // ровно в момент создания сессии.
  static unsigned long clientsCheckedAt = 0;
  static bool idleConfirmed = false;

  if (millis() - clientsCheckedAt >= 500UL) {
    clientsCheckedAt = millis();

    int clients[4];
    size_t count = 4;

    idleConfirmed = httpd_get_client_list(tlsServer, &count, clients) == ESP_OK && count == 0;
  }

  if (idleConfirmed) {
    if (tlsIdleSince == 0) {
      tlsIdleSince = millis();
    } else if (millis() - tlsIdleSince >= DED_CORE_TLS_IDLE_MS) {
      tlsModeToBle();
      return;
    }
  } else {
    tlsIdleSince = 0;
  }
}
