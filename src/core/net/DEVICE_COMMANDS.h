#ifndef DEVICE_COMMANDS_H
#define DEVICE_COMMANDS_H

// Команды владельца: один и тот же JSON приходит и по WebSocket (81 и wss 443), и по
// Bluetooth. Обработчик общий, отличаются только каналы ответа: WebSocket отдаёт строку
// запросившему клиенту, Bluetooth — уведомлением.
//
// Сканирование сетей и подключение к роутеру выполняет задача OTA (см. core/tasks/OtaTask.h):
// они занимают секунды, и в обработчике Bluetooth это остановило бы стек.

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "config/PROPS.h"
#include "core/ota/HYBRID_OTA.h"

extern Preferences preferences;
extern HybridOTAManager otaManager;
extern int active_cart;
extern int select_cart;

// Сколько сетей отдаём и как режем имя: кадр должен уложиться в одну BLE-нотификацию
// (согласованный MTU 255 → 252 байта полезной нагрузки).
#define WIFI_SCAN_MAX_NETWORKS 6
#define WIFI_SCAN_SSID_MAX 24

// Кадр в приложение по всем открытым каналам (WS 81, wss 443, Bluetooth).
void sendDeviceFrame(const String &frame);

// Строку из JSON копируем сразу: буфер разбора живёт только до конца обработчика.
String jsonString(JsonObject &data, const char *key) {
  const char *value = data[key];
  return value == nullptr ? String("") : String(value);
}

// Снимок подключения к домашней сети: имена полей совпадают с кадром телеметрии,
// поэтому приложение разбирает его тем же кодом.
String wifiStateJson() {
  bool connected = (WiFi.status() == WL_CONNECTED && WiFi.SSID() == WIFI_UPDATE_SSID);

  String json = "{";
  json += "\"wifi_auto_update_enabled\":" + String(WIFI_AUTO_UPDATE_ENABLED ? 1 : 0) + ",";
  json += "\"wifi_update_ssid\":\"" + WIFI_UPDATE_SSID + "\",";
  json += "\"wifi_update_status\":\"" + WIFI_UPDATE_STATUS + "\",";
  json += "\"wifi_password_set\":" + String(WIFI_UPDATE_PASSWORD.length() > 0 ? 1 : 0) + ",";
  json += "\"wifi_connected\":" + String(connected ? 1 : 0) + ",";
  json += "\"ota_update_available\":" + String(otaManager.getLastFirmwareInfo().updateAvailable ? 1 : 0) + ",";
  json += "\"ota_last_version\":\"" + otaManager.getLastFirmwareInfo().version + "\",";
  json += "\"firmware_version\":\"" + String(FIRMWARE_VERSION) + "\"";
  json += "}";
  return json;
}

// Настройки домашней сети: ssid и pass пишем, только если они переданы; enabled: 0/1 —
// включение автообновления, -1 — «не менять».
//
// Новые настройки означают и новую попытку подключения: обнуляем время прошлой, иначе
// задача OTA ждала бы десятиминутный интервал повторов.
bool applyWifiSettings(const String &ssid, const String &pass, int enabled) {
  bool changed = false;

  if (ssid.length() > 0 && ssid != WIFI_UPDATE_SSID) {
    WIFI_UPDATE_SSID = ssid;
    preferences.putString("WIFI_UPDATE_SSID", WIFI_UPDATE_SSID);
    changed = true;
  }

  if (pass.length() > 0) {
    WIFI_UPDATE_PASSWORD = pass;
    preferences.putString("WIFI_UPDATE_PASSWORD", WIFI_UPDATE_PASSWORD);
    changed = true;
  }

  if (enabled >= 0 && (enabled == 1) != WIFI_AUTO_UPDATE_ENABLED) {
    WIFI_AUTO_UPDATE_ENABLED = (enabled == 1);
    preferences.putBool("WIFI_AUTO_UPDATE_ENABLED", WIFI_AUTO_UPDATE_ENABLED);
    changed = true;
  }

  if (changed) {
    WIFI_LAST_ATTEMPT_TIME = 0;
    preferences.putULong("WIFI_LAST_ATTEMPT_TIME", 0);
    WIFI_UPDATE_STATUS = "idle";

    Serial.print("WiFi settings saved: ssid=");
    Serial.print(WIFI_UPDATE_SSID);
    Serial.print(" auto_update=");
    Serial.println(WIFI_AUTO_UPDATE_ENABLED ? "on" : "off");
  }

  return changed;
}

// Имя сети из сканирования попадает в JSON как есть, а кавычка или обратный слэш сломали бы
// кадр: заменяем их и режем длину. Обрезку ведём по границе UTF-8, чтобы имя осталось читаемым.
String sanitizeSsid(const String &ssid) {
  String clean = "";

  for (size_t i = 0; i < ssid.length(); i++) {
    char c = ssid[i];
    if (c == '"' || c == '\\') {
      clean += '\'';
    } else if (c >= 0x20) {
      clean += c;
    }
  }

  if (clean.length() <= WIFI_SCAN_SSID_MAX) {
    return clean;
  }

  size_t cut = WIFI_SCAN_SSID_MAX;
  while (cut > 0 && ((uint8_t)clean[cut] & 0xC0) == 0x80) {
    cut--;
  }

  return clean.substring(0, cut);
}

// Сети вокруг: отдаём шесть сильнейших, приложение показывает их списком.
String buildWifiNetworksJson() {
  int found = WiFi.scanNetworks(false, true, false, 300);

  String json = "{\"event\":\"wifi_networks\",\"data\":[";

  if (found > 0) {
    bool used[64] = {false};
    int limit = found > 64 ? 64 : found;

    for (int slot = 0; slot < WIFI_SCAN_MAX_NETWORKS; slot++) {
      int best = -1;

      for (int i = 0; i < limit; i++) {
        if (!used[i] && (best < 0 || WiFi.RSSI(i) > WiFi.RSSI(best))) {
          best = i;
        }
      }

      if (best < 0) {
        break;
      }

      used[best] = true;

      if (slot > 0) {
        json += ",";
      }

      json += "[\"" + sanitizeSsid(WiFi.SSID(best)) + "\",";
      json += String(WiFi.RSSI(best)) + ",";
      json += String(WiFi.encryptionType(best) != WIFI_AUTH_OPEN ? 1 : 0) + "]";
    }

    WiFi.scanDelete();
  }

  json += "]}";
  return json;
}

// Разбор команды. true — есть ответ: WebSocket отправляет строку запросившему клиенту,
// Bluetooth — уведомлением.
bool handleDeviceCommand(const String &dataStr, String &out) {
  StaticJsonBuffer<512> jsonBuffer;
  JsonObject &json = jsonBuffer.parseObject(dataStr);

  if (!json.success()) {
    Serial.println(dataStr);
    return false;
  }

  if (json["event"] == "cart") {
    int cart = json["data"]["cart"] | 0;

    if (cart > 0 && cart <= 3) {
      active_cart = (cart - 1);
      select_cart = cart;
      preferences.putInt("active_cart", active_cart);

      Serial.print("active_cart ");
      Serial.println(active_cart);
    }

    return false;
  }

  if (json["event"] == "servo_error") {
    Servo_error = 1;
    return false;
  }

  if (json["event"] == "ping") {
    out = "{\"event\":\"pong\"}";
    return true;
  }

  if (json["event"] == "wifi") {
    JsonObject &data = json["data"];
    applyWifiSettings(jsonString(data, "ssid"), jsonString(data, "pass"), data["enabled"] | -1);

    out = "{\"event\":\"wifi\",\"data\":" + wifiStateJson() + "}";
    return true;
  }

  if (json["event"] == "wifi_scan") {
    // Сканирует задача OTA: список придёт отдельным кадром, иначе канал встал бы на секунды.
    WIFI_SCAN_REQUESTED = true;

    out = "{\"event\":\"wifi_scan\",\"data\":{\"started\":true}}";
    return true;
  }

  return false;
}

#endif // DEVICE_COMMANDS_H
