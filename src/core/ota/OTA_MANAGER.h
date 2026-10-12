#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include "config/PROPS.h"

#define FIRMWARE_CHECK_INTERVAL 3600000 // 1 час в миллисекундах
#define OTA_TIMEOUT 30000 // 30 секунд таймаут

struct FirmwareInfo {
    String version;
    String url;
    uint32_t size;
    String md5;

    // SHA-256 образа. Сервис отдаёт его вместе с md5; если его нет (старый сервис),
    // проверяем по md5, но полагаться на md5 как на защиту нельзя.
    String sha256;

    String changelog;
    bool updateAvailable;

    // Переход на новую разметку флеша: сервис сообщает, что устройству с заводской
    // разметкой пора перейти на слоты 1536 КБ и где взять полный образ.
    bool migrationRequired;
    String migrationUrl;
    String migrationMd5;
    String migrationSha256;
    uint32_t migrationSize;
};

class OTAManager {
private:
    String firmwareServerURL;
    String currentVersion;
    String jwtToken;
    String deviceKey;
    unsigned long lastCheckTime;
    bool initialized;
    bool updateInProgress = false;
    FirmwareInfo lastFirmwareInfo;
    
    bool validateFirmware(uint32_t size, const String& md5, const String& sha256);
    bool performUpdateInternal();
    bool downloadAndInstall(const String& url, const String& md5, const String& sha256);
    void onProgress(size_t progress, size_t total);
    bool parseFirmwareInfo(const String& json, FirmwareInfo& info);
    void loadDeviceKey();
    
public:
    OTAManager();
    void begin();
    bool checkForUpdate();
    bool performUpdate();

    // Идёт запись образа во флеш: переход на новую разметку в это время не начинаем.
    bool isUpdating();
    String getCurrentVersion();
    void setServerURL(const String& url);
    void setJwtToken(const String& token);
    void setDeviceKey(const String& key);
    String getDeviceKey();
    FirmwareInfo getLastFirmwareInfo();
    bool isInitialized();
};

#endif
