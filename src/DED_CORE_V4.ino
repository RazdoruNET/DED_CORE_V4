#include <Preferences.h>

Preferences preferences;

#include "core/init/SYSTEM_INIT.h"
#include "core/preferences/PREFERENCES_MANAGER.h"
#include "core/recovery/RECOVERY_MODE.h"
#include "config/PROPS_DEF.h"
#include "config/PINS_DEF.h"
#include "servers/WEB_SOCKET_VARS.h"

#include "core/ignition/MOTO.h"
#include "core/ignition/carts/MOTO_CARTS.h"
#include "servers/WEB_SERVER_CLEAN.h"
#include "servers/WEB_SERVER_FUNCS.h"
#include "core/ota/HYBRID_OTA.h"
#include "core/migration/LEGACY_CONFIG_IMPORT.h"
#include "core/migration/PARTITION_MIGRATION.h"
#include "core/net/TLS_SERVER.h"
#include "core/net/PAIRING.h"
#include "core/provision/PROVISION.h"

// Bluetooth собирается не во всех вариантах: полный BLE-стек (контроллер + NimBLE)
// не влезает в 1280 КБ — старую разметку флеша, с которой приходят устройства,
// выпущенные до DED CORE V4. См. env esp32dev_legacy в platformio.ini и docs/ded-core-migration.md.
#ifndef DED_CORE_NO_BLE
#include "core/ble/BLE_SERVER.h"
#endif

// Function declarations
void handleMoto();
void handleControllPin();
void handleAlarm();
void handleRPMAlarm();
void initWebSocket();
void handleWebSocket();
void handelWebSocketData();
void handleServer();
void handleWifiAutoUpdate();
bool connectToConfiguredWifi();

#include "core/handlers/HANDLERS_FUNCS.h"
#include "servers/WEB_SOCKET_FUNCS.h"
#include "core/handlers/HANDLERS.h"
#include "core/tasks/TASKS.h"

// Тела задач подключаем здесь: они используют обработчики и сетевой слой,
// поэтому должны идти после их определений.
#include "core/tasks/ControlTask.h"
#include "core/tasks/AlarmTask.h"
#include "core/tasks/ServerTask.h"
#include "core/tasks/OtaTask.h"

TaskHandle_t TaskServerHandler;
TaskHandle_t TaskControlHandler;
TaskHandle_t TaskAlarmHandler;
TaskHandle_t TaskOtaHandler;

HybridOTAManager otaManager;

void setup()
{
  initSystemHardware();
  
  if (isRecoveryModeActivated()) {
    enterRecoveryMode();
  } else {
    initMoto();
    
    if (!initializePreferences()) {
      return;
    }
    
    if (!initializeMainPreferences()) {
      return;
    }

    // Устройства с заводской прошивки держат настройки в SPIFFS — переносим их в NVS,
    // иначе владелец после обновления увидит заводские карты вместо своей настройки.
    importLegacyConfigIfNeeded();

    // Владелец читает эти две строки с провода, чтобы привязать блок к учётной записи.
    printPairCodeToSerial();

    // Если разметку уже переписали и устройство перезагрузилось — доводим переход до конца
    // (переключаем загрузку на новый слот). Успех завершается перезагрузкой.
    if (handleMigrationBootStep()) {
      return;
    }

    setupConfiguration();
    initSystemTasks();
    
    // Initialize OTA manager
    OtaMode mode = static_cast<OtaMode>(OTA_MODE);
    otaManager.setMode(mode);
    otaManager.setJwtToken(OTA_JWT_TOKEN);
    otaManager.setArduinoOTAPassword(OTA_PASSWORD);
    otaManager.begin();

#ifndef DED_CORE_NO_BLE
    initBleServer();
#endif
  }

  initSystemSound();
}

// Кадр владельцу по всем открытым каналам: WS на 81 (вшитый фронт и PWA по HTTP),
// wss на 443 (PWA по HTTPS) и Bluetooth. Команда приходит по одному каналу, а её ответ
// нужен всем: владелец может держать открытым и приложение, и страницу блока.
void sendDeviceFrame(const String &frame) {
  String copy = frame;

  for (int i = 0; i <= 100; i++) {
    webSocket.sendTXT(i, copy);
  }

  tlsBroadcastFrame(frame);

#ifndef DED_CORE_NO_BLE
  if (bleConnected && bleTelemetry != nullptr) {
    bleTelemetry->setValue((uint8_t*)frame.c_str(), frame.length());
    bleTelemetry->notify();
  }
#endif
}

void loop() {
  handleMoto();
  handleSerialProvision();
#ifndef DED_CORE_NO_BLE
  handleBleTelemetry();
#endif
}
