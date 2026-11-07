#ifndef AI_GRANT_H
#define AI_GRANT_H

// Подпись блока для чата «Люси» на встроенной странице.
//
// Страница блока не знает ни логина, ни JWT, поэтому доказывает право на чат знанием
// ключа устройства — того же, которым блок обновляется. Сервис чата выдаёт одноразовый
// nonce, блок возвращает HMAC-SHA256(ключ, "MAC|nonce"), а проверяет подпись
// FirmwareService, который ключ и хранит. Ключ не покидает блок: наружу уходит только
// подпись, и только под конкретный nonce.
//
// Часы блока здесь не нужны: срок жизни nonce считает сервис.

#include <Arduino.h>
#include <WiFi.h>
#include <mbedtls/md.h>
#include "core/ota/HYBRID_OTA.h"

extern HybridOTAManager otaManager;

// MAC в том же виде, в каком его ждёт сервис: только hex, нижний регистр.
inline String aiGrantMac() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toLowerCase();
  return mac;
}

// nonce: 8..64 символа — так подпись не превращается в способ посчитать HMAC
// от произвольного текста.
inline bool aiGrantSignature(const String &nonce, String &signature, String &error) {
  if (nonce.length() < 8 || nonce.length() > 64) {
    error = "nonce";
    return false;
  }

  String key = otaManager.getDeviceKey();

  if (key.length() == 0) {
    error = "not_paired";
    return false;
  }

  String message = aiGrantMac() + "|" + nonce;
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  uint8_t digest[32];

  if (info == nullptr ||
      mbedtls_md_hmac(info,
                      (const unsigned char *)key.c_str(), key.length(),
                      (const unsigned char *)message.c_str(), message.length(),
                      digest) != 0) {
    error = "hmac";
    return false;
  }

  char hex[65];
  for (uint8_t i = 0; i < 32; i++) {
    snprintf(hex + i * 2, 3, "%02x", digest[i]);
  }
  hex[64] = '\0';

  signature = String(hex);
  return true;
}

#endif // AI_GRANT_H
