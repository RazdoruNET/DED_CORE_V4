#include "HYBRID_OTA.h"

HybridOTAManager::HybridOTAManager() {
    mode = OtaMode::AUTO;
    initialized = false;
}

void HybridOTAManager::begin() {
    httpOta.begin();
    
    if (mode == OtaMode::MANUAL || mode == OtaMode::AUTO) {
        arduinoOta.begin();
    }
    
    initialized = true;
    Serial.println("HybridOTAManager initialized");
    Serial.print("Mode: ");
    Serial.println(getModeString().c_str());
}

void HybridOTAManager::handle() {
    if (!initialized) {
        return;
    }
    
    if (mode == OtaMode::MANUAL || mode == OtaMode::AUTO) {
        arduinoOta.handle();
    }
}

void HybridOTAManager::setMode(OtaMode m) {
    mode = m;
    Serial.print("OTA mode set to: ");
    Serial.println(getModeString().c_str());
}

OtaMode HybridOTAManager::getMode() {
    return mode;
}

String HybridOTAManager::getModeString() {
    switch (mode) {
        case OtaMode::AUTO:
            return "AUTO";
        case OtaMode::MANUAL:
            return "MANUAL";
        case OtaMode::OFF:
            return "OFF";
        default:
            return "UNKNOWN";
    }
}

bool HybridOTAManager::checkForUpdate() {
    if (!initialized || mode == OtaMode::OFF) {
        Serial.println("OTA disabled or not initialized");
        return false;
    }
    
    if (mode == OtaMode::AUTO) {
        return httpOta.checkForUpdate();
    }
    
    return false;
}

bool HybridOTAManager::performUpdate() {
    if (!initialized || mode == OtaMode::OFF) {
        Serial.println("OTA disabled or not initialized");
        return false;
    }
    
    if (mode == OtaMode::AUTO) {
        return httpOta.performUpdate();
    }
    
    Serial.println("Manual mode - use ArduinoOTA for updates");
    return false;
}

void HybridOTAManager::setJwtToken(const String& token) {
    httpOta.setJwtToken(token);
}

void HybridOTAManager::setServerURL(const String& url) {
    httpOta.setServerURL(url);
}

void HybridOTAManager::setArduinoOTAPassword(const String& password) {
    arduinoOta.setPassword(password);
}

String HybridOTAManager::getCurrentVersion() {
    return httpOta.getCurrentVersion();
}

FirmwareInfo HybridOTAManager::getLastFirmwareInfo() {
    return httpOta.getLastFirmwareInfo();
}

bool HybridOTAManager::isInitialized() {
    return initialized;
}
