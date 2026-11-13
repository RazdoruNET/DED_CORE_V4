#ifndef DEVICE_API_ROUTES_H
#define DEVICE_API_ROUTES_H

// HTTP API устройства на порту 80 — то, чем управляет вшитый веб-интерфейс и внешние
// интеграции (см. docs/api/README.md).
//
// Маршруты жили в WEB_SERVER.h, но тот файл перестали подключать при рефакторинге
// (WEB_SERVER_CLEAN.h объявляет только функции, а определения живут в WEB_SERVER_FUNCS.h),
// и весь API оказался мёртвым: встроенная страница открывалась, но сохранить настройки
// не могла — запросы уходили на 404. Здесь маршруты собраны заново и регистрируются
// из initServer().
//
// Параметры читаются через hasParam: страница обращается к /modulator тремя отдельными
// запросами (filter, time, high), а к /sensor и /throttle — только с invert, поэтому
// безусловное чтение параметра уронило бы устройство.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include "core/storage/DEVICE_FS.h"
#include <ESPAsyncWebServer.h>
#include "config/PROPS.h"
#include "core/ignition/carts/MOTO_CARTS.h"
#include "core/ota/HYBRID_OTA.h"
// Границы настроек, clampSetting и jsonEscape живут здесь.
#include "core/net/DEVICE_COMMANDS.h"

extern HybridOTAManager otaManager;

// Определение — core/net/DEVICE_COMMANDS.h (подключается раньше в основном файле).
void applyApSettings(const String &ssid, const String &pass);

// --- Проверка права на изменение состояния ------------------------------------
//
// Пароль точки доступа — не аутентификация: его знает каждый, кто подключился к сети
// блока, а раньше он вообще был зашит в прошивку. Поэтому действия, которые меняют
// настройки, обновление или питание, требуют ключ устройства (`X-Device-Key`).
//
// Пока ключ не выдан (блок не привязан), менять настройки разрешено только из сети
// самого блока — иначе первый же владелец не смог бы ничего настроить до привязки.
// Как только ключ появился, путь «из подсети» для опасных действий закрывается.

// Сравнение без раннего выхода: длина ключа известна атакующему и так, а посимвольный
// возврат на первом несовпадении даёт лишний канал.
bool dedCoreKeyEquals(const String &a, const String &b) {
  if (a.length() == 0 || a.length() != b.length()) {
    return false;
  }

  unsigned char diff = 0;

  for (size_t i = 0; i < a.length(); i++) {
    diff |= static_cast<unsigned char>(a[i] ^ b[i]);
  }

  return diff == 0;
}

bool dedCoreKeyMatches(AsyncWebServerRequest *request) {
  String deviceKey = otaManager.getDeviceKey();

  if (deviceKey.length() == 0 || !request->hasHeader("X-Device-Key")) {
    return false;
  }

  return dedCoreKeyEquals(request->getHeader("X-Device-Key")->value(), deviceKey);
}

// Определение — core/net/MODULE_PAIRING.h: модуль сопряжения подключается позже,
// поэтому объявляем проверку здесь.
bool devicePairingWindowOpen();

bool dedCoreFromAccessPoint(AsyncWebServerRequest *request) {
  // Во время окна сопряжения модулей сеть блока открыта — адрес клиента больше ничего
  // не доказывает, и путь «из сети блока» закрывается до конца окна.
  if (devicePairingWindowOpen()) {
    return false;
  }

  // Точный признак «запрос пришёл через точку доступа»: локальный адрес соединения
  // совпадает с адресом softAP. Проверка по подсети клиента ошибалась, если домашняя
  // сеть тоже использует 192.168.4.0/24.
  return request->client()->localIP() == WiFi.softAPIP();
}

/// Действие меняет состояние: нужен ключ, если он уже выдан.
bool dedCoreChangeAllowed(AsyncWebServerRequest *request) {
  if (otaManager.getDeviceKey().length() > 0) {
    return dedCoreKeyMatches(request);
  }

  return dedCoreFromAccessPoint(request);
}

/// Чувствительное чтение (пароль точки доступа, код сопряжения): только по ключу.
bool dedCoreReadSecretAllowed(AsyncWebServerRequest *request) {
  return dedCoreKeyMatches(request);
}

void dedCoreDenyKey(AsyncWebServerRequest *request) {
  request->send(403, "text/plain", "Нужен ключ устройства (X-Device-Key)");
}

// Долгие операции (рукопожатие TLS, скачивание и запись образа) нельзя выполнять в
// задаче AsyncTCP: её стека не хватает на mbedTLS, а сама задача на всё это время
// перестаёт обслуживать HTTP и WebSocket. Поэтому работа уходит в свою задачу с
// большим стеком, а клиент получает ответ сразу.
typedef void (*DeviceJobFn)();

void deviceJobEntry(void *argument) {
  DeviceJobFn job = reinterpret_cast<DeviceJobFn>(argument);

  if (job != nullptr) {
    job();
  }

  vTaskDelete(nullptr);
}

bool startDeviceJob(DeviceJobFn job, const char *name, uint32_t stack) {
  return xTaskCreatePinnedToCore(deviceJobEntry, name, stack, reinterpret_cast<void *>(job), 1, nullptr, 0) == pdPASS;
}

// Стек как у задачи обновлений (core/tasks/OtaTask.h): рукопожатию TLS и разбору
// сертификатов нужно заметно больше, чем у задачи AsyncTCP.
#define DEVICE_JOB_STACK 24576

void otaCheckJob() {
  otaManager.checkForUpdate();
  Serial.printf("OTA: check finished, updateAvailable=%d\n", otaManager.getLastFirmwareInfo().updateAvailable ? 1 : 0);
}

void otaUpdateJob() {
  // Успех завершается перезагрузкой внутри performUpdate().
  if (!otaManager.performUpdate()) {
    Serial.println("OTA: update from the page failed");
  }
}

// Одна и та же проверка у каждого маршрута, меняющего состояние: раньше она стояла
// только у части из них, и настройки зажигания правились в обход ключа устройства.
#define DED_CORE_REQUIRE_CHANGE(request) \
  if (!dedCoreChangeAllowed(request)) { \
    dedCoreDenyKey(request); \
    return; \
  }

/// Регистрирует API устройства на переданном сервере (обычный HTTP-сервер, порт 80).
void registerDeviceApiRoutes(AsyncWebServer *server) {
server->on("/card", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    DED_CORE_REQUIRE_CHANGE(request);

    if (request->hasParam("id")) 
    {
       // Номер карты в API и в команде WebSocket — 1..3, в памяти карты лежат по
       // индексу 0..2. Значение вне диапазона попадало в индекс массива карт.
       int cart = request->getParam("id")->value().toInt();

       if (cart < 1 || cart > 3) 
       {
         request->send(400, "text/plain", "id must be 1..3");
         return;
       }

       active_cart = cart - 1;
       select_cart = cart;
    }
    
    request->send(200, "text/plain", "OK");
  });

server->on("/update_card", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    DED_CORE_REQUIRE_CHANGE(request);

    Serial.println("update_card");
    
    if (request->hasParam("id") && request->hasParam("rpm") && request->hasParam("angle") && request->hasParam("angle_adv") && request->hasParam("quick_adv") && request->hasParam("servo")) 
    {
      int id = (request->getParam("id")->value().toInt());

      // Индекс точки приходит от клиента, а active_cart — ещё и состояние блока:
      // оба обязаны попасть в границы массива карт, иначе запись уходит за его конец.
      if (id < 0 || id >= MAP_POINTS_MAX || active_cart < 0 || active_cart >= 3)
      {
        request->send(400, "text/plain", "bad index");
        return;
      }

      // Диапазоны те же, что у команды map_set: карта управляет зажиганием
      // работающего двигателя, и значение за пределами — это детонация.
      int rpm = clampSetting(request->getParam("rpm")->value().toInt(), SETTINGS_MAP_RPM_MIN, SETTINGS_MAX_RPM_MAX);
      int quick_adv = clampSetting(request->getParam("quick_adv")->value().toInt(), SETTINGS_QUICK_SHIFT_MIN, SETTINGS_QUICK_SHIFT_MAX);
      float angle = constrain(request->getParam("angle")->value().toFloat(), (float) SETTINGS_ANGLE_MIN, (float) SETTINGS_ANGLE_MAX);
      int angle_adv = clampSetting((int) request->getParam("angle_adv")->value().toFloat(), SETTINGS_ANGLE_MIN, SETTINGS_ANGLE_MAX);
      int servo = clampSetting((int) request->getParam("servo")->value().toFloat(), SETTINGS_SERVO_PERCENT_MIN, SETTINGS_SERVO_PERCENT_MAX);
      
      rpmsArr[active_cart][id] = rpm;
      anglesArr[active_cart][id] = angle;
      angles_adjustmentArr[active_cart][id] = angle_adv;
      quick_adjustmentArr[active_cart][id] = quick_adv;
      servo_valueArr[active_cart][id] = servo;
      
      String x = String(active_cart);
      String y = String(id);
      
      String rpm_chars = "rpm"+x+"_"+y;
      String angles_chars = "angles"+x+"_"+y;
      String angles_adjustment_chars = "aa"+x+"_"+y;
      String quick_adjustment_chars = "qa"+x+"_"+y;
      String addres_servo_chars = "servo"+x+"_"+y;

      const char* addres_rpm = rpm_chars.c_str();
      const char* addres_angles = angles_chars.c_str();
      const char* addres_angles_adjustment = angles_adjustment_chars.c_str();
      const char* addres_quick_adjustment = quick_adjustment_chars.c_str();
      const char* addres_servo = addres_servo_chars.c_str();
      
      preferences.putInt(addres_rpm,(int) rpm * 100);
      
      Serial.print(addres_rpm);
      Serial.println(rpm);
      
      preferences.putInt(addres_angles,(int) angle * 100);

      Serial.print(addres_angles);
      Serial.println(angle);
      
      preferences.putInt(addres_angles_adjustment,(int) angle_adv * 100);

      Serial.print(addres_angles_adjustment);
      Serial.println(angle_adv);
      
      preferences.putInt(addres_quick_adjustment,(int) quick_adv * 100);

      Serial.print(addres_quick_adjustment);
      Serial.println(quick_adv);


      preferences.putInt(addres_servo,servo);

      Serial.print(addres_servo);
      Serial.println(servo);
      
      Serial.println("SAVE");
    }
  
    request->send(200, "text/plain", "OK");
  });

server->on("/max", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    DED_CORE_REQUIRE_CHANGE(request);

    if (request->hasParam("rpm") && request->hasParam("angle")) 
    {
       // 0 — «отсечка не настроена»; выше 20000 двигатель этого класса не крутится.
       int rpm = clampSetting(request->getParam("rpm")->value().toInt(), 0, SETTINGS_MAX_RPM_MAX);
       int angle = clampSetting(request->getParam("angle")->value().toInt(), SETTINGS_ANGLE_MIN, SETTINGS_ANGLE_MAX);
       max_rpm = rpm;
       max_rpm_angle_kiker = angle;

       preferences.putInt("max_rpm",max_rpm);
       preferences.putInt("max_rpm_ang_k",max_rpm_angle_kiker);
    }
    
    request->send(200, "text/plain", "OK");
  });

server->on("/angle/advance", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    DED_CORE_REQUIRE_CHANGE(request);

    if (request->hasParam("status")) 
    {
       ANGLE_ADVANCE = (request->getParam("status")->value().toInt() == 1);

       preferences.putBool("ANGLE_ADV",ANGLE_ADVANCE);
    }
    request->send(200, "text/plain", "OK");
  });

server->on("/angle", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    DED_CORE_REQUIRE_CHANGE(request);

    if (request->hasParam("init")) 
    {
       // Метка ротора бывает и 120° (см. ignition/MARK_SCAN.h), но не отрицательной
       // и не больше оборота.
       int angle = clampSetting(request->getParam("init")->value().toInt(), 0, 360);
       Init_angle = angle;
       preferences.putInt("Init_angle",Init_angle);
    }
    request->send(200, "text/plain", "OK");
  });

server->on("/type", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    DED_CORE_REQUIRE_CHANGE(request);

    if (request->hasParam("set") && request->hasParam("new_chart_type")) 
    {
      // type — рабочее число точек карты (7/12/22), всё остальное здесь не разбирается.
      type = clampSetting(request->getParam("set")->value().toInt(), 0, 2);

      int new_chart_type = request->getParam("new_chart_type")->value().toInt();

      // Прежний начальный угол нужен для «своей» карты (new_chart_type вне 1..6).
      int angle_result = Init_angle / 2;

      preferences.putInt("type", type);

      // Сетка оборотов выбирается по типу карты, а у 166/172FMM и 175FMM она своя,
      // даже когда выбрано семь точек.
      float (*rpmSource)[22] = rpmsArr_0;

      if (type == 1)
      {
        rpmSource = rpmsArr_1;
      }
      else if (type == 2)
      {
        rpmSource = rpmsArr_2;
      }
      else if (new_chart_type == 4)
      {
        rpmSource = rpmsArr_4;
      }
      else if (new_chart_type == 6)
      {
        rpmSource = rpmsArr_3;
      }

      // Паспортная карта углов: индекс совпадает с new_chart_type страницы.
      float (*angleSource)[22] = nullptr;

      switch (new_chart_type)
      {
        case 1: angleSource = anglesArr_yx125;    break;
        case 2: angleSource = anglesArr_yx140;    break;
        case 3: angleSource = anglesArr_smartcom; break;
        case 4: angleSource = anglesArr_172fmm;   break;
        case 5: angleSource = anglesArr_zs190;    break;
        case 6: angleSource = anglesArr_175fmm;   break;
      }

      for (int c = 0; c < 3; c++)
      {
        for (int i = 0; i < 22; i++)
        {
          rpmsArr[c][i] = rpmSource[c][i];

          if (angleSource != nullptr)
          {
            anglesArr[c][i] = angleSource[c][i];
          }
          else
          {
            // Карта «по начальному углу»: первая точка ниже, остальные выше.
            anglesArr[c][i] = (i == 0) ? (angle_result - 2) : (angle_result + 1);
            angles_adjustmentArr[c][i] = 0;
            quick_adjustmentArr[c][i] = 0;
            servo_valueArr[c][i] = 0;
          }
        }
      }

      // Начальный угол под паспортную карту.
      switch (new_chart_type)
      {
        case 1:
        case 2:
        case 4:
          Init_angle = 35;
        break;
        case 3:
          Init_angle = 43;
        break;
        case 5:
          Init_angle = 17;
        break;
        case 6:
          Init_angle = 80;
        break;
      }

      // Карта — в NVS: ключи обязаны совпадать с загрузкой в config/CONFIG.h.
      for (int c = 0; c < 3; c++)
      {
        for (int id = 0; id < 22; id++)
        {
          String suffix = String(c) + "_" + String(id);

          preferences.putInt(("rpm" + suffix).c_str(),    (int)(rpmsArr[c][id] * 100));
          preferences.putInt(("angles" + suffix).c_str(), (int)(anglesArr[c][id] * 100));
          preferences.putInt(("aa" + suffix).c_str(),     angles_adjustmentArr[c][id] * 100);
          preferences.putInt(("qa" + suffix).c_str(),     quick_adjustmentArr[c][id] * 100);
          preferences.putInt(("servo" + suffix).c_str(),  servo_valueArr[c][id]);
        }
      }

      preferences.putInt("Init_angle", Init_angle);
    }
     
    request->send(200, "text/plain", "OK");
  });


server->on("/flash", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    if (!dedCoreChangeAllowed(request)) {
      dedCoreDenyKey(request);
      return;
    }

    AFTER_REBOOT = false;
    SAVE_CONFIG = true;
    request->send(200, "text/plain", "OK");
  });

// Маршрут /wifi/updator убран: он писал APSSID/APPSK, которые не используются ни
// точкой доступа (у неё APSSID1/APPSK1), ни остальной прошивкой, — то есть не делал
// ничего. Имя и пароль точки доступа меняет /wifi.

server->on("/wifi", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    if (!dedCoreChangeAllowed(request)) {
      dedCoreDenyKey(request);
      return;
    }

    if (request->hasParam("ssid")) 
    {
       // Через общий обработчик: он ставит признак смены, по которому
       // сопряжённые модули получают новые параметры до перезапуска сети.
       applyApSettings(request->getParam("ssid")->value(),
                       request->hasParam("appsk") ? request->getParam("appsk")->value() : String(""));
    }
    request->send(200, "text/plain", "OK");
  });

server->on("/wifi/auto", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    if (!dedCoreChangeAllowed(request)) {
      dedCoreDenyKey(request);
      return;
    }

    if (request->hasParam("ssid")) {
      WIFI_UPDATE_SSID = request->getParam("ssid")->value();
      preferences.putString("wifi_ssid", WIFI_UPDATE_SSID);
    }

    if (request->hasParam("pass")) {
      WIFI_UPDATE_PASSWORD = request->getParam("pass")->value();
      preferences.putString("wifi_pass", WIFI_UPDATE_PASSWORD);
    }

    if (request->hasParam("enabled")) {
      WIFI_AUTO_UPDATE_ENABLED = (request->getParam("enabled")->value().toInt() == 1);
      preferences.putBool("wifi_auto", WIFI_AUTO_UPDATE_ENABLED);
    }

    request->send(200, "application/json", "{\"enabled\":" + String(WIFI_AUTO_UPDATE_ENABLED ? "true" : "false") + ",\"ssid\":\"" + jsonEscape(WIFI_UPDATE_SSID) + "\",\"status\":\"" + jsonEscape(WIFI_UPDATE_STATUS) + "\",\"pass_set\":" + String(WIFI_UPDATE_PASSWORD.length() > 0 ? "true" : "false") + ",\"connected\":" + String(WiFi.status() == WL_CONNECTED && WiFi.SSID() == WIFI_UPDATE_SSID ? "true" : "false") + ",\"last_check_time\":" + String(WIFI_LAST_CHECK_TIME) + ",\"last_attempt_time\":" + String(WIFI_LAST_ATTEMPT_TIME) + ",\"retry_interval_ms\":" + String(WIFI_RETRY_INTERVAL_MS) + ",\"next_allowed_check_ms\":" + String((WIFI_LAST_ATTEMPT_TIME==0)?0:((WIFI_LAST_ATTEMPT_TIME+WIFI_RETRY_INTERVAL_MS)-millis())) + "}");
  });

server->on("/status", HTTP_GET, [] (AsyncWebServerRequest *request) {
    bool sta_connected = (WiFi.status() == WL_CONNECTED && WiFi.SSID() == WIFI_UPDATE_SSID);
    FirmwareInfo info = otaManager.getLastFirmwareInfo();

    String response = "{";
    response += "\"wifi\":{";
    response += "\"mode\":\"" + String((WiFi.getMode() == WIFI_MODE_AP) ? "ap" : (WiFi.getMode() == WIFI_MODE_STA ? "sta" : "ap_sta")) + "\",";
    response += "\"ap_ssid\":\"" + jsonEscape(APSSID1) + "\",";
    response += "\"sta_ssid\":\"" + jsonEscape(WIFI_UPDATE_SSID) + "\",";
    response += "\"sta_connected\":" + String(sta_connected ? "true" : "false") + ",";
    response += "\"status\":\"" + jsonEscape(WIFI_UPDATE_STATUS) + "\",";
    response += "\"last_check_time\":" + String(WIFI_LAST_CHECK_TIME) + ",";
    response += "\"last_attempt_time\":" + String(WIFI_LAST_ATTEMPT_TIME) + ",";
    unsigned long next_check = 0;
    if (WIFI_LAST_ATTEMPT_TIME != 0) next_check = (WIFI_LAST_ATTEMPT_TIME + WIFI_RETRY_INTERVAL_MS > millis()) ? (WIFI_LAST_ATTEMPT_TIME + WIFI_RETRY_INTERVAL_MS - millis()) : 0;
    response += "\"next_check_ms\":" + String(next_check);
    response += "},";

    response += "\"ota\":{";
    response += "\"currentVersion\":\"" + otaManager.getCurrentVersion() + "\",";
    response += "\"mode\":\"" + otaManager.getModeString() + "\",";
    response += "\"initialized\":" + String(otaManager.isInitialized() ? "true" : "false") + ",";
    response += "\"updateMode\":" + String(UPDATE_MODE ? "true" : "false") + ",";
    bool updateAvailable = otaManager.getLastFirmwareInfo().updateAvailable;
    response += "\"updateAvailable\":" + String(updateAvailable ? "true" : "false") + ",";
    response += "\"lastFirmwareInfo\":{";
    response += "\"version\":\"" + info.version + "\",";
    response += "\"size\":\"" + String(info.size) + "\",";
    response += "\"md5\":\"" + info.md5 + "\",";
    response += "\"sha256\":\"" + info.sha256 + "\",";
    response += "\"changelog\":\"" + info.changelog + "\"";
    response += "}";
    response += "},";

    response += "\"motor\":{";
    response += "\"rpm\":" + String(RPM) + ",";
    response += "\"throttle\":" + String(Throttle) + ",";
    response += "\"motor_running\":" + String(RPM > 0 ? "true" : "false");
    response += "},";

    response += "\"config\":{";
    response += "\"autoUpdateEnabled\":" + String(WIFI_AUTO_UPDATE_ENABLED ? "true" : "false") + ",";
    response += "\"wifiUpdateSSID\":\"" + jsonEscape(WIFI_UPDATE_SSID) + "\",";
    response += "\"wifiPasswordSet\":" + String(WIFI_UPDATE_PASSWORD.length() > 0 ? "true" : "false") + ",";
    response += "\"wifiConnected\":" + String(sta_connected ? "true" : "false") + ",";
    response += "\"retryIntervalMs\":" + String(WIFI_RETRY_INTERVAL_MS) + ",";
    response += "\"nextCheckMs\":" + String(next_check);
    response += "}";

    response += "}";

    request->send(200, "application/json", response);
  });

server->on("/cart/types", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String resp = "{";
    resp += "\"types\":[";
    resp += "{\"id\":0,\"label\":\"7-point default\",\"points\":7},";
    resp += "{\"id\":1,\"label\":\"12-point medium\",\"points\":12},";
    resp += "{\"id\":2,\"label\":\"22-point large\",\"points\":22}";
    resp += "]}";
    request->send(200, "application/json", resp);
  });

server->on("/device/config", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String resp = "{";
    resp += "\"activeCart\":" + String(active_cart) + ",";
    resp += "\"type\":" + String(type) + ",";
    resp += "\"autoUpdateEnabled\":" + String(WIFI_AUTO_UPDATE_ENABLED ? "true" : "false") + ",";
    resp += "\"wifiUpdateSSID\":\"" + jsonEscape(WIFI_UPDATE_SSID) + "\",";
    resp += "\"wifiPasswordSet\":" + String(WIFI_UPDATE_PASSWORD.length() > 0 ? "true" : "false") + ",";
    resp += "\"quickShifterEnabled\":" + String(QUICKSHIFTER ? "true" : "false") + ",";
    resp += "\"angleAdvance\":" + String(ANGLE_ADVANCE ? "true" : "false") + ",";
    resp += "\"servoEnabled\":" + String(SERVO ? "true" : "false") + ",";
    resp += "\"servoType\":" + String(Servo_type) + ",";
    resp += "\"servoRpm\":" + String(Servo_rpm) + ",";
    resp += "\"servoPercent\":" + String(Servo_percent) + ",";
    resp += "\"invertSensor\":" + String(INVERT_SENSOR ? "true" : "false") + ",";
    resp += "\"invertThrottle\":" + String(INVERT_THROTTLE ? "true" : "false") + ",";
    resp += "\"maxRpm\":" + String(max_rpm) + ",";
    resp += "\"maxRpmAngleKicker\":" + String(max_rpm_angle_kiker);
    resp += "}";
    request->send(200, "application/json", resp);
  });

server->on("/wifi/scan", HTTP_GET, [] (AsyncWebServerRequest *request) {
    int n = WiFi.scanNetworks();
    String resp = "{";
    resp += "\"networks\": [";
    for (int i = 0; i < n; ++i) {
      resp += "{";
      resp += "\"ssid\":\"" + jsonEscape(WiFi.SSID(i)) + "\",";
      resp += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
      resp += "\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + ",";
      resp += "\"channel\":" + String(WiFi.channel(i));
      resp += "}";
      if (i < n-1) resp += ",";
    }
    resp += "]}";

    // Список сетей держит память в драйвере: без scanDelete он остаётся до
    // следующего сканирования и отнимает кусок кучи у TLS.
    WiFi.scanDelete();
    request->send(200, "application/json", resp);
  });

server->on("/wifi/connect", HTTP_GET, [] (AsyncWebServerRequest *request) {
    if (!dedCoreChangeAllowed(request)) {
      dedCoreDenyKey(request);
      return;
    }

    if (request->hasParam("ssid")) {
      WIFI_UPDATE_SSID = request->getParam("ssid")->value();
      preferences.putString("wifi_ssid", WIFI_UPDATE_SSID);
    }
    if (request->hasParam("pass")) {
      WIFI_UPDATE_PASSWORD = request->getParam("pass")->value();
      preferences.putString("wifi_pass", WIFI_UPDATE_PASSWORD);
    }

    // Подключение не ждём: обработчик работает в задаче AsyncTCP, и пятнадцать
    // секунд ожидания здесь останавливали бы весь HTTP и WebSocket. Состояние
    // клиент видит в /status и в кадре телеметрии.
    bool started = false;
    if (WIFI_UPDATE_SSID.length() > 0) {
      started = true;
      WiFi.begin(WIFI_UPDATE_SSID.c_str(), WIFI_UPDATE_PASSWORD.c_str());
    }
    bool connected = (WiFi.status() == WL_CONNECTED && WiFi.SSID() == WIFI_UPDATE_SSID);

    String out = "{";
    out += "\"started\":" + String(started ? "true" : "false") + ",";
    out += "\"target_ssid\":\"" + jsonEscape(WIFI_UPDATE_SSID) + "\",";
    out += "\"status\":\"" + String(connected ? "connected" : "connecting") + "\",";
    out += "\"passwordSet\":" + String(WIFI_UPDATE_PASSWORD.length() > 0 ? "true" : "false") + ",";
    out += "\"connected\":" + String((WiFi.status() == WL_CONNECTED && WiFi.SSID() == WIFI_UPDATE_SSID) ? "true" : "false") + "";
    out += "}";
    request->send(200, "application/json", out);
  });

server->on("/quickshifter", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    DED_CORE_REQUIRE_CHANGE(request);

    if (request->hasParam("status")) 
    {
       QUICKSHIFTER = (request->getParam("status")->value().toInt() == 1);
       preferences.putBool("QUICKSHIFT",QUICKSHIFTER);
    }
    request->send(200, "text/plain", "OK");
  });

server->on("/modulator", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    DED_CORE_REQUIRE_CHANGE(request);

    if (request->hasParam("time")) 
    {
      Init_flash_time = clampSetting(request->getParam("time")->value().toInt(), SETTINGS_FLASH_TIME_MIN, SETTINGS_FLASH_TIME_MAX);
      preferences.putInt("flash_time",Init_flash_time);
    }
    else if (request->hasParam("filter")) 
    {
      filter = clampSetting(request->getParam("filter")->value().toInt(), SETTINGS_FILTER_MIN, SETTINGS_FILTER_MAX);
      preferences.putInt("filter",filter);
    }
    else if (request->hasParam("high")) 
    {
      HIGH_OUTPUT = (request->getParam("high")->value().toInt() == 1);
      preferences.putInt("HIGH_OUTPUT",HIGH_OUTPUT);
    }
    else
    {
      // Ответ на 404 отправляется здесь же: раньше следом уходил и 200, то есть
      // клиент получал два ответа на один запрос.
      request->send(404, "text/plain", "404");
      return;
    }

    request->send(200, "text/plain", "OK");
  });

server->on("/sensor", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    DED_CORE_REQUIRE_CHANGE(request);

    if (request->hasParam("invert")) 
    {
       INVERT_SENSOR = (request->getParam("invert")->value().toInt() == 1);
       preferences.putBool("INVERT_SENS",INVERT_SENSOR);
    }
    request->send(200, "text/plain", "OK");
  });

server->on("/carts/switching", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    DED_CORE_REQUIRE_CHANGE(request);

    if (request->hasParam("status")) 
    {
       CARTS_SWHITCHING = (request->getParam("status")->value().toInt() == 1);
       preferences.putBool("CART_SWHITCH",CARTS_SWHITCHING);
    }
    request->send(200, "text/plain", "OK");
  });

server->on("/servo", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    DED_CORE_REQUIRE_CHANGE(request);

    if (request->hasParam("status")) 
    {
       SERVO = (request->getParam("status")->value().toInt() == 1);
       preferences.putBool("SERVO",SERVO);
    }

    // Параметры читаем только если они переданы: страница шлёт их вместе со status,
    // но `getParam()->value()` на отсутствующем параметре — это nullptr.
    if (request->hasParam("servo_type")) 
    {
       Servo_type = clampSetting(request->getParam("servo_type")->value().toInt(), 0, SETTINGS_SERVO_TYPE_MAX);
       preferences.putInt("servo_type",Servo_type);
    }
   
    if (request->hasParam("servo_rpm")) 
    {
       Servo_rpm = clampSetting(request->getParam("servo_rpm")->value().toInt(), SETTINGS_SERVO_RPM_MIN, SETTINGS_SERVO_RPM_MAX);
       preferences.putInt("servo_rpm",Servo_rpm);
    }
    
    if (request->hasParam("servo_percent")) 
    {
       Servo_percent = clampSetting(request->getParam("servo_percent")->value().toInt(), SETTINGS_SERVO_PERCENT_MIN, SETTINGS_SERVO_PERCENT_MAX);
       preferences.putInt("servo_perc",Servo_percent);
    }
    
    request->send(200, "text/plain", "OK");
  });

server->on("/servo_test", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    DED_CORE_REQUIRE_CHANGE(request);

    if (request->hasParam("status")) 
    {
       SERVO = (request->getParam("status")->value().toInt() == 1);
       preferences.putBool("SERVO",SERVO);
    }
    request->send(200, "text/plain", "OK");
  });

server->on("/throttle", HTTP_GET, [] (AsyncWebServerRequest *request) 
  {
    DED_CORE_REQUIRE_CHANGE(request);

    if (request->hasParam("invert")) 
    {
       INVERT_THROTTLE = (request->getParam("invert")->value().toInt() == 1);
       preferences.putBool("INVERT_THROT",INVERT_THROTTLE);
    }
    request->send(200, "text/plain", "OK");
  });

server->on("/ota/check", HTTP_GET, [] (AsyncWebServerRequest *request) {
    if (RPM > 100) {
      request->send(409, "text/plain", "Двигатель работает — проверка отложена");
      return;
    }

    // Проверка — это рукопожатие TLS: в задаче AsyncTCP его стек и куча не выдержат,
    // а ожидание часов заняло бы здесь до двадцати секунд. Запускаем в своей задаче
    // и отвечаем тем, что уже известно: свежий ответ придёт кадром телеметрии.
    if (!startDeviceJob(otaCheckJob, "TaskOtaCheck", DEVICE_JOB_STACK)) {
      request->send(503, "text/plain", "Не хватает памяти на проверку обновлений");
      return;
    }

    FirmwareInfo info = otaManager.getLastFirmwareInfo();

    String response = "{";
    response += "\"checking\":true,";
    response += "\"updateAvailable\":" + String(info.updateAvailable ? "true" : "false") + ",";
    response += "\"currentVersion\":\"" + otaManager.getCurrentVersion() + "\",";
    response += "\"mode\":\"" + otaManager.getModeString() + "\"";

    if (info.updateAvailable) {
      response += ",\"newVersion\":\"" + jsonEscape(info.version) + "\",";
      response += "\"size\":" + String(info.size) + ",";
      response += "\"md5\":\"" + jsonEscape(info.md5) + "\",";
      response += "\"sha256\":\"" + jsonEscape(info.sha256) + "\",";
      response += "\"changelog\":\"" + jsonEscape(info.changelog) + "\"";
    }

    response += "}";
    request->send(202, "application/json", response);
  });

server->on("/ota/status", HTTP_GET, [] (AsyncWebServerRequest *request) {
    FirmwareInfo info = otaManager.getLastFirmwareInfo();
    bool updateAvailable = info.updateAvailable;
    bool connected = (WiFi.status() == WL_CONNECTED && WiFi.SSID() == WIFI_UPDATE_SSID);

    String response = "{";
    response += "\"currentVersion\":\"" + otaManager.getCurrentVersion() + "\",";
    response += "\"mode\":\"" + otaManager.getModeString() + "\",";
    response += "\"initialized\":" + String(otaManager.isInitialized() ? "true" : "false") + ",";
    response += "\"updateMode\":" + String(UPDATE_MODE ? "true" : "false") + ",";
    response += "\"updateAvailable\":" + String(updateAvailable ? "true" : "false") + ",";
    response += "\"lastFirmwareInfo\":{";
    response += "\"version\":\"" + jsonEscape(info.version) + "\",";
    response += "\"size\":" + String(info.size) + ",";
    response += "\"md5\":\"" + jsonEscape(info.md5) + "\",";
    response += "\"sha256\":\"" + jsonEscape(info.sha256) + "\",";
    response += "\"changelog\":\"" + jsonEscape(info.changelog) + "\"";
    response += "},";
    response += "\"wifiAutoUpdateEnabled\":" + String(WIFI_AUTO_UPDATE_ENABLED ? "true" : "false") + ",";
    response += "\"wifiUpdateSSID\":\"" + jsonEscape(WIFI_UPDATE_SSID) + "\",";
    response += "\"wifiConnected\":" + String(connected ? "true" : "false") + ",";
    response += "\"lastCheckTime\":" + String(WIFI_LAST_CHECK_TIME) + ",";
    response += "\"lastAttemptTime\":" + String(WIFI_LAST_ATTEMPT_TIME) + ",";
    response += "\"retryIntervalMs\":" + String(WIFI_RETRY_INTERVAL_MS);
    response += "}";
    request->send(200, "application/json", response);
  });

server->on("/ota/update", HTTP_POST, [] (AsyncWebServerRequest *request) {
    DED_CORE_REQUIRE_CHANGE(request);

    // Запись образа останавливает управление зажиганием на всё время загрузки.
    if (RPM > 100) {
      request->send(409, "text/plain", "Двигатель работает — обновление отложено");
      return;
    }

    if (otaManager.isUpdating()) {
      request->send(409, "text/plain", "Обновление уже идёт");
      return;
    }

    // Скачивание и запись идут в отдельной задаче: в задаче AsyncTCP это остановило бы
    // весь HTTP и WebSocket на всё время обновления. Успешная запись сама перезагрузит блок.
    if (!startDeviceJob(otaUpdateJob, "TaskOtaUpdate", DEVICE_JOB_STACK)) {
      request->send(503, "text/plain", "Не хватает памяти на обновление");
      return;
    }

    request->send(202, "text/plain", "Update initiated, rebooting...");
  });

server->on("/ota/mode", HTTP_POST, [] (AsyncWebServerRequest *request) {
    if (!dedCoreChangeAllowed(request)) {
      dedCoreDenyKey(request);
      return;
    }

    if (request->hasParam("mode")) {
      // Значение вне 0..2 оставило бы OTA в состоянии, которого нет ни в одном режиме.
      int mode = clampSetting(request->getParam("mode")->value().toInt(), 0, 2);
      OTA_MODE = mode;
      preferences.putInt("OTA_MODE", OTA_MODE);
      
      OtaMode otaMode = static_cast<OtaMode>(mode);
      otaManager.setMode(otaMode);
      
      request->send(200, "application/json", "{\"mode\":\"" + otaManager.getModeString() + "\"}");
    } else {
      request->send(400, "text/plain", "Missing mode parameter");
    }
  });

server->on("/ota/password", HTTP_POST, [] (AsyncWebServerRequest *request) {
    if (!dedCoreChangeAllowed(request)) {
      dedCoreDenyKey(request);
      return;
    }

    if (request->hasParam("password")) {
      String password = request->getParam("password")->value();

      // Короткий пароль на порту обновления — то же, что его отсутствие.
      if (password.length() > 0 && password.length() < 8) {
        request->send(400, "text/plain", "Password must be at least 8 characters");
        return;
      }

      OTA_PASSWORD = password;
      preferences.putString("OTA_PASSWORD", OTA_PASSWORD);
      otaManager.setArduinoOTAPassword(password);
      
      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Missing password parameter");
    }
  });

server->on("/wifi/ssid", HTTP_GET, [] (AsyncWebServerRequest *request)
  {
    request->send(200, "text/plain", APSSID1);
  });

server->on("/wifi/pass", HTTP_GET, [] (AsyncWebServerRequest *request)
  {
    // Пароль точки доступа — секрет блока: без него в него не войти. Отдаём только
    // владельцу с ключом устройства, а не любому, кто уже подключился к сети.
    if (!dedCoreReadSecretAllowed(request)) {
      dedCoreDenyKey(request);
      return;
    }

    request->send(200, "text/plain", APPSK1);
  });

server->on("/core/reboot", HTTP_GET, [] (AsyncWebServerRequest *request)
  {
    if (!dedCoreChangeAllowed(request)) {
      dedCoreDenyKey(request);
      return;
    }

    request->send(200, "text/plain", "REBOOT");
    ESP.restart();
  });

server->on("/core/version", HTTP_GET, [] (AsyncWebServerRequest *request)
  {
    request->send(200, "text/plain", FIRMWARE_VERSION);
   
  });
}

#endif // DEVICE_API_ROUTES_H
