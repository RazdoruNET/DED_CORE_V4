#ifndef PROVISION_H
#define PROVISION_H

#include <Arduino.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "core/ota/HYBRID_OTA.h"
#include "core/net/TLS_SERVER.h"

extern HybridOTAManager otaManager;

// Провижининг по Serial. Мастер через WebSerial шлёт строку (без переносов):
//   {"event":"provision","data":{"key":"<64 hex>"}}
//   {"event":"tls","data":{"cert":"-----BEGIN CERTIFICATE-----\n...","key":"-----BEGIN PRIVATE KEY-----\n..."}}
// Ключ сохраняется в NVS и используется для OTA по X-Device-Key, сертификат — в SPIFFS (/cert.pem, /key.pem).
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

  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(line);
  if (!root.success()) {
    return;
  }

  if (root["event"] == "provision") {
    const char* key = root["data"]["key"];
    if (key == nullptr || strlen(key) == 0) {
      Serial.println("PROVISION error: key missing");
      return;
    }

    otaManager.setDeviceKey(String(key));
    Serial.println("PROVISION ok");
    return;
  }

  if (root["event"] == "tls") {
    String error;
    if (applyTlsMaterial(String(line), error)) {
      // Перезапуск TLS делает задача сервера (handleTlsServer) — не трогаем httpd из loop().
      Serial.println("TLS ok");
    } else {
      Serial.print("TLS error: ");
      Serial.println(error);
    }
    return;
  }
}

#endif // PROVISION_H
