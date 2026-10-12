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

void initRecoveryServerRoutes() {
  // Recovery server routes
}

void initServer() {
  delay(500);

  if (!initFileSystem()) {
    return;
  }

  server.serveStatic("/", DEVICE_FS, "/").setDefaultFile("index.html");

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

  server.serveStatic("/", DEVICE_FS, "/").setDefaultFile("index.html");

  initRecoveryServerRoutes();
  
  server.begin();
}
