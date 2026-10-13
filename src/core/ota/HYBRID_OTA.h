#ifndef HYBRID_OTA_H
#define HYBRID_OTA_H

#include <Arduino.h>
#include "OTA_MANAGER.h"
#include "ARDUINO_OTA.h"

enum class OtaMode {
    AUTO,      // Автоматическое обновление с сервера
    MANUAL,    // Ручное обновление через ArduinoOTA
    OFF        // Обновления отключены
};

class HybridOTAManager {
private:
    OTAManager httpOta;
    ArduinoOTAManager arduinoOta;
    OtaMode mode;
    bool initialized;
    
public:
    HybridOTAManager();
    void begin();
    void handle();
    void setMode(OtaMode m);
    OtaMode getMode();
    bool checkForUpdate();

    // Ответил ли сервис на последнюю проверку: неудачная попытка не должна откладывать
    // следующую на полчаса.
    bool lastCheckAnswered();
    bool performUpdate();
    void setJwtToken(const String& token);
    void setServerURL(const String& url);
    void setDeviceKey(const String& key);
    String getDeviceKey();
    void setArduinoOTAPassword(const String& password);
    String getCurrentVersion();
    FirmwareInfo getLastFirmwareInfo();
    bool isInitialized();
    // Идёт запись образа во флеш: переход на новую разметку в это время не начинаем.
    bool isUpdating();
    String getModeString();
};

#endif
