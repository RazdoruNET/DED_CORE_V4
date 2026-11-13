// HTTPS/WSS сервер устройства (IDF httpd, esp_https_server).
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
#include "esp_https_server.h"
#include "esp_http_server.h"
#include <time.h>
#include "config/PROPS.h"
#include "core/ota/HYBRID_OTA.h"
#include "core/net/TLS_SERVER.h"
#include "core/net/AI_GRANT.h"
#include "core/net/SERVICE_CLIENT.h"

// Общий протокол с WS на 81 — определения в WEB_SOCKET_FUNCS.h (в этой единице трансляции их нет).
String getDataJson();
bool handleClientCommand(const String &dataStr, String &out, bool authorized);

extern Preferences preferences;
extern HybridOTAManager otaManager;

// Домен сертификата; AP-DNS резолвит его на адрес устройства.
#define DED_CORE_TLS_CERT_FILE    "/cert.pem"
#define DED_CORE_TLS_KEY_FILE     "/key.pem"
#define DED_CORE_TLS_MAX_CLIENTS  4
#define DED_CORE_TLS_BROADCAST_MS 200
#define DED_CORE_TLS_MAX_UPLOAD   8192

// Стек http-задачи: TLS-рукопожатие + StaticJsonBuffer телеметрии (~5 КБ).
#define DED_CORE_TLS_STACK 14336

static httpd_handle_t tlsServer = nullptr;
static int tlsClients[DED_CORE_TLS_MAX_CLIENTS] = { -1, -1, -1, -1 };
// Клиент подтвердил ключ устройства кадром auth. TLS-канал шифрует трафик,
// поэтому ключ можно принимать и из домашней сети, а не только с точки доступа.
static bool tlsAuthorized[DED_CORE_TLS_MAX_CLIENTS] = { false, false, false, false };
static String tlsCertPem;
static String tlsKeyPem;
static bool tlsRestartPending = false;
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

  File file = DEVICE_FS.open(path, "r");
  if (!file || file.isDirectory()) {
    return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
  }

  httpd_resp_set_type(req, tlsMimeType(path));
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");

  // Фронт отдаём потоком, не поднимая файл целиком в RAM.
  char chunk[1024];
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

  File certFile = DEVICE_FS.open(DED_CORE_TLS_CERT_FILE, "w");
  if (!certFile) {
    error = "cannot write cert.pem";
    return false;
  }
  certFile.print(cert);
  certFile.close();

  File keyFile = DEVICE_FS.open(DED_CORE_TLS_KEY_FILE, "w");
  if (!keyFile) {
    error = "cannot write key.pem";
    return false;
  }
  keyFile.print(key);
  keyFile.close();

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
  json["cert"] = tlsCertPem.length() > 0;
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
    error = "service returned " + String(code);
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
    tlsCertCachePresent = DEVICE_FS.exists(DED_CORE_TLS_CERT_FILE);
  }

  int daysLeft = tlsCertCacheDays;
  bool needRefresh = !tlsIsRunning() || !tlsCertCachePresent ||
                     (daysLeft >= 0 && daysLeft < DED_CORE_TLS_RENEW_DAYS);

  if (!needRefresh) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  // Не долбим сервис: раз в час достаточно.
  if (lastAttempt != 0 && millis() - lastAttempt < 3600000UL) {
    return;
  }

  lastAttempt = millis();

  String error;
  if (!tlsFetchFromService(error)) {
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
  if (tlsServer == nullptr) {
    return;
  }

  String *payload = new String(json);
  if (httpd_queue_work(tlsServer, tlsBroadcastFrameWork, payload) != ESP_OK) {
    delete payload;
  }
}

void tlsBroadcastWork(void *arg) {
  if (tlsServer == nullptr) {
    return;
  }

  tlsSendFrameToClients(getDataJson());
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

void stopTlsServer() {
  if (tlsServer == nullptr) {
    return;
  }

  httpd_ssl_stop(tlsServer);
  tlsServer = nullptr;

  for (int i = 0; i < DED_CORE_TLS_MAX_CLIENTS; i++) {
    tlsClients[i] = -1;
  }
}

// true — HTTPS/WSS поднялся. Без сертификата устройство остаётся на HTTP (80) + WS (81).
bool startTlsServer() {
  stopTlsServer();

  if (!DEVICE_FS.begin(true)) {
    Serial.println("TLS: filesystem is not available");
    return false;
  }

  if (!tlsReadFile(DED_CORE_TLS_CERT_FILE, tlsCertPem) || !tlsReadFile(DED_CORE_TLS_KEY_FILE, tlsKeyPem)) {
    Serial.println("TLS: cert.pem/key.pem not found — HTTPS disabled");
    return false;
  }

  httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
  config.cacert_pem = (const uint8_t *)tlsCertPem.c_str();
  config.cacert_len = tlsCertPem.length() + 1;
  config.prvtkey_pem = (const uint8_t *)tlsKeyPem.c_str();
  config.prvtkey_len = tlsKeyPem.length() + 1;
  config.transport_mode = HTTPD_SSL_TRANSPORT_SECURE;
  config.port_secure = DED_CORE_TLS_PORT;
  config.httpd.stack_size = DED_CORE_TLS_STACK;
  config.httpd.max_uri_handlers = 5;
  config.httpd.uri_match_fn = httpd_uri_match_wildcard;

  if (httpd_ssl_start(&tlsServer, &config) != ESP_OK) {
    tlsServer = nullptr;
    Serial.println("TLS: httpd_ssl_start failed");
    return false;
  }

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

  httpd_uri_t staticUri = {};
  staticUri.uri = "/*";
  staticUri.method = HTTP_GET;
  staticUri.handler = tlsStaticHandler;
  httpd_register_uri_handler(tlsServer, &staticUri);

  Serial.printf("TLS: https/wss on port %d (%s)\n", DED_CORE_TLS_PORT, DED_CORE_TLS_HOST);
  return true;
}

// Вызывается из задачи сервера: рассылка телеметрии и отложенный перезапуск после смены сертификата.
// Перезапуск делаем здесь, а не в обработчике запроса: нельзя останавливать httpd из его же задачи.
void handleTlsServer() {
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

