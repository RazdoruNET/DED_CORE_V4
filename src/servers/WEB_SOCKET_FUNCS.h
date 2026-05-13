// WEB_SOCKET_FUNCS.h - WebSocket function implementations
// This file should be included to get WebSocket function definitions

#include <WebSocketsServer.h>
#include "config/PROPS.h"
#include <ArduinoJson.h>

// Use external variables from WEB_SOCKET_VARS.h
extern WebSocketsServer webSocket;
extern int socet_flash;
extern int pass;
extern int userIndex;
extern int modul;

// Forward declaration
void onEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);

void initWebSocket() {
  webSocket.begin();
  webSocket.onEvent(onEvent);
}

void handleWebSocket() {
  webSocket.loop();
}

void handelWebSocketData() {
  // WebSocket data handling logic
}

void onEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.println("WebSocket disconnected");
      break;
    case WStype_CONNECTED:
      Serial.println("WebSocket connected");
      break;
    case WStype_TEXT:
      // Handle text data
      break;
    case WStype_BIN:
      Serial.println("WebSocket binary request");
      break;
  }
}

String getDataJson() {
  String body = "";
  StaticJsonBuffer<5048> jsonBuffer;
  
  JsonObject& json = jsonBuffer.createObject();
  JsonObject& data = jsonBuffer.createObject();
  
  json["event"] = "message";
  data["micros"] = micros();
  data["select_cart"] = select_cart;
  data["rpm"] = RPM;
  data["termo"] = Termo;
  data["throttle"] = Throttle;
  
  json.printTo(body);
  return body;
}

void send_data() {
  String json = getDataJson();
  webSocket.sendTXT(0, json);
}
