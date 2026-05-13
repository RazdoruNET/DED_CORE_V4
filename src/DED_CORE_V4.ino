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

// Function declarations
void handleMoto();
void handleControllPin();
void handleAlarm();
void handleRPMAlarm();
void initWebSocket();
void handleWebSocket();
void handelWebSocketData();
void handleServer();

#include "core/handlers/HANDLERS_FUNCS.h"
#include "servers/WEB_SOCKET_FUNCS.h"
#include "core/handlers/HANDLERS.h"
#include "core/tasks/TASKS.h"

TaskHandle_t TaskServerHandler;
TaskHandle_t TaskControlHandler;
TaskHandle_t TaskAlarmHandler;


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
  }

  initSystemSound();
}

void loop() {
  handleMoto();
}
