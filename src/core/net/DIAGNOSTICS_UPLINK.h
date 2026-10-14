#pragma once

// Удалённая диагностика со стороны блока: когда приложения владельца рядом нет, блок сам
// забирает команды оператора и отдаёт своё состояние.
//
// Почему отдельный модуль, а не вместе с проверкой обновлений: у диагностики другой ритм
// (раз в несколько секунд, пока доступ открыт) и другая цена ошибки — она забирает память
// у Bluetooth, поэтому работает только когда с приложением никто не связан.

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "core/net/SERVICE_CLIENT.h"
#include "core/net/DEVICE_COMMANDS.h"
#include "servers/WIFI_SERVER.h"

static unsigned long diagnosticsLastTick = 0;
static unsigned long diagnosticsStartedEpoch = 0;
static unsigned long diagnosticsLastState = 0;
static bool diagnosticsBleReleased = false;
static bool diagnosticsWasActive = false;

/// Включена ли удалённая диагностика по WiFi (владелец разрешил её из приложения).
bool diagnosticsUplinkEnabled() {
  return DIAGNOSTICS_WIFI_ENABLED;
}

/// Экранирование строки для JSON: в настройках кавычек нет, но имя сети может быть любым.
String diagnosticsEscape(const String &value) {
  String out;

  for (unsigned int index = 0; index < value.length(); index++) {
    char symbol = value[index];

    if (symbol == '"' || symbol == '\\') {
      out += '\\';
      out += symbol;
    } else if (symbol == '\n') {
      out += "\\n";
    } else {
      out += symbol;
    }
  }

  return out;
}

/// Снимок настроек блока: его видит оператор.
String diagnosticsSettingsJson() {
  String json = "{";

  json += "\"cart\":" + String(select_cart) + ",";
  json += "\"maxRpm\":" + String(max_rpm) + ",";
  json += "\"maxRpmKiker\":" + String(max_rpm_angle_kiker) + ",";
  json += "\"initAngle\":" + String(Init_angle) + ",";
  json += "\"initFlashTime\":" + String(Init_flash_time) + ",";
  json += "\"filterVal\":" + String(filter) + ",";
  json += "\"engineType\":" + String(type) + ",";
  json += "\"angleAdvance\":" + String(ANGLE_ADVANCE) + ",";
  json += "\"quickshifter\":" + String(QUICKSHIFTER) + ",";
  json += "\"cartsSwitching\":" + String(CARTS_SWHITCHING) + ",";
  json += "\"servoRpm\":" + String(Servo_rpm) + ",";
  json += "\"rpm\":" + String(RPM) + ",";
  json += "\"coolantC\":" + String(Termo);
  json += "}";

  return json;
}

/// Параметры команды, пришедшие строкой JSON.
JsonObject &diagnosticsParsePayload(const char *payloadJson) {
  static StaticJsonBuffer<512> payloadBuffer;

  payloadBuffer.clear();

  return payloadBuffer.parseObject(payloadJson == nullptr ? "{}" : payloadJson);
}

/// Кадр для обработчика команд: оператор присылает вид действия и его параметры.
bool diagnosticsBuildFrame(const String &kind, JsonObject &payload, String &frame) {
  if (kind == "ping") {
    frame = "{\"event\":\"ping\"}";
    return true;
  }

  if (kind == "map_read") {
    int cart = payload["cart"] | 1;
    int point = payload["point"] | 0;

    frame = "{\"event\":\"map_point\",\"data\":{\"cart\":" + String(cart) + ",\"point\":" + String(point) + "}}";
    return true;
  }

  if (kind == "map_write") {
    int cart = payload["cart"] | 1;
    int point = payload["point"] | 0;

    frame = "{\"event\":\"map_set\",\"data\":{\"cart\":" + String(cart) + ",\"point\":" + String(point);

    // Меняем только присланное: остальные поля точки остаются такими, как на блоке.
    if (payload.containsKey("rpm")) {
      frame += ",\"rpm\":" + String(payload["rpm"].as<float>());
    }
    if (payload.containsKey("angle")) {
      frame += ",\"angle\":" + String(payload["angle"].as<float>());
    }
    if (payload.containsKey("aa")) {
      frame += ",\"aa\":" + String(payload["aa"].as<float>());
    }
    if (payload.containsKey("qa")) {
      frame += ",\"qa\":" + String(payload["qa"].as<int>());
    }
    if (payload.containsKey("servo")) {
      frame += ",\"servo\":" + String(payload["servo"].as<float>());
    }

    frame += "}}";
    return true;
  }

  if (kind == "cart_select") {
    int cart = payload["cart"] | 1;

    frame = "{\"event\":\"cart\",\"data\":{\"cart\":" + String(cart) + "}}";
    return true;
  }

  if (kind == "servo_reset") {
    frame = "{\"event\":\"servo_error\"}";
    return true;
  }

  return false;
}

/// Выполнить команду оператора и вернуть результат. Пустой результат означает, что блок
/// применил команду, но ничего на неё не отвечает.
String diagnosticsRunCommand(const String &kind, JsonObject &payload, bool &failed, String &error) {
  failed = false;

  if (kind == "settings_read") {
    return diagnosticsSettingsJson();
  }

  if (kind == "servo_read") {
    return "{\"servoRpm\":" + String(Servo_rpm) + ",\"cart\":" + String(select_cart) + "}";
  }

  String frame;

  if (!diagnosticsBuildFrame(kind, payload, frame)) {
    failed = true;
    error = "блок не умеет: " + kind;

    return "";
  }

  String out;

  handleDeviceCommand(frame, out);

  return out.length() == 0 ? "{\"applied\":true}" : out;
}

/// Один запрос к сервису диагностики. Возвращает код ответа HTTP.
int diagnosticsRequest(WiFiClientSecure &client, const String &url, const String &body, String &response) {
  HTTPClient http;

  if (!serviceClientSetup(client, DIAGNOSTICS_TIMEOUT_MS)) {
    return -1;
  }

  http.begin(client, url);
  http.setTimeout(DIAGNOSTICS_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Key", otaManager.getDeviceKey());

  // Сторожевой таймер ядра 0 следит за бездействием: криптография держит ядро занятым
  // дольше пяти секунд, и блок перезагружался посреди рукопожатия.
  disableCore0WDT();

  int code = http.POST(body);
  response = code > 0 ? http.getString() : "";

  enableCore0WDT();
  http.end();

  return code;
}

/// Подключиться к домашней сети без точки доступа: в этом режиме AP не нужна, а её буферы
/// отнимают память у рукопожатия TLS.
void diagnosticsConnectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  if (WIFI_UPDATE_SSID.length() == 0) {
    Serial.println("remote diagnostics: no home network configured");

    return;
  }

  WiFi.begin(WIFI_UPDATE_SSID.c_str(), WIFI_UPDATE_PASSWORD.c_str());

  unsigned long started = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - started < WIFI_CONNECT_TIMEOUT_MS) {
    delay(200);
  }

  Serial.printf("remote diagnostics: wifi sta=%d free=%u maxAlloc=%u\n",
                (int) WiFi.status(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

/// Пора ли заканчивать режим: он ограничен по времени, иначе блок останется без приложения.
bool diagnosticsModeExpired() {
  if (diagnosticsStartedEpoch == 0) {
    diagnosticsStartedEpoch = preferences.getULong("diag_at", 0);
  }

  if (diagnosticsStartedEpoch == 0) {
    return false;
  }

  // Время берём из часов: millis() перезагрузку не переживает, а режим хранится в NVS.
  time_t now = time(nullptr);

  if (now < 1600000000) {
    return false;
  }

  return (unsigned long) now > diagnosticsStartedEpoch + DIAGNOSTICS_MAX_MINUTES * 60;
}

/// Выключить режим и перезагрузиться: обычная работа вернётся, Bluetooth поднимется снова.
void endDiagnosticsMode() {
  Serial.println("remote diagnostics: time is up, restarting");

  DIAGNOSTICS_WIFI_ENABLED = false;
  preferences.putBool("diag_en", false);
  preferences.putULong("diag_at", 0);

  delay(300);
  ESP.restart();
}

/// Один проход канала: забрать команды, выполнить их, отдать состояние.
void runDiagnosticsUplinkPass() {
  if (!DIAGNOSTICS_WIFI_ENABLED) {
    return;
  }

  // Режим живёт ограниченное время: блок должен вернуться к обычной работе, даже если
  // владелец забыл его выключить (приложение в этом режиме к блоку не подключится).
  if (diagnosticsModeExpired()) {
    endDiagnosticsMode();

    return;
  }

  if (WiFi.status() != WL_CONNECTED || otaManager.getDeviceKey().length() == 0) {
    return;
  }

  if (!diagnosticsWasActive) {
    diagnosticsWasActive = true;
    Serial.printf("remote diagnostics: started, free=%u maxAlloc=%u\n",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  }

  // Серийный номер — MAC в том же виде, в каком его заводит приложение (с двоеточиями).
  String serial = WiFi.macAddress();

  String base = String(DIAGNOSTICS_SERVER_URL) + "/api/telemetry/diagnostics/device/" + serial;
  WiFiClientSecure client;
  String response;

  int code = diagnosticsRequest(client, base + "/commands", "{}", response);

  if (code != HTTP_CODE_OK) {
    Serial.printf("remote diagnostics: commands failed, code=%d free=%u\n", code, ESP.getFreeHeap());
    return;
  }

  StaticJsonBuffer<4096> commandsBuffer;
  JsonArray &commands = commandsBuffer.parseArray(response);

  if (!commands.success()) {
    return;
  }

  for (JsonObject &command : commands) {
    const char *commandId = command["id"];
    const char *commandKind = command["kind"];

    if (commandId == nullptr || commandKind == nullptr) {
      continue;
    }

    String id = String(commandId);
    String kind = String(commandKind);
    JsonObject &payload = command["payloadJson"].is<const char *>()
                            ? diagnosticsParsePayload(command["payloadJson"].as<const char *>())
                            : commandsBuffer.createObject();

    bool failed = false;
    String error;
    String result = diagnosticsRunCommand(kind, payload, failed, error);

    String body = failed
                    ? "{\"errorText\":\"" + diagnosticsEscape(error) + "\",\"deliveredVia\":\"device\"}"
                    : "{\"resultJson\":\"" + diagnosticsEscape(result) + "\",\"deliveredVia\":\"device\"}";

    Serial.printf("remote diagnostics: %s -> %s\n", kind.c_str(), failed ? error.c_str() : "ok");

    diagnosticsRequest(client, base + "/commands/" + id + "/result", body, response);
  }

  // Состояние отправляем реже: оно меняется медленно, а каждый запрос — это рукопожатие TLS.
  if (millis() - diagnosticsLastState >= DIAGNOSTICS_STATE_MS) {
    diagnosticsLastState = millis();

    String stateBody = "{\"settingsJson\":\"" + diagnosticsEscape(diagnosticsSettingsJson()) +
                       "\",\"bridge\":\"device\"}";

    diagnosticsRequest(client, base + "/state", stateBody, response);
  }
}

/// Задача канала: рукопожатие TLS требует много стека (буферы mbedTLS), поэтому канал
/// живёт отдельной задачей, а не в основном цикле — там стек задачи переполнялся.
void TaskDiagnosticsCode(void * parameter) {
  diagnosticsStartedEpoch = preferences.getULong("diag_at", 0);

  diagnosticsConnectWifi();

  for (;;) {
    runDiagnosticsUplinkPass();
    vTaskDelay(DIAGNOSTICS_TICK_MS / portTICK_PERIOD_MS);
  }
}
