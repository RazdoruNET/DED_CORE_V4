// WEB_SERVER_CLEAN.h - Clean web server declarations only
// This file replaces the corrupted WEB_SERVER.h

#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include "config/PROPS.h"
#include "core/ignition/carts/MOTO_CARTS.h"

// External variable declarations
extern AsyncWebServer server;
extern DNSServer dnsServer;

// Function declarations only - no implementations
void initServer();
void initRecoveryServer();
void initServerRoutes();
void initRecoveryServerRoutes();
