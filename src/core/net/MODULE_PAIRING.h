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
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <esp_random.h>

#include "config/PROPS.h"
#include "core/ota/HYBRID_OTA.h"

extern HybridOTAManager otaManager;

// Определён в servers/WEB_SOCKET_VARS.h (подключается раньше в основном файле).
extern WebSocketsServer webSocket;

extern Preferences preferences;

// Префикс сети сопряжения. По нему модуль ищет блок сканированием.
#define MODULE_PAIR_PREFIX "DED_PAIR_"

// Сколько живёт окно сопряжения, если никто не пришёл. Три минуты: владельцу
// нужно открыть окно в приложении блока, записать код и ввести его в портале модуля.
#define MODULE_PAIR_WINDOW_MS 180000UL

// Задержка перед подменой точки доступа: ответ на POST /module/pair/start
// должен уйти в сеть раньше, чем сеть исчезнет.
#define MODULE_PAIR_SWITCH_DELAY_MS 700UL

// Сколько ждём после ответа модулю: он должен успеть дочитать ответ.
#define MODULE_PAIR_RESTORE_DELAY_MS 3000UL

// Код сопряжения. Сеть DED_PAIR_* поднимается не открытой, а с WPA2-паролем,
// выведенным из этого кода: иначе любой в радиусе мог бы прочитать ответ
// /module/pair с паролем основной точки доступа. Код показывает приложение
// блока, владелец вводит его в портале модуля.
#define MODULE_PAIR_CODE_LENGTH 12

// Токен доверенного модуля. Выдаётся при сопряжении и приходит в каждом
// подтверждении связи (module_hello); по нему блок отличает сопряжённый модуль
// от телефона или чужого клиента в той же сети.
#define MODULE_TOKEN_LENGTH 16
#define MODULE_TOKEN_SLOTS 8

// Сколько WS-клиентов могут быть доверенными модулями одновременно.
#define MODULE_TRUSTED_CLIENTS 8

// Пауза между рассылкой новых параметров точки доступа и её перезапуском:
// модуль должен успеть сохранить их до обрыва связи.
#define MODULE_CREDENTIALS_DELAY_MS 1500UL

namespace module_pairing {

bool g_active = false;
bool g_openPending = false;
bool g_closePending = false;
bool g_switchScheduled = false;

unsigned long g_switchAt = 0;
unsigned long g_windowEndAt = 0;
unsigned long g_restoreAt = 0;

// Код текущего окна и признак «модуль уже забрал параметры». Один код — один
// модуль: иначе один и тот же код выпускал бы сколько угодно токенов.
String g_pairCode;
bool g_pairServed = false;

// Какие WS-клиенты подтвердили токен модуля.
bool g_trusted[MODULE_TRUSTED_CLIENTS] = {false};

bool g_credentialsNotified = false;
unsigned long g_credentialsNotifyAt = 0;

// Имя сети сопряжения уникально для блока: рядом могут стоять два блока,
// и модуль должен выбрать свой.
inline String pairApSsid() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");

  return String(MODULE_PAIR_PREFIX) + mac.substring(mac.length() - 4);
}

// WPA2-пароль сети сопряжения — сам код: 12 знаков без похожих друг на друга,
// минимальная длина WPA2 (8) соблюдена.
inline String pairApPassword() {
  return g_pairCode;
}

inline String randomPairCode() {
  static const char alphabet[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";

  String code;
  code.reserve(MODULE_PAIR_CODE_LENGTH);

  for (int i = 0; i < MODULE_PAIR_CODE_LENGTH; i++) {
    code += alphabet[esp_random() % (sizeof(alphabet) - 1)];
  }

  return code;
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
  g_pairCode = "";
  g_pairServed = false;

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

  if (g_pairCode.length() != MODULE_PAIR_CODE_LENGTH) {
    g_pairCode = randomPairCode();
  }

  // Одна точка доступа на ESP32: основную на время окна заменяет сеть
  // сопряжения, иначе модуль не смог бы к ней подключиться. Сеть закрыта
  // WPA2-паролем из кода: без кода к ней не подключиться и не перехватить ответ.
  WiFi.softAP(pairApSsid().c_str(), pairApPassword().c_str());

  g_active = true;
  g_windowEndAt = millis() + MODULE_PAIR_WINDOW_MS;
  g_restoreAt = 0;

  Serial.print("Module pairing: open WPA2 AP ");
  Serial.print(pairApSsid());
  Serial.println(" (code shown in the app)");
}

inline void scheduleSwitch() {
  g_switchScheduled = true;
  g_switchAt = millis() + MODULE_PAIR_SWITCH_DELAY_MS;
}

// Открыть окно по команде приложения. Если окно уже открыто — продлить.
inline void requestOpen() {
  g_closePending = false;

  if (g_active) {
    // Один код — один модуль: если параметры уже забрали, новое окно откроется
    // только после закрытия текущего.
    if (!g_pairServed) {
      g_windowEndAt = millis() + MODULE_PAIR_WINDOW_MS;
    }
    return;
  }

  // Каждое окно — новый код. Старый не даёт подключиться к следующему окну.
  g_pairCode = randomPairCode();
  g_pairServed = false;

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

// Определены ниже: tick() рассылает новые параметры перед перезапуском сети.
inline uint8_t notifyCredentials();

// Вызывается из loop(): смена параметров точки доступа, отложенная подмена
// точки доступа и таймеры окна.
inline void tick() {
  // Новые имя и пароль точки доступа. Сначала уходят сопряжённым модулям, иначе
  // после перезапуска сети они останутся со старой связкой и потеряют блок.
  if (ap_credentials_changed && !active() && !g_switchScheduled) {
    if (!g_credentialsNotified) {
      const uint8_t sent = notifyCredentials();

      g_credentialsNotified = true;
      g_credentialsNotifyAt = sent > 0 ? millis() : millis() - MODULE_CREDENTIALS_DELAY_MS;

      Serial.printf("AP change: new credentials sent to %u module(s)\n", static_cast<unsigned>(sent));
    } else if (static_cast<uint32_t>(millis() - g_credentialsNotifyAt) >= MODULE_CREDENTIALS_DELAY_MS) {
      WiFi.softAP(APSSID1, APPSK1);

      ap_credentials_changed = false;
      g_credentialsNotified = false;

      Serial.print("AP change: access point restarted as ");
      Serial.println(APSSID1);
    }
  }

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

// Ответ модулю. Токен — пропуск к обновлению параметров сети: без него смену
// пароля модуль увидит только через потерю связи.
inline String payloadJson(const String &token) {
  String json = "{\"event\":\"module_pair\",\"data\":{";
  json += "\"ssid\":\"" + APSSID1 + "\",";
  json += "\"pass\":\"" + APPSK1 + "\",";
  json += "\"host\":\"192.168.4.1\",";
  json += "\"port\":81,";
  json += "\"token\":\"" + token + "\",";
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
  json += "\",\"code\":\"";
  json += windowOpen ? g_pairCode : String("");
  json += "\",\"window_ms\":";
  json += String(MODULE_PAIR_WINDOW_MS);
  json += "}}";

  return json;
}

inline bool fromAccessPoint(AsyncWebServerRequest *request) {
  // Точный признак «запрос пришёл через точку доступа»: локальный адрес соединения
  // совпадает с адресом softAP. Подсеть клиента ничего не доказывает — домашняя
  // сеть тоже может быть 192.168.4.0/24.
  return request->client()->localIP() == WiFi.softAPIP();
}

// --- Доверенные модули --------------------------------------------------------
//
// Модуль, прошедший сопряжение, получает токен и при каждом подключении
// подтверждает его кадром module_hello. Только таким клиентам уходят новые
// параметры точки доступа: телефон владельца и чужой клиент в сети блока токена
// не знают, поэтому смена пароля не утекает всем, кто был подключён.

inline String tokenSlotKey(uint8_t slot) {
  return String("mtok") + String(slot);
}

// MAC модуля храним рядом с токеном: украденный в окне сопряжения токен не
// должен работать с другого устройства.
inline String tokenMacSlotKey(uint8_t slot) {
  return String("mmac") + String(slot);
}

inline String normalizeMac(const String &value) {
  String out;
  out.reserve(12);

  for (unsigned int i = 0; i < value.length(); i++) {
    const char symbol = value[i];

    if (isxdigit(static_cast<unsigned char>(symbol))) {
      out += static_cast<char>(toupper(static_cast<unsigned char>(symbol)));
    }
  }

  return out;
}

// Сравнение без раннего выхода: посимвольный возврат на первом несовпадении
// даёт лишний канал о значении токена.
inline bool tokenEquals(const String &left, const String &right) {
  if (left.length() == 0 || left.length() != right.length()) {
    return false;
  }

  unsigned char diff = 0;

  for (size_t i = 0; i < left.length(); i++) {
    diff |= static_cast<unsigned char>(left[i] ^ right[i]);
  }

  return diff == 0;
}

inline String randomToken() {
  static const char alphabet[] = "0123456789abcdef";

  String token;
  token.reserve(MODULE_TOKEN_LENGTH);

  for (int i = 0; i < MODULE_TOKEN_LENGTH; i++) {
    token += alphabet[esp_random() & 0x0F];
  }

  return token;
}

// Выдать токен при сопряжении. Свободный слот — под новый модуль; когда слоты
// кончились, заменяем самый старый. Старые токены остаются действительными,
// пока их не вытеснили: пересопряжение одного модуля не рвёт связку с другим.
inline String issueToken(const String &mac) {
  const String normalizedMac = normalizeMac(mac);

  if (normalizedMac.length() == 0) {
    Serial.println("Module pairing: module MAC is missing, token not issued");
    return String("");
  }

  for (uint8_t slot = 0; slot < MODULE_TOKEN_SLOTS; slot++) {
    const String key = tokenSlotKey(slot);
    const String stored = preferences.getString(key.c_str(), "");

    if (stored.length() == MODULE_TOKEN_LENGTH) {
      continue;
    }

    const String token = randomToken();
    preferences.putString(key.c_str(), token);
    preferences.putString(tokenMacSlotKey(slot).c_str(), normalizedMac);

    Serial.printf("Module pairing: token issued in slot %u\n", static_cast<unsigned>(slot));

    return token;
  }

  const String token = randomToken();
  preferences.putString(tokenSlotKey(0).c_str(), token);
  preferences.putString(tokenMacSlotKey(0).c_str(), normalizedMac);

  Serial.println("Module pairing: token slots are full, oldest replaced");

  return token;
}

inline bool verifyToken(const String &token, const String &mac) {
  const String normalizedMac = normalizeMac(mac);

  if (token.length() != MODULE_TOKEN_LENGTH || normalizedMac.length() == 0) {
    return false;
  }

  for (uint8_t slot = 0; slot < MODULE_TOKEN_SLOTS; slot++) {
    const String stored = preferences.getString(tokenSlotKey(slot).c_str(), "");
    const String storedMac = preferences.getString(tokenMacSlotKey(slot).c_str(), "");

    if (tokenEquals(stored, token) && storedMac.length() > 0 && storedMac.equalsIgnoreCase(normalizedMac)) {
      return true;
    }
  }

  return false;
}

// Забыть все сопряжённые модули: токены и привязанные MAC. Следующее
// сопряжение выдаст новые пропуска, старые перестанут работать сразу.
inline void forgetModules() {
  for (uint8_t slot = 0; slot < MODULE_TOKEN_SLOTS; slot++) {
    preferences.remove(tokenSlotKey(slot).c_str());
    preferences.remove(tokenMacSlotKey(slot).c_str());
  }

  for (uint8_t client = 0; client < MODULE_TRUSTED_CLIENTS; client++) {
    g_trusted[client] = false;
  }

  Serial.println("Module pairing: all module tokens forgotten");
}

inline void clearClient(uint8_t client) {
  if (client < MODULE_TRUSTED_CLIENTS) {
    g_trusted[client] = false;
  }
}

inline bool isTrusted(uint8_t client) {
  return client < MODULE_TRUSTED_CLIENTS && g_trusted[client];
}

// Кадр подтверждения связи от модуля. Возвращает true, если кадр разобран здесь
// и обработчику команд владельца его отдавать не нужно.
inline bool handleHello(uint8_t client, const char *payload, size_t length) {
  if (payload == nullptr || length < 16) {
    return false;
  }

  // Дешёвый отсев: остальные кадры — команды владельца.
  bool found = false;

  for (size_t i = 0; i + 12 <= length; i++) {
    if (memcmp(payload + i, "module_hello", 12) == 0) {
      found = true;
      break;
    }
  }

  if (!found) {
    return false;
  }

  static StaticJsonBuffer<512> jsonBuffer;
  jsonBuffer.clear();

  JsonObject &root = jsonBuffer.parseObject(const_cast<char *>(payload), length);

  if (!root.success() || root["event"] != "module_hello") {
    return false;
  }

  const char *token = root["data"]["token"];
  const char *mac = root["data"]["mac"];
  const char *fw = root["data"]["fw"];

  const bool trusted = token != nullptr &&
                       verifyToken(String(token), mac != nullptr ? String(mac) : String(""));

  if (client < MODULE_TRUSTED_CLIENTS) {
    g_trusted[client] = trusted;
  }

  String reply = "{\"event\":\"module_hello\",\"data\":{\"trusted\":";
  reply += trusted ? "1" : "0";
  reply += "}}";

  webSocket.sendTXT(client, reply);

  Serial.printf("Module hello: client %u mac %s fw %s trusted %d\n",
                static_cast<unsigned>(client),
                mac != nullptr ? mac : "-",
                fw != nullptr ? fw : "-",
                trusted ? 1 : 0);

  return true;
}

// Новые параметры точки доступа — только доверенным модулям.
inline uint8_t notifyCredentials() {
  String frame = "{\"event\":\"module_credentials\",\"data\":{";
  frame += "\"ssid\":\"" + APSSID1 + "\",";
  frame += "\"pass\":\"" + APPSK1 + "\",";
  frame += "\"host\":\"192.168.4.1\",";
  frame += "\"port\":81}}";

  uint8_t sent = 0;

  for (uint8_t client = 0; client < MODULE_TRUSTED_CLIENTS; client++) {
    if (!isTrusted(client) || !webSocket.clientIsConnected(client)) {
      continue;
    }

    webSocket.sendTXT(client, frame);
    sent++;
  }

  return sent;
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

// Окно сопряжения открыто. На это время основная точка доступа подменена открытой
// сетью DED_PAIR_*, поэтому «запрос пришёл из 192.168.4.0/24» больше не доказывает,
// что рядом владелец: к открытой сети может подключиться кто угодно в радиусе.
// Пока окно открыто, остальные проверки прав обязаны требовать ключ устройства.
inline bool devicePairingWindowOpen() {
  return module_pairing::active();
}

inline void registerModulePairingRoutes(AsyncWebServer *server) {
  // Модуль забирает параметры основной точки доступа. Маршрут живёт только
  // в сети сопряжения и только при открытом окне; после первого ответа окно
  // считается использованным.
  server->on("/module/pair", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!module_pairing::active() || !module_pairing::fromAccessPoint(request) ||
        module_pairing::g_pairServed) {
      request->send(403, "text/plain", "Pairing window is closed");
      return;
    }

    const AsyncWebHeader *macHeader = request->getHeader("X-Module-Mac");
    const String mac = macHeader != nullptr ? macHeader->value() : String("");
    const String token = module_pairing::issueToken(mac);

    if (token.length() != MODULE_TOKEN_LENGTH) {
      request->send(400, "text/plain", "Module MAC is required");
      return;
    }

    module_pairing::g_pairServed = true;

    Serial.print("Module pairing: module ");
    Serial.print(request->client()->remoteIP());
    Serial.println(" received the AP credentials");

    request->send(200, "application/json", module_pairing::payloadJson(token));

    // Основную точку доступа возвращаем с задержкой: ответ ещё должен уйти.
    module_pairing::scheduleRestore();
  });

  // Забыть сопряжённые модули: старые токены перестают работать сразу.
  server->on("/module/forget", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!module_pairing::changeAllowed(request)) {
      module_pairing::denyKey(request);
      return;
    }

    module_pairing::forgetModules();
    request->send(200, "application/json", "{\"event\":\"module_forget\",\"data\":{\"ok\":1}}");
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
