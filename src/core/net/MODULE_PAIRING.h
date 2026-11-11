#ifndef MODULE_PAIRING_H
#define MODULE_PAIRING_H

// Сопряжение внешних модулей (DED CORE SERVO и других) с блоком.
//
// Пароль точки доступа уникален для каждого блока и живёт только в NVS: модуль,
// который к блоку ещё не подключён, узнать его не может ни по HTTP (там нужен
// X-Device-Key), ни из кадра телеметрии (пароль оттуда убран). Bluetooth тоже не
// вариант: модуль — не телефон и код сопряжения не вводит.
//
// Поэтому по нажатию кнопки на блоке на минуту поднимается открытая сеть
// сопряжения DED_PAIR_<хвост MAC> с одним маршрутом: GET /module/pair отдаёт
// имя и пароль основной точки доступа. Кнопка — то же физическое подтверждение,
// что и у pair_info по Bluetooth: нажать её может только владелец блока.
//
// Окно короткое и закрывается само; после ответа основная точка доступа
// возвращается. На время сопряжения телефон владельца отваливается от сети
// блока — это ожидаемо и длится считанные секунды.

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#include "config/PROPS.h"

// Префикс сети сопряжения. По нему модуль ищет блок сканированием.
#define MODULE_PAIR_PREFIX "DED_PAIR_"

// Сколько живёт окно сопряжения, если никто не пришёл.
#define MODULE_PAIR_WINDOW_MS 90000UL

// Сколько ждём после ответа: модуль должен успеть прочитать ответ и отключиться.
#define MODULE_PAIR_RESTORE_DELAY_MS 3000UL

// Пауза антидребезга кнопки.
#define MODULE_PAIR_BUTTON_DEBOUNCE_MS 50UL

// Объявлен в core/recovery/RECOVERY_MODE.h: в режиме восстановления окно
// сопряжения не открывается, иначе оно заменит сеть DED_RECOVERY.
extern bool recoveryModeActive;

namespace module_pairing {

bool g_active = false;
unsigned long g_windowEndAt = 0;
unsigned long g_restoreAt = 0;
bool g_buttonWasDown = false;
bool g_buttonWasUp = false;
unsigned long g_buttonChangedAt = 0;

// Имя сети сопряжения уникально для блока: рядом могут стоять два блока,
// и модуль должен выбрать свой.
inline String pairApSsid() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");

  return String(MODULE_PAIR_PREFIX) + mac.substring(mac.length() - 4);
}

inline bool active() {
  return g_active;
}

inline void close() {
  if (!g_active) {
    return;
  }

  g_active = false;
  g_windowEndAt = 0;
  g_restoreAt = 0;

  // Возвращаем основную точку доступа. Пароль уже создан в initWifi(),
  // поэтому восстановление всегда возможно.
  if (APPSK1.length() >= 8) {
    WiFi.softAP(APSSID1, APPSK1);
    Serial.print("Module pairing: closed, AP restored: ");
    Serial.println(APSSID1);
  } else {
    Serial.println("Module pairing: closed, AP password is not set");
  }
}

inline void open() {
  // Отдавать модулю нечего: без пароля основной точки доступа окно бессмысленно.
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
  Serial.println(" (no password), press the button again to extend");
}

// Закрыть окно после ответа модулю: ему нужно время дочитать ответ.
inline void scheduleRestore() {
  g_restoreAt = millis() + MODULE_PAIR_RESTORE_DELAY_MS;
}

// Вызывается из loop(). Кнопка нажата — открыть (или продлить) окно,
// истекло время — вернуть основную точку доступа.
inline void tick() {
  if (recoveryModeActive) {
    return;
  }

  const bool buttonDown = digitalRead(select_cart_down_pin) == LOW;

  if (buttonDown != g_buttonWasDown) {
    g_buttonWasDown = buttonDown;
    g_buttonChangedAt = millis();

    if (!buttonDown) {
      g_buttonWasUp = true;
    }
  }

  if (g_active) {
    if (g_restoreAt != 0 && static_cast<int32_t>(millis() - g_restoreAt) >= 0) {
      close();
      return;
    }

    if (static_cast<int32_t>(millis() - g_windowEndAt) >= 0) {
      close();
      return;
    }
  }

  // Кнопку, зажатую ещё с включения или после перезагрузки, за нажатие не
  // считаем: окно открывается только по новому нажатию после отпускания.
  if (g_buttonWasUp && buttonDown &&
      static_cast<uint32_t>(millis() - g_buttonChangedAt) >= MODULE_PAIR_BUTTON_DEBOUNCE_MS) {
    // Одно нажатие — одно окно: удержание кнопки не должно держать сеть
    // сопряжения открытой бесконечно.
    g_buttonWasUp = false;

    if (g_active) {
      g_windowEndAt = millis() + MODULE_PAIR_WINDOW_MS;
      return;
    }

    open();
  }
}

// Ответ модулю. Пароль основного доступа отдаём только из сети сопряжения:
// запрос с домашней сети (STA-интерфейс) сюда не пройдёт, да и окно для него
// не открывается.
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

inline bool fromAccessPoint(AsyncWebServerRequest *request) {
  const IPAddress client = request->client()->remoteIP();

  return client[0] == 192 && client[1] == 168 && client[2] == 4;
}

}  // namespace module_pairing

inline void registerModulePairingRoutes(AsyncWebServer *server) {
  server->on("/module/pair", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!module_pairing::active() || !module_pairing::fromAccessPoint(request)) {
      request->send(403, "text/plain",
                    "Pairing window is closed: press the button on the block");
      return;
    }

    Serial.print("Module pairing: module ");
    Serial.print(request->client()->remoteIP());
    Serial.println(" received the AP credentials");

    request->send(200, "application/json", module_pairing::payloadJson());

    // Основную точку доступа возвращаем с задержкой: ответ ещё должен уйти.
    module_pairing::scheduleRestore();
  });
}

#endif // MODULE_PAIRING_H
