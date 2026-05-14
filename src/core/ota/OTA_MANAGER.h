#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "config/PROPS.h"

#define FIRMWARE_CHECK_INTERVAL 3600000 // 1 час в миллисекундах
#define OTA_TIMEOUT 30000 // 30 секунд таймаут

struct FirmwareInfo {
    String version;
    String url;
    uint32_t size;
    String md5;
    String changelog;
    bool updateAvailable;
};

class OTAManager {
private:
    String firmwareServerURL;
    String currentVersion;
    String jwtToken;
    unsigned long lastCheckTime;
    bool initialized;
    FirmwareInfo lastFirmwareInfo;
    
    bool validateFirmware(uint32_t size, const String& md5);
    bool downloadAndInstall(const String& url);
    void onProgress(size_t progress, size_t total);
    bool parseFirmwareInfo(const String& json, FirmwareInfo& info);
    
public:
    OTAManager();
    void begin();
    bool checkForUpdate();
    bool performUpdate();
    String getCurrentVersion();
    void setServerURL(const String& url);
    void setJwtToken(const String& token);
    FirmwareInfo getLastFirmwareInfo();
    bool isInitialized();
};

#endif
