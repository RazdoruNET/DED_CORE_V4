#ifndef TLS_HTTP_ROUTES_H
#define TLS_HTTP_ROUTES_H

// Восстановление сертификата по обычному HTTP (порт 80).
//
// Это аварийный путь, и он обязан работать тогда, когда HTTPS уже не работает:
// сертификат истёк, не установлен или испорчен. Поэтому маршруты живут на том же
// веб-сервере, что и страница обновления, а не на HTTPS-сервере.
//
// Реализация отдельно от TLS_SERVER.cpp намеренно: заголовки IDF (esp_http_server.h)
// и ESPAsyncWebServer объявляют несовместимые перечисления HTTP_*, в одной единице
// трансляции они не уживаются.
//
// Как это использует владелец, у которого блок без интернета:
//   1. телефон подключают к сети блока (AP-DNS резолвит core.dedbox.pro на адрес блока);
//   2. открывают http://192.168.4.1/update — страница отдаётся из прошивки;
//   3. скачивают свежие cert.pem и key.pem (например, из приложения) и устанавливают
//      их здесь же.
// Никакого интернета на устройстве для этого не нужно.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include "config/PROPS.h"
#include "core/ota/HYBRID_OTA.h"
#include "core/net/TLS_SERVER.h"
#include "core/net/PAIRING.h"
#include "core/net/AI_GRANT.h"

extern HybridOTAManager otaManager;

#define TLS_HTTP_MAX_BODY 16384

// Долгую операцию (рукопожатие TLS к сервису) выполняет отдельная задача:
// определения — servers/DEVICE_API_ROUTES.h.
typedef void (*DeviceJobFn)();
bool startDeviceJob(DeviceJobFn job, const char *name, uint32_t stack);

void tlsRefreshJob() {
  String error;

  if (!tlsFetchFromService(error)) {
    Serial.print("TLS: refresh failed: ");
    Serial.println(error);
  }
}

// Права: из сети самой точки доступа (туда пускает пароль сети блока) либо с ключом
// устройства. Так сертификат не подменит сосед по домашней сети.
bool tlsHttpKeyEquals(const String &a, const String &b) {
  if (a.length() == 0 || a.length() != b.length()) {
    return false;
  }

  unsigned char diff = 0;

  for (size_t i = 0; i < a.length(); i++) {
    diff |= static_cast<unsigned char>(a[i] ^ b[i]);
  }

  return diff == 0;
}

bool tlsHttpKeyMatches(AsyncWebServerRequest *request) {
  String deviceKey = otaManager.getDeviceKey();

  if (deviceKey.length() == 0 || !request->hasHeader("X-Device-Key")) {
    return false;
  }

  return tlsHttpKeyEquals(request->getHeader("X-Device-Key")->value(), deviceKey);
}

// Определение — core/net/MODULE_PAIRING.h: модуль сопряжения подключается позже,
// поэтому объявляем проверку здесь.
bool devicePairingWindowOpen();

bool tlsHttpRequestAllowed(AsyncWebServerRequest *request) {
  if (tlsHttpKeyMatches(request)) {
    return true;
  }

  // Из сети блока — только пока ключа нет: привязанный блок меняет сертификат
  // и параметры лишь по ключу устройства.
  if (otaManager.getDeviceKey().length() > 0) {
    return false;
  }

  // Во время окна сопряжения модулей сеть блока открыта: адрес клиента не доказывает
  // доступ владельца, поэтому остаётся только ключ устройства.
  if (devicePairingWindowOpen()) {
    return false;
  }

  // См. DEVICE_API_ROUTES.h: подсеть клиента ничего не доказывает, сравниваем
  // локальный адрес соединения с адресом точки доступа.
  return request->client()->localIP() == WiFi.softAPIP();
}

void tlsSendStatusJson(AsyncWebServerRequest *request) {
  StaticJsonBuffer<512> jsonBuffer;
  JsonObject &json = jsonBuffer.createObject();

  json["host"] = DED_CORE_TLS_HOST;
  json["firmware"] = FIRMWARE_VERSION;
  json["tls"] = tlsIsRunning();
  json["cert"] = DEVICE_FS.exists("/cert.pem");
  json["certExpiry"] = tlsCertificateExpiry();
  json["certDaysLeft"] = tlsCertificateDaysLeft();
  json["wifiConnected"] = (WiFi.status() == WL_CONNECTED);
  json["wifiStatus"] = WIFI_UPDATE_STATUS;
  json["active_cart"] = active_cart;
  json["rpm"] = RPM;
  // Код сопряжения и MAC нужны приложению, чтобы владелец привязал блок к аккаунту.
  // Сам код — секрет блока: по нему привязывают к чужому аккаунту. Поэтому он уходит
  // только владельцу с ключом устройства, а из сети блока без ключа виден лишь MAC.
  json["pairCode"] = tlsHttpKeyMatches(request) ? devicePairCode() : "";
  json["mac"] = WiFi.macAddress();
  json["paired"] = otaManager.getDeviceKey().length() > 0;

  String body = "";
  json.printTo(body);

  request->send(200, "application/json", body);
}

void registerTlsHttpRoutes(AsyncWebServer *server) {
  // Состояние блока для его страницы: доступно без HTTPS (сертификата может не быть),
  // но только из сети блока или с ключом устройства.
  //
  // В ответе MAC и код сопряжения, поэтому открытым он быть не должен: блок работает
  // и в домашней сети (WIFI_AP_STA), а сосед по сети не должен уметь ни подсмотреть код,
  // ни привязать блок к себе.
  server->on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!tlsHttpRequestAllowed(request)) {
      request->send(403, "text/plain",
        "Откройте страницу блока из его собственной сети (или передайте ключ устройства).");
      return;
    }

    tlsSendStatusJson(request);
  });

  // Установка сертификата вручную (аварийный путь).
  server->on("/api/tls", HTTP_POST, [](AsyncWebServerRequest *request) {}, nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      static String body;

      if (index == 0) {
        body = "";

        // Лимит проверяем ДО reserve: объявленный в запросе размер иначе заставлял
        // устройство выделять под тело сколько угодно памяти.
        if (total > TLS_HTTP_MAX_BODY || !tlsHttpRequestAllowed(request)) {
          request->send(total > TLS_HTTP_MAX_BODY ? 413 : 403, "text/plain",
                        total > TLS_HTTP_MAX_BODY ? "Тело запроса слишком большое" : "Нужен ключ устройства");
          return;
        }

        body.reserve(total + 1);
      }

      if (!tlsHttpRequestAllowed(request) || total > TLS_HTTP_MAX_BODY) {
        return;
      }

      body.concat((const char *)data, len);

      if (index + len < total) {
        return;
      }

      String error;
      if (applyTlsMaterial(body, error)) {
        // Сертификат применён — перезапуск HTTPS сделает задача сервера.
        request->send(200, "application/json", "{\"result\":\"ok\"}");
      } else {
        String reply = "{\"result\":\"error\",\"message\":\"" + error + "\"}";
        request->send(400, "application/json", reply);
      }

      body = "";
    });

  // Привязка: владелец передаёт полученный у сервиса ключ устройства.
  // Тело: {"key":"<64 hex>"} — ключ сохраняется в NVS и используется для обновлений.
  server->on("/api/provision", HTTP_POST, [](AsyncWebServerRequest *request) {}, nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      static String body;

      if (index == 0) {
        body = "";

        if (total > 1024 || !tlsHttpRequestAllowed(request)) {
          request->send(403, "text/plain", "Тело запроса слишком большое или нет прав");
          return;
        }

        body.reserve(total + 1);
      }

      body.concat((const char *)data, len);

      if (index + len < total) {
        return;
      }

      StaticJsonBuffer<512> jsonBuffer;
      JsonObject &json = jsonBuffer.parseObject(body);
      body = "";

      if (!json.success()) {
        request->send(400, "text/plain", "Ожидается JSON с ключом устройства");
        return;
      }

      const char *key = json["key"];

      if (key == nullptr || strlen(key) < 32) {
        request->send(400, "text/plain", "Нужен ключ устройства");
        return;
      }

      otaManager.setDeviceKey(String(key));
      Serial.println("Pairing: device key stored");

      request->send(200, "application/json", "{\"result\":\"ok\"}");
    });

  // Подпись для чата «Люси»: nonce выдаёт сервис чата, блок подписывает его ключом.
  // Тело: {"nonce":"<hex>"} — в ответ MAC и HMAC, сам ключ наружу не уходит.
  server->on("/api/ai/grant", HTTP_POST, [](AsyncWebServerRequest *request) {}, nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      static String body;
      static bool denied;

      if (index == 0) {
        body = "";
        denied = total > 512 || !tlsHttpRequestAllowed(request);

        if (denied) {
          request->send(403, "application/json", "{\"result\":\"error\",\"error\":\"forbidden\"}");
          return;
        }

        body.reserve(total + 1);
      }

      if (denied) {
        return;
      }

      body.concat((const char *)data, len);

      if (index + len < total) {
        return;
      }

      StaticJsonBuffer<256> jsonBuffer;
      JsonObject &json = jsonBuffer.parseObject(body);
      body = "";

      const char *nonce = json.success() ? json["nonce"].as<const char *>() : nullptr;
      String signature;
      String error = "nonce";

      if (nonce == nullptr || !aiGrantSignature(String(nonce), signature, error)) {
        String reply = "{\"result\":\"error\",\"error\":\"" + error + "\"}";
        request->send(error == "not_paired" ? 409 : 400, "application/json", reply);
        return;
      }

      String reply = "{\"result\":\"ok\",\"mac\":\"" + aiGrantMac() +
                     "\",\"signature\":\"" + signature + "\"}";
      request->send(200, "application/json", reply);
    });

  // Попытка забрать свежий сертификат у сервиса прямо сейчас (нужен интернет у блока).
  server->on("/api/tls/refresh", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!tlsHttpRequestAllowed(request)) {
      request->send(403, "text/plain", "Нужен доступ из сети блока или ключ устройства");
      return;
    }

    if (WiFi.status() != WL_CONNECTED) {
      request->send(409, "text/plain",
        "Блок не подключён к сети с интернетом. Установите сертификат вручную файлами cert.pem и key.pem.");
      return;
    }

    // Запрос к сервису — это рукопожатие TLS на 16–32 КБ: в задаче AsyncTCP его стек
    // и куча не выдержат, а ожидание часов остановило бы интерфейс. Поэтому работа
    // уходит в свою задачу, а сертификат применяется и HTTPS перезапускается там же.
    if (!startDeviceJob(tlsRefreshJob, "TaskTlsRefresh", 24576)) {
      request->send(503, "text/plain", "Не хватает памяти на запрос сертификата");
      return;
    }

    request->send(202, "text/plain", "Запрос сертификата отправлен, HTTPS перезапустится после ответа");
  });
}

#endif // TLS_HTTP_ROUTES_H
