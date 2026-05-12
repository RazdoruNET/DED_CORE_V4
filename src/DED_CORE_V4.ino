#include <Preferences.h>

Preferences preferences;

#define FIRMWARE_VERSION "0.0.1"

#include <nvs_flash.h>

#include "ArduinoJson.h"
#include "ArduinoOTA.h"
#include "WebSocketsServer.h"
#include "FS.h"
#include "SPIFFS.h"

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

void clearPref()
{
  Serial.println("clearPref");
  nvs_flash_erase();   
  nvs_flash_init();    
  Serial.println("clearPref complited");
}

void setup()
{
  
  Serial.begin(115200);
  Serial.setDebugOutput(true);
 
  pinMode(select_cart_down_pin, INPUT);   

  gpio_set_pull_mode(GPIO_NUM_33, GPIO_PULLUP_ONLY);
  
  int down_btn = digitalRead(select_cart_down_pin);

  if (down_btn == LOW)
  {
    initRecoveryWifi();
    initRecoveryServer();
  }
  else
  {
    initMoto();
    
    if (!preferences.begin("init_test", false)) 
    {
      preferences.end();
      
      clearPref();

      if (!preferences.begin("init_test", false)) 
      {
        Serial.println("Hmmmmm wtf");
        return;
      }
    }
    else
    {
      if(preferences.getBool("init", false) == false)
      {
        preferences.end();
        
        clearPref();

        if (!preferences.begin("init_test", false)) 
        {
          Serial.println("Hmmmmm wtf 2");
          return;
        }
      }
    }
    
    preferences.putBool("init", true);
    preferences.end();
    
    Serial.println();
    
    if (!preferences.begin("ded_box", false)) 
    {
      Serial.println("Ошибка инициализации Preferences");
  
      nvs_flash_erase();     
      nvs_flash_init();      

      if (!preferences.begin("ded_box", false)) 
      {
        Serial.println("Ошибка повторной инициализации Preferences");

        return;
      }
    }
    
    loadConfig();

    Serial.print("active_cart: ");
    Serial.println(active_cart);
    
    xTaskCreatePinnedToCore(TaskServerCode,  "TaskServerHandler",  20000, NULL, 2, &TaskServerHandler,     0);
    xTaskCreatePinnedToCore(TaskControlCode, "TaskControlHandler", 1024, NULL, 1, &TaskControlHandler,     0);

    if (SOUND == true)
    {
      pinMode(select_cart_up_pin, OUTPUT);  
      xTaskCreatePinnedToCore(TaskAlarmCode,   "TaskAlarmHandler",   2000, NULL, 1, &TaskAlarmHandler,   0);
    }
  }

  if (SOUND == true)
  {
    beep(1, 0, 150, 698.5);
    beep(1, 0, 150, 880);
    beep(1, 0, 150, 1050);
  }
}

void loop() {
  handleMoto();
}
