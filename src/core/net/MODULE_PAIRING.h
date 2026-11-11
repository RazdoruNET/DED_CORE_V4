#ifndef MODULE_PAIRING_H
#define MODULE_PAIRING_H

// Сопряжение внешних модулей (DED CORE SERVO и других) с блоком.
//
// Пароль точки доступа уникален для каждого блока и живёт только в NVS: модуль,
// который к блоку ещё не подключён, узнать его не может ни по HTTP (там нужен
// X-Device-Key), ни из кадра телеметрии (пароль оттуда убран). Bluetooth тоже не
// вариант: модуль — не телефон и код сопряжения не вводит.
//
// Поэтому блок на время поднимает отдельную открытую сеть DED_PAIR_<хвост MAC>
// с одним маршрутом: GET /module/pair отдаёт имя и пароль основной точки доступа.
//
// Кнопок на блоке нет — корпус закрыт, физического подтверждения быть не может.
// Окно открывает владелец из приложения: POST /module/pair/start с ключом
// устройства (X-Device-Key). Ключ есть только у привязанного приложения, то есть
// окно открывает тот, кто уже владеет блоком. Пока блок не привязан, действует
// общее правило DEVICE_API_ROUTES.h: менять состояние можно из сети самого блока.
//
// Окно короткое и закрывается само, а после ответа модулю основная точка доступа
// возвращается. На время сопряжения телефон отключается от сети блока: ESP32
// держит одну точку доступа, и второй одновременно не бывает.

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#include "config/PROPS.h"
#include "core/ota/HYBRID_OTA.h"

extern HybridOTAManager otaManager;

// Префикс сети сопряжения. По нему модуль ищет блок сканированием.
#define MODULE_PAIR_PREFIX "DED_PAIR_"

// Сколько живёт окно сопряжения, если никто не пришёл.
#define MODULE_PAIR_WINDOW_MS 90000UL

// Задержка перед подменой точки доступа: ответ на POST /module/pair/start
// должен уйти в сеть раньше, чем сеть исчезнет.
#define MODULE_PAIR_SWITCH_DELAY_MS 700UL

// Сколько ждём после ответа модулю: он должен успеть дочитать ответ.
#define MODULE_PAIR_RESTORE_DELAY_MS 3000UL

namespace module_pairing {

bool g_active = false;
bool g_openPending = false;
bool g_closePending = false;
bool g_switchScheduled = false;

unsigned long g_switchAt = 0;
unsigned long g_windowEndAt = 0;
unsigned long g_restoreAt = 0;

// Имя сети сопряжения уникально для блока: рядом могут стоять два блока,
// и модуль должен выбрать свой.
inline String pairApSsid() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");

  return String(MODULE_PAIR_PREFIX) + mac.substring(mac.length() - 4);
}

inline bool active() {
  return g_active || g_openPending;
}

// Основная точка доступа возвращается с прежним паролем из NVS.
inline void restore() {
  if (!g_active) {
    return;
  }

  g_active = false;
  g_windowEndAt = 0;
  g_restoreAt = 0;

  if (APPSK1.length() < 8) {
    Serial.println("Module pairing: closed, AP password is not set");
    return;
  }

  WiFi.softAP(APSSID1, APPSK1);

  Serial.print("Module pairing: closed, AP restored: ");
  Serial.println(APSSID1);
}

inline void open() {
  if (APPSK1.length() < 8) {
    Serial.println("Module pairing: AP password is not set, window skipped");
    return;
  }

  // Одна точка доступа на ESP32: основную на время окна заменяет сеть
  // сопряжения, иначе модуль не смог бы к ней подключиться.
  WiFi.softAP(pairApSsid().c_str());

  g_active = true;
  g_windowEndAt = millis() + MODULE_PAIR_WINDOW_MS;
  g_restoreAt = 0;

  Serial.print("Module pairing: open AP ");
  Serial.print(pairApSsid());
  Serial.println(" (no password)");
}

inline void scheduleSwitch() {
  g_switchScheduled = true;
  g_switchAt = millis() + MODULE_PAIR_SWITCH_DELAY_MS;
}

// Открыть окно по команде приложения. Если окно уже открыто — продлить.
inline void requestOpen() {
  g_closePending = false;

  if (g_active) {
    g_windowEndAt = millis() + MODULE_PAIR_WINDOW_MS;
    return;
  }

  g_openPending = true;
  scheduleSwitch();
}

// Закрыть окно досрочно, не дожидаясь таймера.
inline void requestClose() {
  g_openPending = false;

  if (!g_active) {
    return;
  }

  g_closePending = true;
  scheduleSwitch();
}

// Закрыть окно после ответа модулю: ему нужно время дочитать ответ.
inline void scheduleRestore() {
  g_restoreAt = millis() + MODULE_PAIR_RESTORE_DELAY_MS;
}

// Вызывается из loop(): отложенная подмена точки доступа и таймеры окна.
inline void tick() {
  if (g_switchScheduled && static_cast<int32_t>(millis() - g_switchAt) >= 0) {
    g_switchScheduled = false;

    if (g_openPending) {
      g_openPending = false;
      open();
    } else if (g_closePending) {
      g_closePending = false;
      restore();
    }
  }

  if (!g_active) {
    return;
  }

  if (g_restoreAt != 0 && static_cast<int32_t>(millis() - g_restoreAt) >= 0) {
    restore();
    return;
  }

  if (static_cast<int32_t>(millis() - g_windowEndAt) >= 0) {
    restore();
  }
}

// Ответ модулю.
inline String payloadJson() {
  String json = "{\"event\":\"module_pair\",\"data\":{";
  json += "\"ssid\":\"" + APSSID1 + "\",";
  json += "\"pass\":\"" + APPSK1 + "\",";
  json += "\"host\":\"192.168.4.1\",";
  json += "\"port\":81,";
  json += "\"fw\":\"" FIRMWARE_VERSION "\"";
  json += "}}";

  return json;
}

// Ответ приложению: что сейчас с окном.
inline String stateJson() {
  const bool windowOpen = active();

  String json = "{\"event\":\"module_pair\",\"data\":{";
  json += "\"active\":";
  json += windowOpen ? "1" : "0";
  json += ",\"ssid\":\"";
  json += windowOpen ? pairApSsid() : String("");
  json += "\",\"window_ms\":";
  json += String(MODULE_PAIR_WINDOW_MS);
  json += "}}";

  return json;
}

inline bool fromAccessPoint(AsyncWebServerRequest *request) {
  const IPAddress client = request->client()->remoteIP();

  return client[0] == 192 && client[1] == 168 && client[2] == 4;
}

// Сравнение без раннего выхода: посимвольный возврат на первом несовпадении
// даёт лишний канал о значении ключа (так же, как в DEVICE_API_ROUTES.h).
inline bool keyEquals(const String &left, const String &right) {
  if (left.length() == 0 || left.length() != right.length()) {
    return false;
  }

  unsigned char diff = 0;

  for (size_t i = 0; i < left.length(); i++) {
    diff |= static_cast<unsigned char>(left[i] ^ right[i]);
  }

  return diff == 0;
}

// Право открыть окно: ключ устройства, если блок привязан, иначе — из сети
// самого блока. Правило то же, что у остальных действий, меняющих состояние
// (servers/DEVICE_API_ROUTES.h), но проверка своя: модуль сопряжения не должен
// зависеть от порядка включения заголовков серверов.
inline bool changeAllowed(AsyncWebServerRequest *request) {
  const String deviceKey = otaManager.getDeviceKey();

  if (deviceKey.length() == 0) {
    return fromAccessPoint(request);
  }

  if (!request->hasHeader("X-Device-Key")) {
    return false;
  }

  return keyEquals(request->getHeader("X-Device-Key")->value(), deviceKey);
}

inline void denyKey(AsyncWebServerRequest *request) {
  request->send(403, "text/plain", "Нужен ключ устройства (X-Device-Key)");
}

}  // namespace module_pairing

inline void registerModulePairingRoutes(AsyncWebServer *server) {
  // Модуль забирает параметры основной точки доступа. Маршрут живёт только
  // в сети сопряжения и только при открытом окне.
  server->on("/module/pair", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!module_pairing::active() || !module_pairing::fromAccessPoint(request)) {
      request->send(403, "text/plain", "Pairing window is closed");
      return;
    }

    Serial.print("Module pairing: module ");
    Serial.print(request->client()->remoteIP());
    Serial.println(" received the AP credentials");

    request->send(200, "application/json", module_pairing::payloadJson());

    // Основную точку доступа возвращаем с задержкой: ответ ещё должен уйти.
    module_pairing::scheduleRestore();
  });

  // Владелец открывает окно из приложения. Пока блок привязан, нужен ключ
  // устройства; у непривязанного блока действует правило «из сети блока».
  server->on("/module/pair/start", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!module_pairing::changeAllowed(request)) {
      module_pairing::denyKey(request);
      return;
    }

    module_pairing::requestOpen();

    Serial.println("Module pairing: window requested by the app");

    request->send(200, "application/json", module_pairing::stateJson());
  });

  server->on("/module/pair/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!module_pairing::changeAllowed(request)) {
      module_pairing::denyKey(request);
      return;
    }

    module_pairing::requestClose();

    request->send(200, "application/json", module_pairing::stateJson());
  });
}

#endif // MODULE_PAIRING_H
