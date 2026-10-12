// HTTPS/WSS сервер устройства (IDF httpd, esp_https_server).
//
// Зачем: PWA открывается по HTTPS, а браузер запрещает ws:// со страницы HTTPS (mixed content).
// Значит блок должен отдавать wss://, а для этого нужен сертификат, которому доверяет браузер.
//
// Сертификат выпускаем МЫ (Let's Encrypt DNS-01 на core.dedbox.pro) и кладём на устройство
// файлами /cert.pem и /key.pem в SPIFFS. В офлайне DNS/интернет не нужны: имя резолвит AP-DNS
// устройства (initApDns в servers/WIFI_SERVER.h), доверие проверяет браузер телефона.
//
// Плейн-HTTP (80) и WS (81) остаются fallback-ом для уже выпущенных фронтов.
//
// Реализация в отдельном .cpp: заголовки IDF (esp_http_server.h) и ESPAsyncWebServer
// объявляют несовместимые перечисления HTTP_*, в одной единице трансляции они не уживаются.

#include <Arduino.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "esp_https_server.h"
#include "esp_http_server.h"
#include <time.h>
#include "config/PROPS.h"
#include "core/ota/HYBRID_OTA.h"
#include "core/net/TLS_SERVER.h"
#include "core/net/SERVICE_CLIENT.h"

// Общий протокол с WS на 81 — определения в WEB_SOCKET_FUNCS.h (в этой единице трансляции их нет).
String getDataJson();
bool handleClientCommand(const String &dataStr, String &out);

extern Preferences preferences;
extern HybridOTAManager otaManager;

// Определения в servers/WEB_SOCKET_FUNCS.h — протокол общий с WS на 81.
String getDataJson();
bool handleClientCommand(const String &dataStr, String &out);

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
static String tlsCertPem;
static String tlsKeyPem;
static bool tlsRestartPending = false;
static unsigned long tlsLastBroadcast = 0;

bool tlsIsRunning() {
  return tlsServer != nullptr;
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

void tlsForgetClient(int fd) {
  for (int i = 0; i < DED_CORE_TLS_MAX_CLIENTS; i++) {
    if (tlsClients[i] == fd) {
      tlsClients[i] = -1;
    }
  }
}

esp_err_t tlsWsHandler(httpd_req_t *req) {
  int fd = httpd_req_to_sockfd(req);

  if (req->method == HTTP_GET) {
    // Рукопожатие уже сделал httpd — запоминаем клиента для рассылки телеметрии.
    tlsTrackClient(fd);
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

  String response;
  if (handleClientCommand(payloadStr, response)) {
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

  File file = SPIFFS.open(path, "r");
  if (!file || file.isDirectory()) {
    return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
  }

  httpd_resp_set_type(req, tlsMimeType(path));

  // Файлы фронта крупные (main.js ~850 КБ) — отдаём потоком, не поднимая в RAM целиком.
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

  return deviceKey == String(header);
}

// Приём сертификата:
//   {"cert":"-----BEGIN CERTIFICATE-----...","key":"-----BEGIN PRIVATE KEY-----...","notAfter":1790000000}
// notAfter (время окончания в секундах UNIX) необязателен, но с ним устройство само
// понимает, что сертификат пора менять, и успевает это сделать до истечения.
// Заголовок X-Device-Key обязателен: иначе сертификат мог бы подменить любой из той же Wi-Fi сети.
bool applyTlsMaterial(const String &body, String &error) {
  // Serial-провижининг может сработать раньше, чем задача сервера смонтирует SPIFFS.
  if (!SPIFFS.begin(true)) {
    error = "SPIFFS not available";
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

  File certFile = SPIFFS.open(DED_CORE_TLS_CERT_FILE, "w");
  if (!certFile) {
    error = "cannot write cert.pem";
    return false;
  }
  certFile.print(cert);
  certFile.close();

  File keyFile = SPIFFS.open(DED_CORE_TLS_KEY_FILE, "w");
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

  preferences.putUInt(DED_CORE_TLS_EXPIRY_KEY, notAfter);

  tlsRestartPending = true;
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
  StaticJsonBuffer<512> jsonBuffer;
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

  String body = "";
  json.printTo(body);

  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, body.c_str(), HTTPD_RESP_USE_STRLEN);
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

  int daysLeft = tlsCertificateDaysLeft();
  bool needRefresh = !tlsIsRunning() || !SPIFFS.exists(DED_CORE_TLS_CERT_FILE) ||
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

void tlsBroadcastWork(void *arg) {
  if (tlsServer == nullptr) {
    return;
  }

  String json = getDataJson();

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

bool tlsReadFile(const char *path, String &out) {
  File file = SPIFFS.open(path, "r");
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

  if (!SPIFFS.begin(true)) {
    Serial.println("TLS: SPIFFS not available");
    return false;
  }

  if (!tlsReadFile(DED_CORE_TLS_CERT_FILE, tlsCertPem) || !tlsReadFile(DED_CORE_TLS_KEY_FILE, tlsKeyPem)) {
    Serial.println("TLS: cert.pem/key.pem not found in SPIFFS — HTTPS disabled");
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

