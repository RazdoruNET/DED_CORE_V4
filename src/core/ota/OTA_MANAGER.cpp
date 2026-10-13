#include "OTA_MANAGER.h"
#include "core/net/IMAGE_HASH.h"
#include <Preferences.h>
#include <esp_ota_ops.h>
#include "core/net/SERVICE_CLIENT.h"
#include "core/net/PAIRING.h"

// Размер слота приложения, на котором сейчас работает устройство. Сервис по нему
// понимает, нужен ли переход на новую разметку флеша (заводской слот 1280 КБ).
static uint32_t runningAppSlotSize() {
    const esp_partition_t *running = esp_ota_get_running_partition();
    return running == nullptr ? 0 : running->size;
}

extern Preferences preferences;

OTAManager::OTAManager() {
    firmwareServerURL = SERVICE_BASE_URL;
    // Версия, которую блок сообщает сервису при проверке обновлений. Раньше здесь
    // стояла строка "0.0.1" вместо константы сборки: сервис считал, что обновление
    // доступно всегда, и блок переустанавливал один и тот же релиз по кругу.
    currentVersion = FIRMWARE_VERSION;
    jwtToken = "";
    lastCheckTime = 0;
    initialized = false;
    lastFirmwareInfo.updateAvailable = false;
}

void OTAManager::begin() {
    initialized = true;
    loadDeviceKey();
    Serial.println("OTAManager initialized");
    Serial.print("Firmware server URL: ");
    Serial.println(firmwareServerURL);
    Serial.print("Current version: ");
    Serial.println(currentVersion);
}

// Ключ устройства хранится в NVS (namespace "ded_box") и выдаётся мастером при
// провижининге. Пусто — устройство обновляется по старой схеме (MAC + токен).
void OTAManager::loadDeviceKey() {
    deviceKey = preferences.getString("device_key", "");
    if (deviceKey.length() > 0) {
        Serial.print("Device key loaded, length: ");
        Serial.println(deviceKey.length());
    } else {
        Serial.println("Device key not set: legacy update check by MAC");
    }
}

void OTAManager::setDeviceKey(const String& key) {
    deviceKey = key;
    preferences.putString("device_key", key);
    Serial.println("Device key saved to NVS");
}

String OTAManager::getDeviceKey() {
    return deviceKey;
}

void OTAManager::setServerURL(const String& url) {
    firmwareServerURL = url;
}

void OTAManager::setJwtToken(const String& token) {
    jwtToken = token;
}

String OTAManager::getCurrentVersion() {
    return currentVersion;
}

bool OTAManager::isInitialized() {
    return initialized;
}

bool OTAManager::parseFirmwareInfo(const String& json, FirmwareInfo& info) {
    StaticJsonBuffer<1024> jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(json);
    
    if (!root.success()) {
        Serial.println("JSON parse error");
        return false;
    }
    
    info.updateAvailable = root["updateAvailable"];

    // Блок перехода на новую разметку (может прийти вместе с обычным ответом).
    info.migrationRequired = false;
    info.migrationUrl = "";
    info.migrationMd5 = "";
    info.migrationSha256 = "";
    info.migrationSize = 0;

    JsonObject& migration = root["migration"];

    if (migration.success() && migration["required"] == true) {
        info.migrationRequired = true;
        info.migrationUrl = migration["downloadUrl"].as<String>();
        info.migrationMd5 = migration["md5"].as<String>();
        info.migrationSha256 = migration["sha256"].as<String>();
        info.migrationSize = migration["size"];
    }
    
    if (info.updateAvailable) {
        info.version = root["version"].as<String>();
        info.url = root["downloadUrl"].as<String>();
        info.size = root["size"];
        info.md5 = root["md5"].as<String>();
        info.sha256 = root["sha256"].as<String>();
        info.changelog = root["changelog"].as<String>();
        
        Serial.println("Update available:");
        Serial.print("  Version: ");
        Serial.println(info.version);
        Serial.print("  Size: ");
        Serial.println(info.size);
        Serial.print("  MD5: ");
        Serial.println(info.md5);
        Serial.print("  SHA-256: ");
        Serial.println(info.sha256);
    } else {
        Serial.println("No update available");
    }
    
    return true;
}

bool OTAManager::checkForUpdate() {
    if (!initialized) {
        Serial.println("OTAManager not initialized");
        return false;
    }
    
    if (millis() - lastCheckTime < FIRMWARE_CHECK_INTERVAL && lastCheckTime != 0) {
        Serial.println("Update check interval not elapsed");
        return lastFirmwareInfo.updateAvailable;
    }
    
    WiFiClientSecure secureClient;

    if (!serviceClientSetup(secureClient)) {
        // Без часов проверять сертификат сервера нечем — повторим позже.
        return false;
    }

    HTTPClient http;
    String url;

    if (deviceKey.length() > 0) {
        // Купленное устройство: доступ по ключу (X-Device-Key), без сессии.
        url = firmwareServerURL + "/device/check?current=" + currentVersion + "&type=Kernel" +
              "&slot=" + String(runningAppSlotSize());
    } else {
        // Legacy: проверка по MAC проекта под пользовательским токеном.
        String macAddress = WiFi.macAddress();
        macAddress.replace(":", "");
        macAddress.toLowerCase();
        url = firmwareServerURL + "/check?current=" + currentVersion + "&mac=" + macAddress + "&type=Kernel";
    }

    Serial.print("Checking for update: ");
    Serial.println(url);

    // Рукопожатию TLS нужно около 32 КБ одним куском (буферы mbedTLS), поэтому при
    // нехватке памяти важно видеть не только общий остаток, но и самый большой блок.
    Serial.printf("Heap before check: free=%u maxAlloc=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

    http.begin(secureClient, url);
    http.setTimeout(OTA_TIMEOUT);

    if (deviceKey.length() > 0) {
        http.addHeader("X-Device-Key", deviceKey);
        // Код сопряжения: сервис запомнит его хеш, и владелец сможет подтвердить,
        // что привязывает именно этот блок. Заголовок, а не параметр в URL: адреса
        // попадают в журналы, а код — секрет блока.
        http.addHeader("X-Pair-Code", devicePairCode());
    } else if (jwtToken.length() > 0) {
        String authHeader = "Bearer " + jwtToken;
        http.addHeader("Authorization", authHeader);
    }
    
    int httpCode = http.GET();

    Serial.printf("Heap after check: free=%u maxAlloc=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        Serial.print("Response: ");
        Serial.println(payload);
        
        if (parseFirmwareInfo(payload, lastFirmwareInfo)) {
            lastCheckTime = millis();
            http.end();
            return lastFirmwareInfo.updateAvailable;
        }
    } else {
        Serial.print("HTTP error: ");
        Serial.println(httpCode);
    }
    
    http.end();
    lastCheckTime = millis();
    return false;
}

bool OTAManager::validateFirmware(uint32_t size, const String& md5, const String& sha256) {
    if (size == 0 || size > 3000000) { // Максимальный размер 3MB
        Serial.println("Invalid firmware size");
        return false;
    }

    // Образ без контрольной суммы не принимаем: проверять целостность будет нечем.
    if (sha256.length() != 64 && md5.length() != 32) {
        Serial.println("No usable firmware hash in the response");
        return false;
    }

    return true;
}

void OTAManager::onProgress(size_t progress, size_t total) {
    static unsigned long lastProgressTime = 0;
    
    if (millis() - lastProgressTime > 500) { // Обновлять каждые 500мс
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
        lastProgressTime = millis();
    }
}

bool OTAManager::downloadAndInstall(const String& url, const String& md5, const String& sha256) {
    WiFiClientSecure secureClient;

    if (!serviceClientSetup(secureClient)) {
        return false;
    }

    HTTPClient http;
    
    Serial.print("Downloading firmware from: ");
    Serial.println(url);
    
    http.begin(secureClient, url);
    http.setTimeout(OTA_TIMEOUT);
    
    if (deviceKey.length() > 0) {
        http.addHeader("X-Device-Key", deviceKey);
        // Код сопряжения: сервис запомнит его хеш, и владелец сможет подтвердить,
        // что привязывает именно этот блок. Заголовок, а не параметр в URL: адреса
        // попадают в журналы, а код — секрет блока.
        http.addHeader("X-Pair-Code", devicePairCode());
    } else if (jwtToken.length() > 0) {
        String authHeader = "Bearer " + jwtToken;
        http.addHeader("Authorization", authHeader);
    }
    
    int httpCode = http.GET();
    
    if (httpCode != HTTP_CODE_OK) {
        Serial.print("Download failed, HTTP code: ");
        Serial.println(httpCode);
        return false;
    }
    
    int contentLength = http.getSize();
    if (contentLength <= 0) {
        Serial.println("Invalid content length");
        return false;
    }
    
    Serial.print("Firmware size: ");
    Serial.println(contentLength);
    
    if (!Update.begin(contentLength)) {
        Serial.println("Not enough space for update");
        return false;
    }
    
    WiFiClient* stream = http.getStreamPtr();

    // Суммы считаем по тем же байтам, что уходят во флеш: поток оборачиваем, а
    // проверенный цикл записи из библиотеки обновления оставляем как есть.
    ImageHasher hasher;
    hasher.begin();
    HashingStream hashingStream(*stream, hasher);

    size_t written = Update.writeStream(hashingStream);
    
    if (written != contentLength) {
        Serial.print("Write failed. Written: ");
        Serial.print(written);
        Serial.print(" Expected: ");
        Serial.println(contentLength);
        return false;
    }

    // Проверяем ДО Update.end(): до него образ ещё не считается принятым, и при
    // подмене содержимого блок просто останется на прежней прошивке.
    String hashError;

    if (!hasher.matches(sha256, md5, hashError)) {
        Serial.print("Firmware verification failed: ");
        Serial.println(hashError);
        Update.abort();
        http.end();
        return false;
    }
    
    if (!Update.end()) {
        Serial.println("Update end failed");
        Serial.println(Update.errorString());
        return false;
    }
    
    if (!Update.isFinished()) {
        Serial.println("Update not finished");
        return false;
    }
    
    Serial.println("Firmware downloaded successfully");
    http.end();
    return true;
}

bool OTAManager::isUpdating() {
    return updateInProgress;
}

// Флаг занятости ставим и снимаем в одном месте: внутри много выходов по ошибке,
// и снимать его на каждом легко забыть. Переход на новую разметку тоже пишет во флеш,
// поэтому одновременно он выполняться не должен.
bool OTAManager::performUpdate() {
    if (!initialized) {
        Serial.println("OTAManager not initialized");
        return false;
    }

    if (updateInProgress) {
        Serial.println("Another flash operation is running");
        return false;
    }

    updateInProgress = true;
    bool result = performUpdateInternal();
    updateInProgress = false;

    return result;
}

bool OTAManager::performUpdateInternal() {
    if (!checkForUpdate()) {
        Serial.println("No update available");
        return false;
    }
    
    if (!lastFirmwareInfo.updateAvailable) {
        Serial.println("No update available");
        return false;
    }
    
    if (!validateFirmware(lastFirmwareInfo.size, lastFirmwareInfo.md5, lastFirmwareInfo.sha256)) {
        Serial.println("Firmware validation failed");
        return false;
    }
    
    Serial.println("Starting firmware update...");
    UPDATE_MODE = true;
    
    String downloadUrl = serviceAbsoluteUrl(lastFirmwareInfo.url);

    if (downloadUrl.length() == 0) {
        Serial.println("Update: service returned an unusable address");
        return false;
    }

    bool success = downloadAndInstall(downloadUrl, lastFirmwareInfo.md5, lastFirmwareInfo.sha256);
    
    if (success) {
        Serial.println("Update successful, rebooting...");
        delay(1000);
        ESP.restart();
    } else {
        Serial.println("Update failed");
        UPDATE_MODE = false;
    }
    
    return success;
}

FirmwareInfo OTAManager::getLastFirmwareInfo() {
    return lastFirmwareInfo;
}
