#include <Preferences.h>

Preferences preferences;

#define FIRMWARE_VERSION "0.0.1"

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
#include "servers/WIFI_SERVER.h"
#include "core/ota/HYBRID_OTA.h"

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

TaskHandle_t TaskServerHandler;
TaskHandle_t TaskControlHandler;
TaskHandle_t TaskAlarmHandler;

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
    
    setupConfiguration();
    initSystemTasks();
    
    // Initialize OTA manager
    OtaMode mode = static_cast<OtaMode>(OTA_MODE);
    otaManager.setMode(mode);
    otaManager.setJwtToken(OTA_JWT_TOKEN);
    otaManager.setArduinoOTAPassword(OTA_PASSWORD);
    otaManager.begin();
  }

  initSystemSound();
}

void loop() {
  handleMoto();
  handleWifiAutoUpdate();
}
