#ifndef PREFERENCES_MANAGER_H
#define PREFERENCES_MANAGER_H

#include <Preferences.h>
#include <nvs_flash.h>
#include "config/CONFIG.h"
#include "config/PROPS.h"

extern Preferences preferences;

void clearPreferences() {
  Serial.println("clearPref");
  nvs_flash_erase();   
  nvs_flash_init();    
  Serial.println("clearPref complited");
}

bool initializePreferences() {
  if (!preferences.begin("init_test", false)) {
    preferences.end();
    clearPreferences();

    if (!preferences.begin("init_test", false)) {
      Serial.println("Hmmmmm wtf");
      return false;
    }
  } else {
    if(preferences.getBool("init", false) == false) {
      preferences.end();
      clearPreferences();

      if (!preferences.begin("init_test", false)) {
        Serial.println("Hmmmmm wtf 2");
        return false;
      }
    }
  }
  
  preferences.putBool("init", true);
  preferences.end();
  
  return true;
}

bool initializeMainPreferences() {
  if (!preferences.begin("ded_box", false)) {
    Serial.println("Ошибка инициализации Preferences");

    nvs_flash_erase();     
    nvs_flash_init();      

    if (!preferences.begin("ded_box", false)) {
      Serial.println("Ошибка повторной инициализации Preferences");
      return false;
    }
  }
  
  return true;
}

void setupConfiguration() {
  loadConfig();
  
  Serial.print("active_cart: ");
  Serial.println(active_cart);
}

#endif // PREFERENCES_MANAGER_H
