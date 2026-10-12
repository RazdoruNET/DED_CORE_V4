#ifndef ARDUINO_OTA_H
#define ARDUINO_OTA_H

// Обновление через ArduinoOTA (UDP, пароль) можно собирать по желанию.
//
// По умолчанию выключено: в заводской прошивке этот путь тоже был отключён
// (в setup() стояло закомментированное InitOTA()), а у нас он дублирует
// POST /api/update и обновление по команде сервиса, занимая флеш и открывая
// сетевой порт с паролем по умолчанию.
#ifdef DED_CORE_NO_ARDUINO_OTA

class ArduinoOTAManager {
public:
    void begin() {}
    void handle() {}
    void setPassword(const String& password) {}
    void setHostname(const String& hostname) {}
    bool isInitialized() { return false; }
};

#else

#include <ArduinoOTA.h>
#include "config/PROPS.h"

class ArduinoOTAManager {
private:
    String otaPassword;
    bool initialized;
    
public:
    ArduinoOTAManager();
    void begin();
    void handle();
    void setPassword(const String& password);
    void setHostname(const String& hostname);
    bool isInitialized();
};

#endif // DED_CORE_NO_ARDUINO_OTA

#endif
