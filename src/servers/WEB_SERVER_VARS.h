// WEB_SERVER_VARS.h - Web server variable declarations
// This file should be included only once in the main compilation unit

#include <DNSServer.h>
#include <ESPAsyncWebServer.h>

AsyncWebServer server(80);
DNSServer dnsServer;
