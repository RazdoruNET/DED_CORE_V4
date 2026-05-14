#ifndef HYBRID_OTA_H
#define HYBRID_OTA_H

#include "OTA_MANAGER.h"
#include "ARDUINO_OTA.h"

enum class OtaMode {
    AUTO,      // Автоматическое обновление с сервера
    MANUAL,    // Ручное обновление через ArduinoOTA
    DISABLED   // Обновления отключены
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
    bool performUpdate();
    void setJwtToken(const String& token);
    void setServerURL(const String& url);
    void setArduinoOTAPassword(const String& password);
    String getCurrentVersion();
    FirmwareInfo getLastFirmwareInfo();
    bool isInitialized();
    String getModeString();
};

#endif
