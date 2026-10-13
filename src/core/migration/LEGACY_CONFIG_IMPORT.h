#ifndef LEGACY_CONFIG_IMPORT_H
#define LEGACY_CONFIG_IMPORT_H

// Перенос настроек с заводской прошивки.
//
// Заводские устройства (SYSTEM_MOTO_ESP32 и её сборки) держат настройки JSON-файлом
// /config.json в SPIFFS, а DED CORE V4 — в NVS. Без переноса владелец после обновления
// получил бы заводские карты вместо своей настройки зажигания.
//
// Значения в заводском JSON целые (там всё читается через as<int>()), а DED CORE V4
// хранит углы, обороты и коррекции с двумя знаками после запятой — поэтому при переносе
// умножаем на 100 так же, как это делает loadConfig() при чтении.
//
// Запускается один раз: после переноса в NVS ставится отметка legacy_imported.
//
// Важно: SPIFFS здесь остаётся SPIFFS при любой сборке — заводская прошивка хранит
// настройки именно в ней. В сборке для новой разметки функция ничего не делает
// (см. внутри): там этот раздел занят файловой системой блока.

#include <Arduino.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#define LEGACY_CONFIG_PATH   "/config.json"
#define LEGACY_CONFIG_MARKER "legacy_imported"
#define LEGACY_CART_COUNT    3
#define LEGACY_POINT_COUNT   7

extern Preferences preferences;

// Заводское значение -> вид, в котором его читает DED CORE V4 (целое * 100).
int legacyScaled(JsonObject &json, const String &key, int fallback) {
  if (!json.containsKey(key)) {
    return fallback;
  }

  return ((int)json[key].as<float>()) * 100;
}

void legacyImportGlobals(JsonObject &json) {
  if (json.containsKey("Init_angle")) {
    preferences.putInt("Init_angle", json["Init_angle"].as<int>());
  }
  if (json.containsKey("Init_flash_time")) {
    preferences.putInt("flash_time", json["Init_flash_time"].as<int>());
  }
  if (json.containsKey("max_rpm")) {
    preferences.putInt("max_rpm", json["max_rpm"].as<int>());
  }
  if (json.containsKey("max_rpm_angle_kiker")) {
    preferences.putInt("max_rpm_ang_k", json["max_rpm_angle_kiker"].as<int>());
  }
  if (json.containsKey("HIGH_OUTPUT")) {
    preferences.putBool("HIGH_OUTPUT", json["HIGH_OUTPUT"] == 1);
  }
  if (json.containsKey("ANGLE_ADVANCE")) {
    preferences.putBool("ANGLE_ADV", json["ANGLE_ADVANCE"] == 1);
  }
  if (json.containsKey("QUICKSHIFTER")) {
    preferences.putBool("QUICKSHIFT", json["QUICKSHIFTER"] == 1);
  }
  if (json.containsKey("INVERT_SENSOR")) {
    preferences.putBool("INVERT_SENS", json["INVERT_SENSOR"] == 1);
  }
  if (json.containsKey("INVERT_THROTTLE")) {
    preferences.putBool("INVERT_THROT", json["INVERT_THROTTLE"] == 1);
  }

  // Точка доступа: сохраняем заводское имя и пароль, телефон владельца уже знает эту сеть.
  if (json.containsKey("APSSID1")) {
    preferences.putString("APSSID1", json["APSSID1"].as<String>());
  }
  if (json.containsKey("APPSK1")) {
    preferences.putString("APPSK1", json["APPSK1"].as<String>());
  }

  // Домашняя сеть: в заводской прошивке это APSSID/APPSK, в DED CORE V4 — сеть для обновлений.
  String stationSsid = json["APSSID"].as<String>();
  if (stationSsid.length() > 0) {
    preferences.putString("wifi_ssid", stationSsid);
    preferences.putString("wifi_pass", json["APPSK"].as<String>());
    preferences.putBool("wifi_auto", true);
  }

  // Заводские таблицы — на 7 точек; столько же использует DED CORE V4 при type = 0.
  preferences.putInt("type", 0);
}

void legacyImportTables(JsonObject &json) {
  for (int cart = 0; cart < LEGACY_CART_COUNT; cart++) {
    for (int point = 0; point < LEGACY_POINT_COUNT; point++) {
      String prefix = String(cart);
      String suffix = String(cart) + "_" + String(point);

      // Отметки шкал и углы: нулевых значений быть не должно, поэтому пропускаем отсутствующие.
      int rpm = legacyScaled(json, prefix + "rpms" + point, 0);
      if (rpm > 0) {
        preferences.putInt(("rpm" + suffix).c_str(), rpm);
      }

      int angle = legacyScaled(json, prefix + "angles" + point, 0);
      if (angle > 0) {
        preferences.putInt(("angles" + suffix).c_str(), angle);
      }

      // Коррекция угла и квикшифтер: ноль — значимое значение, пишем всегда.
      preferences.putInt(("aa" + suffix).c_str(), legacyScaled(json, prefix + "anglesadj" + point, 0));
      preferences.putInt(("qa" + suffix).c_str(), legacyScaled(json, prefix + "quick" + point, 0));
    }
  }
}

// true — перенос выполнен (или переносить было нечего).
bool importLegacyConfigIfNeeded() {
  if (preferences.getBool(LEGACY_CONFIG_MARKER, false)) {
    return true;
  }

#ifndef DED_CORE_LEGACY_LAYOUT
  // В новой разметке заводских настроек в SPIFFS уже нет: их переносит сборка перехода,
  // пока работает с заводской таблицей разделов. Монтировать здесь SPIFFS нельзя —
  // на этом разделе лежит LittleFS с вшитым интерфейсом, и SPIFFS.begin(true)
  // отформатировала бы её вместе с сертификатом.
  return true;
#else
  if (!SPIFFS.begin(true)) {
    Serial.println("Legacy config: SPIFFS unavailable");
    return false;
  }

  if (!SPIFFS.exists(LEGACY_CONFIG_PATH)) {
    // Новая плата или уже переразмеченный флеш: переносить нечего.
    preferences.putBool(LEGACY_CONFIG_MARKER, true);
    return true;
  }

  File configFile = SPIFFS.open(LEGACY_CONFIG_PATH, "r");
  if (!configFile) {
    Serial.println("Legacy config: open failed");
    return false;
  }

  size_t size = configFile.size();
  if (size == 0 || size > 8000) {
    Serial.print("Legacy config: unexpected size ");
    Serial.println(size);
    configFile.close();
    return false;
  }

  std::unique_ptr<char[]> buffer(new char[size + 1]);
  configFile.readBytes(buffer.get(), size);
  buffer[size] = 0;
  configFile.close();

  DynamicJsonBuffer jsonBuffer;
  JsonObject &json = jsonBuffer.parseObject(buffer.get());

  if (!json.success()) {
    Serial.println("Legacy config: parse failed");
    return false;
  }

  Serial.println("Legacy config: importing settings from factory firmware");

  legacyImportGlobals(json);
  legacyImportTables(json);

  preferences.putBool(LEGACY_CONFIG_MARKER, true);
  Serial.println("Legacy config: import done");
  return true;
#endif
}

#endif // LEGACY_CONFIG_IMPORT_H
