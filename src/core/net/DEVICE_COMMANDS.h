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
// Определение метки ротора: команда `mark_scan` живёт здесь, а перебор — в ignition/MARK_SCAN.h.
// Заголовок идемпотентен, поэтому его же подключает core/ignition/MOTO.h.
#include "core/ignition/MARK_SCAN.h"

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

// --- Настройки зажигания и сервопривода ---------------------------------------
//
// Те же настройки, что пишет вшитая страница блока по HTTP (servers/DEVICE_API_ROUTES.h):
// `/max`, `/angle`, `/angle/advance`, `/modulator`, `/quickshifter`, `/sensor`,
// `/throttle`, `/carts/switching`, `/servo`. Приложение шлёт патч — только те поля,
// которые владелец поменял; пустой патч означает «прочитать».
//
// Чтение по команде нужно и потому, что короткий кадр показаний Bluetooth настроек не
// несёт: без него раздел настроек на телефоне без сертификата остался бы пустым.
//
// Ключи NVS обязаны совпадать с загрузкой в config/CONFIG.h — настройка, записанная
// другим ключом, жила бы до первой перезагрузки.

// Границы значений: те же, что у полей вшитой страницы и ползунков карты в приложении.
// Значение вне диапазона исказило бы зажигание, поэтому его молча подрезаем.
#define SETTINGS_MAX_RPM_MIN 1000
#define SETTINGS_MAX_RPM_MAX 20000
#define SETTINGS_ANGLE_MIN 0
#define SETTINGS_ANGLE_MAX 60
// Продолжительность сигнала прошивка сравнивает с micros(), то есть это микросекунды.
#define SETTINGS_FLASH_TIME_MIN 0
#define SETTINGS_FLASH_TIME_MAX 20000
#define SETTINGS_FILTER_MIN 0
#define SETTINGS_FILTER_MAX 100
#define SETTINGS_SERVO_RPM_MIN 0
#define SETTINGS_SERVO_RPM_MAX 20000
#define SETTINGS_SERVO_PERCENT_MIN 0
#define SETTINGS_SERVO_PERCENT_MAX 255
#define SETTINGS_SERVO_TYPE_MAX 1

int clampSetting(int value, int low, int high) {
  if (value < low) {
    return low;
  }

  return value > high ? high : value;
}

// Флаг настройки: приложение шлёт 0/1, в памяти он лежит булевым.
bool settingFlag(JsonVariant value) {
  return value.as<int>() != 0;
}

// Патч настроек: меняем и запоминаем только те поля, что пришли в кадре.
void applySettingsPatch(JsonObject &data) {
  if (data.containsKey("max_rpm")) {
    max_rpm = clampSetting(data["max_rpm"].as<int>(), SETTINGS_MAX_RPM_MIN, SETTINGS_MAX_RPM_MAX);
    preferences.putInt("max_rpm", max_rpm);
  }

  if (data.containsKey("max_rpm_angle_kiker")) {
    max_rpm_angle_kiker = clampSetting(data["max_rpm_angle_kiker"].as<int>(), SETTINGS_ANGLE_MIN, SETTINGS_ANGLE_MAX);
    preferences.putInt("max_rpm_ang_k", max_rpm_angle_kiker);
  }

  if (data.containsKey("init_angle")) {
    Init_angle = clampSetting(data["init_angle"].as<int>(), SETTINGS_ANGLE_MIN, SETTINGS_ANGLE_MAX);
    preferences.putInt("Init_angle", Init_angle);
  }

  if (data.containsKey("init_flash_time")) {
    Init_flash_time = clampSetting(data["init_flash_time"].as<int>(), SETTINGS_FLASH_TIME_MIN, SETTINGS_FLASH_TIME_MAX);
    preferences.putInt("flash_time", Init_flash_time);
  }

  if (data.containsKey("filter_val")) {
    filter = clampSetting(data["filter_val"].as<int>(), SETTINGS_FILTER_MIN, SETTINGS_FILTER_MAX);
    preferences.putInt("filter", filter);
  }

  if (data.containsKey("aa")) {
    ANGLE_ADVANCE = settingFlag(data["aa"]) ? 1 : 0;
    preferences.putBool("ANGLE_ADV", ANGLE_ADVANCE != 0);
  }

  if (data.containsKey("qs")) {
    QUICKSHIFTER = settingFlag(data["qs"]) ? 1 : 0;
    preferences.putBool("QUICKSHIFT", QUICKSHIFTER != 0);
  }

  if (data.containsKey("cc")) {
    CARTS_SWHITCHING = settingFlag(data["cc"]) ? 1 : 0;
    preferences.putBool("CART_SWHITCH", CARTS_SWHITCHING != 0);
  }

  if (data.containsKey("high")) {
    HIGH_OUTPUT = settingFlag(data["high"]);
    preferences.putBool("HIGH_OUTPUT", HIGH_OUTPUT);
  }

  if (data.containsKey("invert_sensor")) {
    INVERT_SENSOR = settingFlag(data["invert_sensor"]);
    preferences.putBool("INVERT_SENS", INVERT_SENSOR);
  }

  if (data.containsKey("invert_throttle")) {
    INVERT_THROTTLE = settingFlag(data["invert_throttle"]);
    preferences.putBool("INVERT_THROT", INVERT_THROTTLE);
  }

  if (data.containsKey("servo")) {
    SERVO = settingFlag(data["servo"]);
    preferences.putBool("SERVO", SERVO);
  }

  if (data.containsKey("servo_type")) {
    Servo_type = clampSetting(data["servo_type"].as<int>(), 0, SETTINGS_SERVO_TYPE_MAX);
    preferences.putInt("servo_type", Servo_type);
  }

  if (data.containsKey("servo_rpm")) {
    Servo_rpm = clampSetting(data["servo_rpm"].as<int>(), SETTINGS_SERVO_RPM_MIN, SETTINGS_SERVO_RPM_MAX);
    preferences.putInt("servo_rpm", Servo_rpm);
  }

  if (data.containsKey("servo_percent")) {
    Servo_percent = clampSetting(data["servo_percent"].as<int>(), SETTINGS_SERVO_PERCENT_MIN, SETTINGS_SERVO_PERCENT_MAX);
    preferences.putInt("servo_perc", Servo_percent);
  }
}

// --- Точка доступа блока и перезагрузка ----------------------------------------
//
// Имя и пароль точки доступа пишет вшитая страница маршрутом `/wifi`
// (servers/DEVICE_API_ROUTES.h). Приложение умеет то же: владелец переименовывает блок
// со телефона, не заходя на его страницу. Пустое поле означает «оставить как было».

void applyApSettings(const String &ssid, const String &pass) {
  if (ssid.length() > 0) {
    APSSID1 = ssid;
    preferences.putString("APSSID1", APSSID1);
  }

  if (pass.length() > 0) {
    APPSK1 = pass;
    preferences.putString("APPSK1", APPSK1);
  }

  Serial.print("AP name: ");
  Serial.println(APSSID1);
}

// Пароль точки доступа в кадр не кладём: приложение показывает только то, что он задан.
String apStateJson() {
  String json = "{\"event\":\"ap\",\"data\":{";
  json += "\"wsid\":\"" + APSSID1 + "\",";
  json += "\"password_set\":" + String(APPSK1.length() > 0 ? 1 : 0);
  json += "}}";

  return json;
}

// Снимок настроек. Двумя кадрами, а не одним: уведомление Bluetooth ограничено
// согласованным MTU (255 → 252 байта), и один кадр со всеми полями за предел выходит.
String settingsCoreJson() {
  String json = "{\"event\":\"settings\",\"data\":{";
  json += "\"max_rpm\":" + String(max_rpm) + ",";
  json += "\"max_rpm_angle_kiker\":" + String(max_rpm_angle_kiker) + ",";
  json += "\"init_angle\":" + String(Init_angle) + ",";
  json += "\"init_flash_time\":" + String(Init_flash_time) + ",";
  json += "\"filter_val\":" + String(filter) + ",";
  json += "\"type\":" + String(type);
  json += "}}";

  return json;
}

String settingsFlagsJson() {
  String json = "{\"event\":\"settings_flags\",\"data\":{";
  json += "\"aa\":" + String(ANGLE_ADVANCE ? 1 : 0) + ",";
  json += "\"qs\":" + String(QUICKSHIFTER ? 1 : 0) + ",";
  json += "\"cc\":" + String(CARTS_SWHITCHING ? 1 : 0) + ",";
  json += "\"high\":" + String(HIGH_OUTPUT ? 1 : 0) + ",";
  // Имена те же, что в кадре телеметрии: приложение разбирает оба кадра одним кодом.
  json += "\"INVERT_SENSOR\":" + String(INVERT_SENSOR ? 1 : 0) + ",";
  json += "\"INVERT_THROTTLE\":" + String(INVERT_THROTTLE ? 1 : 0) + ",";
  json += "\"servo\":" + String(SERVO ? 1 : 0) + ",";
  json += "\"servo_type\":" + String(Servo_type) + ",";
  json += "\"servo_rpm\":" + String(Servo_rpm) + ",";
  json += "\"servo_percent\":" + String(Servo_percent);
  json += "}}";

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

  // Определение метки ротора: мотор крутит владелец, блок перебирает кандидатов и следит
  // за оборотами (подробности — core/ignition/MARK_SCAN.h).
  if (json["event"] == "mark_scan") {
    String action = jsonString(json["data"], "action");

    if (action == "start") {
      markScanStart(jsonString(json["data"], "method"));
    } else if (action == "stop") {
      markScanStop("владелец остановил определение");
    } else if (action == "apply" && markScanBest >= 0) {
      markScanApply(markScanBest);
      sendDeviceFrame(markScanJson());
    } else {
      // Без действия просто отдаём состояние: приложение узнаёт, идёт ли перебор сейчас.
      sendDeviceFrame(markScanJson());
    }

    return false;
  }

  // Имя и пароль точки доступа блока: то же, что делает `/wifi` у вшитой страницы.
  if (json["event"] == "ap") {
    JsonObject &data = json["data"];
    applyApSettings(jsonString(data, "ssid"), jsonString(data, "pass"));

    sendDeviceFrame(apStateJson());

    return false;
  }

  // Перезагрузка блока: ответ уходит по всем каналам до restart(), иначе приложение
  // не увидит подтверждения и будет ждать таймаут.
  if (json["event"] == "reboot") {
    sendDeviceFrame("{\"event\":\"reboot\",\"data\":{\"reboot\":1}}");

    Serial.println("reboot by app");
    delay(400);
    ESP.restart();

    return false;
  }

  // Настройки зажигания, сервопривода и датчиков: патч в `data`, ответ — снимок двумя
  // кадрами по всем каналам (тот же приём, что у состояния домашней сети).
  if (json["event"] == "settings") {
    if (json.containsKey("data")) {
      JsonObject &data = json["data"];
      applySettingsPatch(data);
    }

    sendDeviceFrame(settingsCoreJson());
    sendDeviceFrame(settingsFlagsJson());

    Serial.println("settings applied");

    return false;
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
