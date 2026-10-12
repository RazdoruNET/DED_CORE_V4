#ifndef SERVICE_CLIENT_H
#define SERVICE_CLIENT_H

// Обращение устройства к нашему сервису: прошивка, сертификат, команда на переход.
//
// Только HTTPS и только с проверкой серверного сертификата. По HTTP ключ устройства
// (X-Device-Key) и образ прошивки шли бы открытым текстом: ключ — это фактически пароль
// устройства, а образ — исполняемый код. Контрольная сумма MD5 защищает от порчи при
// передаче, но не от подмены, поэтому небезопасного запасного пути здесь намеренно нет.
//
// Проверка сертификата требует правильных часов: без них нельзя понять, истёк ли
// сертификат сервера. Часы берём по NTP — сеть у устройства для этого и так есть.

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include "core/net/TRUSTED_CA.h"

// Адрес сервиса. Домен тот же, что у приложения: сертификат на него выпущен и
// зарезервирован заранее, так что отдельного имени для устройств не нужно.
#define SERVICE_BASE_URL "https://dedbox.pro/api/firmware"

// Ниже этого времени часы считаем невыставленными (2023-11-14).
#define SERVICE_MIN_VALID_TIME 1700000000UL

// Выставить часы по NTP. Повторные вызовы бесплатны, пока время уже есть.
inline bool serviceEnsureTime(unsigned long timeoutMs = 20000) {
  if (time(nullptr) >= (time_t)SERVICE_MIN_VALID_TIME) {
    return true;
  }

  static bool configured = false;

  if (!configured) {
    configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
    configured = true;
    Serial.println("Service: NTP time requested");
  }

  unsigned long start = millis();

  while (millis() - start < timeoutMs) {
    if (time(nullptr) >= (time_t)SERVICE_MIN_VALID_TIME) {
      Serial.println("Service: clock is set");
      return true;
    }

    delay(200);
  }

  Serial.println("Service: no clock yet, HTTPS is not possible");
  return false;
}

// Привести адрес, полученный от сервиса, к нашему HTTPS-адресу.
//
// Сервис строит адрес скачивания из запроса, а за прокси там может оказаться http://
// или внутренний хост. Устройство обращается только по HTTPS и только к известному ему
// адресу, поэтому от полученной ссылки берём ПУТЬ, а схему и хост подставляем свои.
//
// Чужой адрес не принимаем совсем: в запрос к сервису уходит заголовок с ключом
// устройства, и отправлять его на посторонний хост нельзя. Пустая строка в ответе —
// это отказ, а не «оставить как есть».
inline String serviceAbsoluteUrl(const String &url) {
  const char *marker = "/api/firmware/";
  int index = url.indexOf(marker);

  if (index >= 0) {
    String path = url.substring(index + strlen("/api/firmware"));
    return String(SERVICE_BASE_URL) + path;
  }

  if (url.startsWith("/")) {
    return String(SERVICE_BASE_URL) + url;
  }

  Serial.print("Service: refusing foreign address ");
  Serial.println(url);
  return String();
}

// Настроить клиента на наши корневые сертификаты.
// false — часов нет, проверять сертификат сервера нечем; запрос делать нельзя.
// timeoutMs имеет смысл уменьшать там, где ждать нельзя: запрос сертификата идёт
// в задаче веб-сервера, и долгое ожидание часов остановило бы интерфейс.
inline bool serviceClientSetup(WiFiClientSecure &client, unsigned long timeoutMs = 20000) {
  if (!serviceEnsureTime(timeoutMs)) {
    return false;
  }

  client.setCACert(DED_CORE_TRUSTED_ROOTS);
  return true;
}

#endif // SERVICE_CLIENT_H
