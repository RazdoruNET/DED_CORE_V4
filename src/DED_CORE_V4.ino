#include <Preferences.h>

Preferences preferences;

#define FIRMWARE_VERSION "0.0.1"

#include "core/init/SYSTEM_INIT.h"
#include "core/preferences/PREFERENCES_MANAGER.h"
#include "core/recovery/RECOVERY_MODE.h"

#include "config/PROPS.h"
#include "functions/SOUND.h"

#include "core/ignition/MOTO.h"
#include "core/ignition/carts/MOTO_CARTS.h"
#include "servers/WEB_SERVER.h"
#include "servers/WEB_SOCKET_SERVER.h"
#include "servers/WIFI_SERVER.h"
#include "servers/SERVER.h"
#include "config/CONFIG.h"

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
