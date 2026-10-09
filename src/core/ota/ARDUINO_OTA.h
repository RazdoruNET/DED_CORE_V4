#ifndef ARDUINO_OTA_H
#define ARDUINO_OTA_H

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

#endif
