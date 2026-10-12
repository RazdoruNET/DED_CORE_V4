#ifndef MIGRATION_SERVER_H
#define MIGRATION_SERVER_H

// Запуск перехода на новую разметку флеша по команде приложения.
//
// Устройство само скачивает полный образ с нашего сервиса (у него есть домашняя сеть),
// укладывает его в будущий слот app1, записывает новую таблицу партиций и перезагружается.
// Передавать 1.4 МБ через телефон не нужно — приложение только даёт команду и следит
// за ходом через /api/migration.
//
// Запись во флеш на короткое время останавливает оба ядра (кэш команд выключается),
// поэтому и обновление, и смена разметки разрешены только на заглушённом двигателе:
// на оборотах такие паузы могли бы сбить зажигание.

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ESPAsyncWebServer.h>
#include "config/PROPS.h"
#include "core/ota/HYBRID_OTA.h"
#include "core/migration/PARTITION_MIGRATION.h"
#include "core/net/SERVICE_CLIENT.h"

extern HybridOTAManager otaManager;

#define MIGRATION_CHUNK 2048

static TaskHandle_t migrationTaskHandle = nullptr;
static volatile bool migrationRunning = false;
static volatile uint8_t migrationProgress = 0;
static String migrationError;
static String migrationStep;

bool migrationInProgress() {
  return migrationRunning;
}

bool engineIsStopped() {
  return RPM <= 0;
}

// Скачиваем образ и укладываем его в будущий слот. Ничего не переключаем —
// переключение делает отдельный шаг (writePartitionTable + перезагрузка).
void migrationDownloadAndStage(const String &url, const String &expectedMd5,
                              const String &expectedSha256, size_t expectedSize) {
  WiFiClientSecure client;

  if (!serviceClientSetup(client)) {
    migrationError = "нет часов для проверки сертификата сервиса";
    return;
  }

  HTTPClient http;

  // Адрес от сервиса приводим к нашему HTTPS-адресу: за прокси в нём может быть
  // http:// или внутренний хост. Чужой адрес не принимаем.
  String downloadUrl = serviceAbsoluteUrl(url);

  if (downloadUrl.length() == 0) {
    migrationError = "сервис вернул недопустимый адрес прошивки";
    return;
  }

  migrationStep = "Проверяем наличие прошивки";
  Serial.print("Migration: downloading ");
  Serial.println(downloadUrl);

  if (!http.begin(client, downloadUrl)) {
    migrationError = "не удалось начать загрузку";
    return;
  }

  String deviceKey = otaManager.getDeviceKey();
  if (deviceKey.length() > 0) {
    http.addHeader("X-Device-Key", deviceKey);
  }

  http.setTimeout(30000);
  int code = http.GET();

  if (code != HTTP_CODE_OK) {
    migrationError = "сервис не отдал прошивку (код " + String(code) + ")";
    http.end();
    return;
  }

  int contentLength = http.getSize();
  size_t total = contentLength > 0 ? (size_t)contentLength : expectedSize;

  if (total == 0) {
    migrationError = "неизвестный размер прошивки";
    http.end();
    return;
  }

  migrationStep = "Готовим слот";
  if (!stageBegin(total, migrationError)) {
    http.end();
    return;
  }

  migrationStep = "Загружаем прошивку";
  WiFiClient *stream = http.getStreamPtr();
  uint8_t buffer[MIGRATION_CHUNK];
  size_t written = 0;

  while (http.connected() && written < total) {
    // Появление оборотов — немедленно прекращаем: зажигание важнее обновления.
    if (!engineIsStopped()) {
      migrationError = "двигатель запущен — обновление отменено";
      http.end();
      return;
    }

    size_t available = stream->available();
    if (available == 0) {
      delay(5);
      continue;
    }

    size_t want = available > sizeof(buffer) ? sizeof(buffer) : available;
    int read = stream->readBytes(buffer, want);

    if (read <= 0) {
      continue;
    }

    if (!stageWrite(buffer, read)) {
      migrationError = "ошибка записи во флеш";
      http.end();
      return;
    }

    written += read;
    migrationProgress = (uint8_t)((written * 100) / total);
  }

  http.end();

  migrationStep = "Проверяем образ";
  if (!stageFinish(expectedMd5, expectedSha256, migrationError)) {
    return;
  }

  migrationStep = "Записываем новую разметку";
  if (!writePartitionTable(migrationError)) {
    return;
  }

  migrationStep = "Перезагружаемся";
  Serial.println("Migration: rebooting to apply the new layout");
  delay(500);
  ESP.restart();
}

void migrationTask(void *parameter) {
  String *target = (String *)parameter;
  String url = target[0];
  String expectedMd5 = target[1];
  String expectedSha256 = target[2];
  size_t expectedSize = (size_t)target[3].toInt();
  delete[] target;

  migrationDownloadAndStage(url, expectedMd5, expectedSha256, expectedSize);

  // Сюда доходим только при ошибке: при успехе устройство уже перезагружается.
  Serial.print("Migration failed: ");
  Serial.println(migrationError);
  migrationRunning = false;
  migrationTaskHandle = nullptr;
  vTaskDelete(NULL);
}

bool startMigration(const String &url, const String &md5, const String &sha256, size_t size, String &error) {
  if (migrationRunning) {
    error = "переход уже выполняется";
    return false;
  }

  if (!partitionMigrationNeeded()) {
    error = "разметка уже новая";
    return false;
  }

  if (!engineIsStopped()) {
    error = "двигатель запущен — переходить на новую разметку нельзя";
    return false;
  }

  migrationError = "";
  migrationProgress = 0;
  migrationStep = "Начинаем";
  migrationRunning = true;

  String *argument = new String[4];
  argument[0] = url;
  argument[1] = md5;
  argument[2] = sha256;
  argument[3] = String((unsigned long)size);

  if (xTaskCreatePinnedToCore(migrationTask, "migration", 10240, argument, 1,
                              &migrationTaskHandle, 1) != pdPASS) {
    delete[] argument;
    migrationRunning = false;
    error = "не удалось запустить задачу";
    return false;
  }

  return true;
}

// Автоматический переход: сервис сообщает о необходимости перехода в ответе на
// обычную проверку обновлений, и устройство выполняет его само.
//
// Так сделано намеренно: приложение открыто по HTTPS, а устройство отвечает по HTTP —
// браузер запретил бы приложению обратиться к блоку напрямую (mixed content).
// Устройство же само ходит в сервис и получает и разрешение, и адрес образа.
void maybeStartMigrationFromService() {
  static unsigned long lastAttempt = 0;

  if (migrationRunning || !partitionMigrationNeeded()) {
    return;
  }

  // Обычное обновление тоже пишет во флеш — одновременно нельзя.
  if (otaManager.isUpdating()) {
    return;
  }

  // Переход — операция с записью во флеш, повторяем не чаще раза в минуту.
  if (lastAttempt != 0 && millis() - lastAttempt < 60000) {
    return;
  }

  FirmwareInfo info = otaManager.getLastFirmwareInfo();

  if (!info.migrationRequired || info.migrationUrl.length() == 0) {
    return;
  }

  if (!engineIsStopped() || WiFi.status() != WL_CONNECTED) {
    return;
  }

  lastAttempt = millis();

  String error;
  if (startMigration(info.migrationUrl, info.migrationMd5, info.migrationSha256, info.migrationSize, error)) {
    Serial.println("Migration: started on service request");
  } else {
    Serial.print("Migration: not started: ");
    Serial.println(error);
  }
}

void sendMigrationStatus(AsyncWebServerRequest *request) {
  StaticJsonBuffer<768> jsonBuffer;
  JsonObject &json = jsonBuffer.createObject();

  json["needed"] = partitionMigrationNeeded();
  json["running"] = migrationRunning;
  json["progress"] = migrationProgress;
  json["step"] = migrationStep;
  json["error"] = migrationError;
  json["state"] = migrationState();
  json["appSlotSize"] = (uint32_t)currentApp0Size();
  json["newAppSlotSize"] = (uint32_t)NEW_APP0_SIZE;
  json["rpm"] = RPM;
  json["firmware"] = FIRMWARE_VERSION;
  // Переход невозможен без интернета: образ полной прошивки устройство скачивает само.
  json["wifiConnected"] = (WiFi.status() == WL_CONNECTED);
  json["wifiStatus"] = WIFI_UPDATE_STATUS;

  String body = "";
  json.printTo(body);

  request->send(200, "application/json", body);
}

// Права как у обновления по воздуху: из сети точки доступа или с ключом устройства.
bool migrationRequestAllowed(AsyncWebServerRequest *request) {
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

void registerMigrationRoutes(AsyncWebServer *server) {
  server->on("/api/migration", HTTP_GET, [](AsyncWebServerRequest *request) {
    sendMigrationStatus(request);
  });

  server->on("/api/migrate", HTTP_POST, [](AsyncWebServerRequest *request) {}, nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      static String body;

      if (index == 0) {
        body = "";
        body.reserve(total + 1);
      }

      body.concat((const char *)data, len);

      if (index + len < total) {
        return;
      }

      if (!migrationRequestAllowed(request)) {
        request->send(403, "text/plain", "Нужен доступ из сети блока или ключ устройства");
        return;
      }

      StaticJsonBuffer<512> jsonBuffer;
      JsonObject &json = jsonBuffer.parseObject(body);
      body = "";

      if (!json.success() || json["confirm"] != true) {
        request->send(400, "text/plain", "Нужно подтверждение перехода");
        return;
      }

      // Адрес образа не берём из запроса: иначе блок можно было бы отправить на чужой
      // хост и вместе с запросом отдать ключ устройства. Берём только то, что разрешил
      // сервис, — устройство получает это при проверке обновлений.
      FirmwareInfo info = otaManager.getLastFirmwareInfo();

      if (!info.migrationRequired || info.migrationUrl.length() == 0) {
        request->send(409, "text/plain", "Сервис ещё не разрешил переход для этого блока");
        return;
      }

      String error;

      if (!startMigration(info.migrationUrl, info.migrationMd5, info.migrationSha256, info.migrationSize, error)) {
        request->send(409, "text/plain", error);
        return;
      }

      request->send(202, "text/plain", "Переход на новую разметку начат");
    });
}

#endif // MIGRATION_SERVER_H
