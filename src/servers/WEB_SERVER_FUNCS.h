// WEB_SERVER_FUNCS.h - Web server function declarations
// This file should be included only once in the main compilation unit

#include "WEB_SERVER_VARS.h"
#include "DEVICE_API_ROUTES.h"
#include "core/net/MODULE_PAIRING.h"
#include "core/net/UPDATE_SERVER.h"
#include "core/migration/MIGRATION_SERVER.h"
#include "core/net/TLS_HTTP_ROUTES.h"

void initServerRoutes() {
  server.on("/", HTTP_GET, [] (AsyncWebServerRequest *request) {
    request->send(200, "text/plain", FIRMWARE_VERSION);
  });

  // /reinit убран: он выставлял INIT_SYSTEM, который никто не читает, и не
  // перезагружал блок. Перезагрузка — GET /core/reboot.
}

// Материал TLS лежит в том же разделе, что и вшитый фронт, поэтому раздача статики
// обязана его не отдавать: /key.pem — приватный ключ устройства, и с ним можно выдать
// себя за блок перед приложением.
//
// Обработчики регистрируются ДО раздачи статики: ESPAsyncWebServer перебирает их в
// порядке регистрации, и первый совпавший забирает запрос. Иначе wildcard-статика
// отдала бы файл как обычный — ровно так ключ и утекал.
void registerPrivateFsGuards() {
  static const char *privatePaths[] = {"/key.pem", "/cert.pem"};

  for (const char *path : privatePaths) {
    server.on(path, HTTP_GET, [] (AsyncWebServerRequest *request) {
      request->send(404, "text/plain", "Not found");
    });
    server.on(path, HTTP_POST, [] (AsyncWebServerRequest *request) {
      request->send(404, "text/plain", "Not found");
    });
  }
}

void initRecoveryServerRoutes() {
  // Recovery server routes
}


// Вшитый фронт лежит в файловой системе двумя копиями: обычной и .gz. Сжатая нужна
// потому, что 273 КБ JS через TLS занимают сессию на три секунды, а каждая сессия —
// это память рядом с Bluetooth. Браузеры, которые открывают PWA, gzip понимают всегда;
// обычная копия остаётся для curl и диагностики.
static const char *staticMimeType(const String &path) {
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

static bool clientAcceptsGzip(AsyncWebServerRequest *request) {
  if (!request->hasHeader("Accept-Encoding")) {
    return false;
  }

  return request->header("Accept-Encoding").indexOf("gzip") >= 0;
}

// Статика регистрируется ПОСЛЕДНЕЙ: обработчики API должны получать свои адреса
// раньше, а этот забирает всё остальное. Приватные файлы отсекаются раньше — см.
// registerPrivateFsGuards.
void registerStaticFiles(AsyncWebServer &srv) {
  srv.on("/*", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String path = request->url();

    if (path == "/" || path.length() == 0) {
      path = "/index.html";
    }

    if (path.indexOf("..") >= 0) {
      request->send(400, "text/plain", "Bad path");
      return;
    }

    String file = path;

    if (clientAcceptsGzip(request) && DEVICE_FS.exists(path + ".gz")) {
      file = path + ".gz";
    }

    if (!DEVICE_FS.exists(file)) {
      request->send(404, "text/plain", "Not found");
      return;
    }

    AsyncWebServerResponse *response =
        request->beginResponse(DEVICE_FS, file, staticMimeType(path));

    if (response == nullptr) {
      request->send(500, "text/plain", "Response failed");
      return;
    }

    if (file != path) {
      response->addHeader("Content-Encoding", "gzip");
    }

    // no-store: после заливки новой файловой системы браузер не должен открывать
    // index.html из кеша — иначе он попросит уже удалённый main.js и экран останется чёрным.
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
  });
}

void initServer() {
  delay(500);

  if (!initFileSystem()) {
    return;
  }

  registerPrivateFsGuards();

  initServerRoutes();
  initRecoveryServerRoutes();
  registerDeviceApiRoutes(&server);
  registerModulePairingRoutes(&server);
  registerUpdateRoutes(&server);
  registerMigrationRoutes(&server);
  registerTlsHttpRoutes(&server);

  // Последней: всё, что не забрали API-маршруты выше, отдаётся из файловой системы.
  registerStaticFiles(server);

  server.begin();
}

void initRecoveryServer() {
  delay(500);

  if (!initFileSystem()) {
    return;
  }

  registerPrivateFsGuards();

  initRecoveryServerRoutes();
  registerStaticFiles(server);

  server.begin();
}

// Обычные серверы (HTTP 80 и WebSocket 81) на время HTTPS останавливаются: их сокеты
// и буферы — это та память, которой не хватает TLS-рукопожатию рядом с Bluetooth.
static bool plainWebServersRunning = true;

void stopPlainWebServers() {
  if (!plainWebServersRunning) {
    return;
  }

  webSocket.close();
  server.end();
  plainWebServersRunning = false;
  Serial.printf("TLS: plain HTTP/WS stopped, free=%u maxAlloc=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

void startPlainWebServers() {
  if (plainWebServersRunning) {
    return;
  }

  server.begin();
  webSocket.begin();
  plainWebServersRunning = true;
  Serial.printf("TLS: plain HTTP/WS started, free=%u maxAlloc=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

// Обёртки для HTTPS-сервера (core/net/TLS_SERVER.cpp): он живёт в отдельной единице
// трансляции и не может включать MODULE_PAIRING.h из-за несовместимых HTTP_*.
void dedModuleForget() {
  module_pairing::forgetModules();
}

String dedModulePairStart() {
  module_pairing::requestOpen();
  return module_pairing::stateJson();
}

String dedModulePairStop() {
  module_pairing::requestClose();
  return module_pairing::stateJson();
}
