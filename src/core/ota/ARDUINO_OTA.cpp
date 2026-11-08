// Реализация выключенного по умолчанию пути ArduinoOTA — см. ARDUINO_OTA.h.
#ifdef DED_CORE_NO_ARDUINO_OTA
// Ничего не определяем: при выключенном пути класс собран заглушками в заголовке.
#else

#include "ARDUINO_OTA.h"

ArduinoOTAManager::ArduinoOTAManager() {
    // Пароля по умолчанию нет: значение из публичного репозитория — это не пароль.
    otaPassword = "";
    initialized = false;
}

void ArduinoOTAManager::setPassword(const String& password) {
    otaPassword = password;
}

void ArduinoOTAManager::setHostname(const String& hostname) {
    ArduinoOTA.setHostname(hostname.c_str());
}

void ArduinoOTAManager::begin() {
    // Порт ArduinoOTA без пароля пускает кого угодно, кто его найдёт: пока пароль
    // не задан владельцем, путь обновления по UDP не поднимаем вовсе.
    if (otaPassword.length() < 8) {
        Serial.println("ArduinoOTA: password is not set, UDP update path is disabled");
        initialized = false;
        return;
    }

    ArduinoOTA.setHostname("DED_CORE_V4");
    ArduinoOTA.setPassword(otaPassword.c_str());
    
    ArduinoOTA.onStart([]() {
        UPDATE_MODE = true;
        Serial.println("OTA Update Started");
    });
    
    ArduinoOTA.onEnd([]() {
        Serial.println("\nOTA Update Finished");
        Serial.println("Rebooting...");
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
        UPDATE_MODE = false;
    });
    
    ArduinoOTA.begin();
    initialized = true;
    Serial.println("ArduinoOTA initialized");
}

void ArduinoOTAManager::handle() {
    if (initialized) {
        ArduinoOTA.handle();
    }
}

bool ArduinoOTAManager::isInitialized() {
    return initialized;
}

#endif // DED_CORE_NO_ARDUINO_OTA
