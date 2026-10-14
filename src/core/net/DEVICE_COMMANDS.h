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
// Определения — в core/ignition/Variables.h; заголовок подключается и раньше него.
extern int active_cart;
extern int select_cart;
extern int Servo_error;

// Сколько сетей отдаём и как режем имя: кадр должен уложиться в одну BLE-нотификацию
// (согласованный MTU 255 → 252 байта полезной нагрузки).
#define WIFI_SCAN_MAX_NETWORKS 6
#define WIFI_SCAN_SSID_MAX 24

// Точек в карте зажигания: массивы в прошивке фиксированной длины, а рабочее число
// точек зависит от типа двигателя (7 / 12 / 22, см. core/ignition/Ignition.h).
#define MAP_POINTS_MAX 22

// Кадр в приложение по всем открытым каналам (WS 81, wss 443, Bluetooth).
void sendDeviceFrame(const String &frame);

// Рукопожатию TLS нужно около 33 КБ одним куском (буферы mbedTLS), а стек Bluetooth держит
// десятки килобайт. Перед обращением к сервису эти вызовы отдают память Bluetooth и
// возвращают её обратно (реализация — core/ble/BLE_SERVER.h, там же и сам стек).
// true — Bluetooth остановлен и память освобождена.
bool releaseBleMemory();
void restoreBleMemory();

// Подключено ли сейчас приложение по Bluetooth: во время живой сессии память у него
// не отбираем, иначе связь с владельцем пропадёт прямо во время работы.
bool bleClientConnected();

// Строку из JSON копируем сразу: буфер разбора живёт только до конца обработчика.
String jsonString(JsonObject &data, const char *key) {
  const char *value = data[key];
  return value == nullptr ? String("") : String(value);
}

// Снимок подключения к домашней сети. Имена полей совпадают с кадром телеметрии,
// поэтому приложение разбирает его тем же кодом.
//
// Состояние отдаём двумя кадрами, а не одним: уведомление Bluetooth ограничено
// согласованным MTU (255 → 252 байта), и один кадр со всеми полями выходил за предел
// на длинном имени сети — приложение не видело состояние вовсе.
String wifiStateJson() {
  bool connected = (WiFi.status() == WL_CONNECTED && WiFi.SSID() == WIFI_UPDATE_SSID);

  String json = "{";
  json += "\"wifi_auto_update_enabled\":" + String(WIFI_AUTO_UPDATE_ENABLED ? 1 : 0) + ",";
  json += "\"wifi_update_ssid\":\"" + WIFI_UPDATE_SSID + "\",";
  json += "\"wifi_update_status\":\"" + WIFI_UPDATE_STATUS + "\",";
  json += "\"wifi_password_set\":" + String(WIFI_UPDATE_PASSWORD.length() > 0 ? 1 : 0) + ",";
  json += "\"wifi_connected\":" + String(connected ? 1 : 0);
  json += "}";
  return json;
}

// Вторая половина: что предлагает сервис и что стоит на блоке.
String deviceInfoJson() {
  String json = "{";
  json += "\"ota_update_available\":" + String(otaManager.getLastFirmwareInfo().updateAvailable ? 1 : 0) + ",";
  json += "\"ota_last_version\":\"" + otaManager.getLastFirmwareInfo().version + "\",";
  json += "\"firmware_version\":\"" + String(FIRMWARE_VERSION) + "\"";
  json += ",\"diag_wifi_enabled\":" + String(DIAGNOSTICS_WIFI_ENABLED ? 1 : 0);
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
    preferences.putString("wifi_ssid", WIFI_UPDATE_SSID);
    changed = true;
  }

  if (pass.length() > 0) {
    WIFI_UPDATE_PASSWORD = pass;
    preferences.putString("wifi_pass", WIFI_UPDATE_PASSWORD);
    changed = true;
  }

  if (enabled >= 0 && (enabled == 1) != WIFI_AUTO_UPDATE_ENABLED) {
    WIFI_AUTO_UPDATE_ENABLED = (enabled == 1);
    preferences.putBool("wifi_auto", WIFI_AUTO_UPDATE_ENABLED);
    changed = true;
  }

  if (changed) {
    // Новые настройки — новая попытка: не ждём интервал повторов.
    WIFI_LAST_ATTEMPT_TIME = 0;
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

// Одна точка карты кадром: приложению она нужна, чтобы показать таблицу и дать её
// править. Кадр держим в пределах BLE-нотификации (см. WIFI_SCAN_SSID_MAX выше).
String mapPointJson(int cart, int point) {
  int index = cart - 1;

  String json = "{\"event\":\"map_point\",\"data\":{";
  json += "\"cart\":" + String(cart) + ",";
  json += "\"point\":" + String(point) + ",";
  json += "\"rpm\":" + String(rpmsArr[index][point], 2) + ",";
  json += "\"angle\":" + String(anglesArr[index][point], 2) + ",";
  json += "\"aa\":" + String(angles_adjustmentArr[index][point]) + ",";
  json += "\"qa\":" + String(quick_adjustmentArr[index][point]) + ",";
  json += "\"servo\":" + String(servo_valueArr[index][point]);
  json += "}}";

  return json;
}

// Запись точки карты: тем же способом, что и HTTP-маршрут /update_card вшитой страницы
// (core/servers/DEVICE_API_ROUTES.h), но для явно указанной карты и с записью в NVS.
// Ключи обязаны совпадать с загрузкой в config/CONFIG.h — иначе правка не переживёт
// перезагрузку.
bool applyMapPoint(int cart, int point, float rpm, float angle, float angleAdjust, int quickAdjust, float servo) {
  int index = cart - 1;

  rpmsArr[index][point] = rpm;
  anglesArr[index][point] = angle;
  angles_adjustmentArr[index][point] = (int) angleAdjust;
  quick_adjustmentArr[index][point] = quickAdjust;
  servo_valueArr[index][point] = (int) servo;

  String suffix = String(index) + "_" + String(point);

  preferences.putInt(("rpm" + suffix).c_str(), (int) (rpm * 100));
  preferences.putInt(("angles" + suffix).c_str(), (int) (angle * 100));
  preferences.putInt(("aa" + suffix).c_str(), (int) (angleAdjust * 100));
  preferences.putInt(("qa" + suffix).c_str(), (int) (quickAdjust * 100));
  preferences.putInt(("servo" + suffix).c_str(), (int) servo);

  Serial.print("map saved: cart ");
  Serial.print(cart);
  Serial.print(" point ");
  Serial.println(point);

  return true;
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

  if (json["event"] == "diag") {
    // Удалённая диагностика по WiFi. Режим включается с перезагрузкой: в нём блок не поднимает
    // Bluetooth, точку доступа и веб-сервер, иначе рукопожатию TLS не хватает памяти.
    int enabled = json["data"]["enabled"] | 0;

    DIAGNOSTICS_WIFI_ENABLED = enabled != 0;
    preferences.putBool("diag_en", DIAGNOSTICS_WIFI_ENABLED);
    preferences.putULong("diag_at", DIAGNOSTICS_WIFI_ENABLED ? (unsigned long) time(nullptr) : 0);

    Serial.print("diag_en ");
    Serial.println(DIAGNOSTICS_WIFI_ENABLED ? 1 : 0);

    out = "{\"event\":\"diag\",\"data\":{\"enabled\":" + String(DIAGNOSTICS_WIFI_ENABLED ? 1 : 0) +
          ",\"reboot\":1}}";

    // Режим меняет состав задач и стек: применяем его перезагрузкой в любом случае —
    // и при включении, и при выключении (иначе Bluetooth не вернётся к приложению).
    delay(1500);
    ESP.restart();

    return true;
  }

  if (json["event"] == "ping") {
    out = "{\"event\":\"pong\"}";
    return true;
  }

  // Карта зажигания: чтение одной точки. Таблицу приложение обходит по точкам — целиком
  // она в кадр не влезает (22 точки × 5 чисел).
  if (json["event"] == "map_point") {
    int cart = json["data"]["cart"] | 1;
    int point = json["data"]["point"] | 0;

    if (cart < 1 || cart > 3 || point < 0 || point >= MAP_POINTS_MAX) {
      out = "{\"event\":\"map_point\",\"data\":{\"error\":\"bad index\"}}";
      return true;
    }

    out = mapPointJson(cart, point);
    return true;
  }

  // Запись одной точки: углы в градусах, коррекции — как их ждёт зажигание.
  if (json["event"] == "map_set") {
    JsonObject &data = json["data"];
    int cart = data["cart"] | 1;
    int point = data["point"] | 0;

    if (cart < 1 || cart > 3 || point < 0 || point >= MAP_POINTS_MAX) {
      out = "{\"event\":\"map_set\",\"data\":{\"error\":\"bad index\"}}";
      return true;
    }

    float rpm = data["rpm"] | rpmsArr[cart - 1][point];
    float angle = data["angle"] | anglesArr[cart - 1][point];
    float angleAdjust = data["aa"] | (float) angles_adjustmentArr[cart - 1][point];
    int quickAdjust = data["qa"] | quick_adjustmentArr[cart - 1][point];
    float servo = data["servo"] | (float) servo_valueArr[cart - 1][point];

    applyMapPoint(cart, point, rpm, angle, angleAdjust, quickAdjust, servo);

    out = "{\"event\":\"map_set\",\"data\":{\"cart\":" + String(cart)
        + ",\"point\":" + String(point) + ",\"saved\":1}}";
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
