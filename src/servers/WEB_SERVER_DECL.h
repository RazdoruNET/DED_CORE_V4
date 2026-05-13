// WEB_SERVER_DECL.h - Web server function and class declarations
// This file should be included instead of WEB_SERVER.h to avoid conflicts

#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <SPIFFS.h>
#include "config/PROPS.h"
#include "core/ignition/carts/MOTO_CARTS.h"
#include "WEB_SERVER_VARS.h"

class CaptiveRequestHandler : public AsyncWebHandler {
public:
  CaptiveRequestHandler() {}
  virtual ~CaptiveRequestHandler() {}

  bool canHandle(AsyncWebServerRequest *request){
    return true;
  }

  void handleRequest(AsyncWebServerRequest *request){
    request->send(200, "text/plain", "Hello World!");
  }
};

// Function declarations
void initServer();
void initRecoveryServer();
void initServerRoutes();
void initRecoveryServerRoutes();
