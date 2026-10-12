// WEB_SOCKET_FUNCS.h - WebSocket function implementations
// This file should be included to get WebSocket function definitions
// Протокол: телеметрия {"event":"message","data":{...}} рассылается всем клиентам,
// команды {"event":"ping"} -> {"event":"pong"} и {"event":"cart","data":{"cart":N}}.
// Тот же контракт используется в PWA (frontend/src/app/ded-core-control) и по BLE (core/ble/BLE_SERVER.h).

#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "config/PROPS.h"
#include "core/ota/HYBRID_OTA.h"
#include "core/net/DEVICE_COMMANDS.h"

// Use external variables from WEB_SOCKET_VARS.h
extern WebSocketsServer webSocket;
extern int socet_flash;
extern int pass;
extern int userIndex;
extern int modul;
extern Preferences preferences;
extern HybridOTAManager otaManager;

// Forward declaration
void onEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);

void initWebSocket() {
  webSocket.begin();
  webSocket.onEvent(onEvent);
}

// Обработка команды клиента (WS на 81 и HTTPS/WSS используют её совместно).
// Возвращает true, если нужен ответ (например, pong) — он кладётся в out.
// Команды разбирает общий обработчик: тот же JSON приходит по Bluetooth и по wss на 443.
bool handleClientCommand(const String &dataStr, String &out) {
  return handleDeviceCommand(dataStr, out);
}

void onEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.println("WebSocket disconnected");
      break;
    case WStype_CONNECTED:
      Serial.println("WebSocket connected");
      break;
    case WStype_BIN:
      Serial.println("WebSocket binary request");
      break;
    case WStype_TEXT:
    {
      // Встроенный фронт шлёт полезную нагрузку с экранированными скобками.
      String dataStr = String((char*)payload);
      dataStr.replace("\\", "");
      dataStr.replace("}\"", "}");
      dataStr.replace("\"{", "{");

      String response;
      if (handleClientCommand(dataStr, response)) {
        webSocket.sendTXT(num, response);
      }

      break;
    }
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
  data["select_qs"] = select_qs;
  data["aa"] = ANGLE_ADVANCE;
  data["qs"] = QUICKSHIFTER;
  data["cc"] = CARTS_SWHITCHING;
  data["modulator"] = modul;
  data["max_rpm"] = max_rpm;
  data["max_rpm_angle_kiker"] = max_rpm_angle_kiker;
  data["advance_angle"] = Advance_angle;
  data["rpm"] = RPM;
  data["termo"] = Termo;
  data["throttle"] = Throttle;
  data["init_angle"] = Init_angle;
  data["filter_val"] = filter;
  data["high"] = (HIGH_OUTPUT) ? 1 : 0;
  data["init_flash_time"] = Init_flash_time;
  data["cpu_termo"] = (temprature_sens_read() - 32) / 1.8;
  data["card"] = active_cart;
  data["wsid"] = APSSID1;
  data["wpass"] = APPSK1;
  data["INVERT_SENSOR"] = INVERT_SENSOR;
  data["INVERT_THROTTLE"] = INVERT_THROTTLE;
  data["servo"] = SERVO;
  data["servo_position"] = (SERVO) ? Servo_position : 0;
  data["servo_type"] = Servo_type;
  data["servo_rpm"] = Servo_rpm;
  data["servo_percent"] = Servo_percent;
  data["type"] = type;
  data["wifi_auto_update_enabled"] = WIFI_AUTO_UPDATE_ENABLED ? 1 : 0;
  data["wifi_update_ssid"] = WIFI_UPDATE_SSID;
  data["wifi_update_status"] = WIFI_UPDATE_STATUS;
  data["wifi_last_check_time"] = WIFI_LAST_CHECK_TIME;
  data["wifi_last_attempt_time"] = WIFI_LAST_ATTEMPT_TIME;
  data["wifi_connected"] = (WiFi.status() == WL_CONNECTED && WiFi.SSID() == WIFI_UPDATE_SSID) ? 1 : 0;
  data["wifi_password_set"] = (WIFI_UPDATE_PASSWORD.length() > 0) ? 1 : 0;
  data["firmware_version"] = FIRMWARE_VERSION;
  data["ota_update_available"] = otaManager.getLastFirmwareInfo().updateAvailable ? 1 : 0;
  data["ota_last_version"] = otaManager.getLastFirmwareInfo().version;
  data["motor_running"] = (RPM > 0) ? 1 : 0;

  JsonArray& rpm_array = jsonBuffer.createArray();

  int count = (type == 0) ? 7 : (type == 1) ? 12 : 22;

  for (byte i = 0; i < count; i++) {
    JsonArray& rpm_item = jsonBuffer.createArray();

    rpm_item.add(rpmsArr[active_cart][i]);
    rpm_item.add(anglesArr[active_cart][i]);
    rpm_item.add(angles_adjustmentArr[active_cart][i]);
    rpm_item.add(quick_adjustmentArr[active_cart][i]);
    rpm_item.add(servo_valueArr[active_cart][i]);

    rpm_array.add(rpm_item);
  }

  data["angles"] = rpm_array;

  json["data"] = data;
  
  json.printTo(body);
  return body;
}

void send_data() {
  String json = getDataJson();

  for (int i = 0; i <= 100; i++) {
    webSocket.sendTXT(i, json);
  }

  modul = 0;
}

void handleWebSocket() {
  webSocket.loop();
}

void handelWebSocketData() {
  if (Modulator > 0) {
    modul = Modulator;
    socet_flash = Modulator;
  }

  if (RPM > 0) {
    modul = 1;
  }

  // Ограничитель частоты рассылки: вызывается из задачи сервера.
  if (pass > 150) {
    send_data();
    socet_flash = 0;
    pass = 0;
  } else {
    pass++;
  }
}

void handleServer() {
  handelWebSocketData();
  handleWebSocket();
}
