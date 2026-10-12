// WEB_SERVER_CLEAN.h - объявления веб-сервера и его серверов.
// Определения — в WEB_SERVER_FUNCS.h, маршруты API устройства — в DEVICE_API_ROUTES.h.

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
