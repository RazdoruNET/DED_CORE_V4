#ifndef PROVISION_H
#define PROVISION_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "core/ota/HYBRID_OTA.h"

extern HybridOTAManager otaManager;

// Провижининг по Serial. Мастер через WebSerial шлёт строку:
//   {"event":"provision","data":{"key":"<64 hex>"}}
// Ключ сохраняется в NVS и используется для OTA по X-Device-Key.
// Ответ печатается в тот же порт, чтобы мастер видел результат в логе.
void handleSerialProvision() {
  if (!Serial.available()) {
    return;
  }

  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) {
    return;
  }

  StaticJsonBuffer<512> jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(line);
  if (!root.success() || root["event"] != "provision") {
    return;
  }

  const char* key = root["data"]["key"];
  if (key == nullptr || strlen(key) == 0) {
    Serial.println("PROVISION error: key missing");
    return;
  }

  otaManager.setDeviceKey(String(key));
  Serial.println("PROVISION ok");
}

#endif // PROVISION_H
