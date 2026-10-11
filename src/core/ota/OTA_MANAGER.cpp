#include "OTA_MANAGER.h"
#include <Preferences.h>

extern Preferences preferences;

OTAManager::OTAManager() {
    firmwareServerURL = "http://" + serverName + ":8000/api/firmware";
    currentVersion = "0.0.1";
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
    
    if (info.updateAvailable) {
        info.version = root["version"].as<String>();
        info.url = root["downloadUrl"].as<String>();
        info.size = root["size"];
        info.md5 = root["md5"].as<String>();
        info.changelog = root["changelog"].as<String>();
        
        Serial.println("Update available:");
        Serial.print("  Version: ");
        Serial.println(info.version);
        Serial.print("  Size: ");
        Serial.println(info.size);
        Serial.print("  MD5: ");
        Serial.println(info.md5);
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
    
    HTTPClient http;
    String url;

    if (deviceKey.length() > 0) {
        // Купленное устройство: доступ по ключу (X-Device-Key), без сессии.
        url = firmwareServerURL + "/device/check?current=" + currentVersion + "&type=Kernel";
    } else {
        // Legacy: проверка по MAC проекта под пользовательским токеном.
        String macAddress = WiFi.macAddress();
        macAddress.replace(":", "");
        macAddress.toLowerCase();
        url = firmwareServerURL + "/check?current=" + currentVersion + "&mac=" + macAddress + "&type=Kernel";
    }

    Serial.print("Checking for update: ");
    Serial.println(url);

    http.begin(url);
    http.setTimeout(OTA_TIMEOUT);

    if (deviceKey.length() > 0) {
        http.addHeader("X-Device-Key", deviceKey);
    } else if (jwtToken.length() > 0) {
        String authHeader = "Bearer " + jwtToken;
        http.addHeader("Authorization", authHeader);
    }
    
    int httpCode = http.GET();
    
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

bool OTAManager::validateFirmware(uint32_t size, const String& md5) {
    if (size == 0 || size > 3000000) { // Максимальный размер 3MB
        Serial.println("Invalid firmware size");
        return false;
    }
    
    if (md5.length() != 32) {
        Serial.println("Invalid MD5 hash");
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

bool OTAManager::downloadAndInstall(const String& url) {
    HTTPClient http;
    
    Serial.print("Downloading firmware from: ");
    Serial.println(url);
    
    http.begin(url);
    http.setTimeout(OTA_TIMEOUT);
    
    if (deviceKey.length() > 0) {
        http.addHeader("X-Device-Key", deviceKey);
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
    size_t written = Update.writeStream(*stream);
    
    if (written != contentLength) {
        Serial.print("Write failed. Written: ");
        Serial.print(written);
        Serial.print(" Expected: ");
        Serial.println(contentLength);
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

bool OTAManager::performUpdate() {
    if (!initialized) {
        Serial.println("OTAManager not initialized");
        return false;
    }
    
    if (!checkForUpdate()) {
        Serial.println("No update available");
        return false;
    }
    
    if (!lastFirmwareInfo.updateAvailable) {
        Serial.println("No update available");
        return false;
    }
    
    if (!validateFirmware(lastFirmwareInfo.size, lastFirmwareInfo.md5)) {
        Serial.println("Firmware validation failed");
        return false;
    }
    
    Serial.println("Starting firmware update...");
    UPDATE_MODE = true;
    
    bool success = downloadAndInstall(lastFirmwareInfo.url);
    
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
