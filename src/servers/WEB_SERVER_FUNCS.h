// WEB_SERVER_FUNCS.h - Web server function declarations
// This file should be included only once in the main compilation unit

#include "WEB_SERVER_VARS.h"

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
  SPIFFS.begin(true);
  delay(500);
  
  if(!SPIFFS.begin()){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
  
  initServerRoutes();
  initRecoveryServerRoutes();
  
  server.begin();
}

void initRecoveryServer() {
  SPIFFS.begin(true);
  delay(500);
  
  if(!SPIFFS.begin()){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
  
  initRecoveryServerRoutes();
  
  server.begin();
}
