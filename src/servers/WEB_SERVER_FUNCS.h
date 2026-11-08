// WEB_SERVER_FUNCS.h - Web server function declarations
// This file should be included only once in the main compilation unit

#include "WEB_SERVER_VARS.h"
#include "DEVICE_API_ROUTES.h"
#include "core/net/UPDATE_SERVER.h"
#include "core/migration/MIGRATION_SERVER.h"
#include "core/net/TLS_HTTP_ROUTES.h"

void initServerRoutes() {
  server.on("/", HTTP_GET, [] (AsyncWebServerRequest *request) {
    request->send(200, "text/plain", FIRMWARE_VERSION);
  });

  server.on("/reinit", HTTP_GET, [] (AsyncWebServerRequest *request) {
    INIT_SYSTEM  = true;
    request->send(200, "text/plain", FIRMWARE_VERSION);
  });
}

// Материал TLS лежит в том же разделе, что и вшитый фронт, поэтому раздача статики
// обязана его не отдавать: /key.pem — приватный ключ устройства, и с ним можно выдать
// себя за блок перед приложением.
//
// Обработчики регистрируются ДО serveStatic: ESPAsyncWebServer перебирает их в порядке
// регистрации, и первый совпавший забирает запрос. Иначе wildcard-статика отдала бы
// файл как обычный — ровно так ключ и утекал.
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

void initServer() {
  delay(500);

  if (!initFileSystem()) {
    return;
  }

  registerPrivateFsGuards();

  // no-store: после заливки новой файловой системы браузер не должен открывать
  // index.html из кеша — иначе он попросит уже удалённый main.js и экран останется чёрным.
  server.serveStatic("/", DEVICE_FS, "/").setDefaultFile("index.html").setCacheControl("no-store");

  initServerRoutes();
  initRecoveryServerRoutes();
  registerDeviceApiRoutes(&server);
  registerUpdateRoutes(&server);
  registerMigrationRoutes(&server);
  registerTlsHttpRoutes(&server);

  server.begin();
}

void initRecoveryServer() {
  delay(500);

  if (!initFileSystem()) {
    return;
  }

  registerPrivateFsGuards();

  // no-store: после заливки новой файловой системы браузер не должен открывать
  // index.html из кеша — иначе он попросит уже удалённый main.js и экран останется чёрным.
  server.serveStatic("/", DEVICE_FS, "/").setDefaultFile("index.html").setCacheControl("no-store");

  initRecoveryServerRoutes();
  
  server.begin();
}
