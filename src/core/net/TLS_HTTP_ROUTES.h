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

extern HybridOTAManager otaManager;

#define TLS_HTTP_MAX_BODY 16384

// Права: из сети самой точки доступа (туда пускает пароль сети блока) либо с ключом
// устройства. Так сертификат не подменит сосед по домашней сети.
bool tlsHttpRequestAllowed(AsyncWebServerRequest *request) {
  IPAddress client = request->client()->remoteIP();

  if (client[0] == 192 && client[1] == 168 && client[2] == 4) {
    return true;
  }

  String deviceKey = otaManager.getDeviceKey();

  if (deviceKey.length() == 0 || !request->hasHeader("X-Device-Key")) {
    return false;
  }

  return request->getHeader("X-Device-Key")->value() == deviceKey;
}

void tlsSendStatusJson(AsyncWebServerRequest *request) {
  StaticJsonBuffer<512> jsonBuffer;
  JsonObject &json = jsonBuffer.createObject();

  json["host"] = DED_CORE_TLS_HOST;
  json["firmware"] = FIRMWARE_VERSION;
  json["tls"] = tlsIsRunning();
  json["cert"] = SPIFFS.exists("/cert.pem");
  json["certExpiry"] = tlsCertificateExpiry();
  json["certDaysLeft"] = tlsCertificateDaysLeft();
  json["wifiConnected"] = (WiFi.status() == WL_CONNECTED);
  json["wifiStatus"] = WIFI_UPDATE_STATUS;
  json["active_cart"] = active_cart;
  json["rpm"] = RPM;
  // Код сопряжения и MAC нужны приложению, чтобы владелец привязал блок к аккаунту.
  json["pairCode"] = devicePairCode();
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
        body.reserve(total + 1);

        if (total > TLS_HTTP_MAX_BODY || !tlsHttpRequestAllowed(request)) {
          return;
        }
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
        body.reserve(total + 1);
      }

      if (total > 1024 || !tlsHttpRequestAllowed(request)) {
        return;
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

    String error;
    if (tlsFetchFromService(error)) {
      request->send(200, "text/plain", "Сертификат получен, HTTPS перезапускается");
      return;
    }

    String reply = "Не удалось получить сертификат: " + error;
    request->send(502, "text/plain", reply);
  });
}

#endif // TLS_HTTP_ROUTES_H
