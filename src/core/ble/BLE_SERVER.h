#ifndef BLE_SERVER_H
#define BLE_SERVER_H

#include <Arduino.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include "core/ota/HYBRID_OTA.h"
#include "core/net/PAIRING.h"
#include "core/net/DEVICE_COMMANDS.h"
#include "config/PROPS.h"

extern Preferences preferences;
extern HybridOTAManager otaManager;

// Контракт BLE совпадает с PWA (frontend/src/app/ded-core-control/ded-core-control.service.ts).
// Меняешь UUID здесь — поменяй и там, иначе сопряжение не найдёт устройство.
// Стек — NimBLE: полный Bluedroid не влезает в отведённую под приложение флеш-партицию.
#define DED_CORE_BLE_NAME      "DED CORE"
#define DED_CORE_BLE_SERVICE   "6f1a0001-9a2b-4c3d-8e4f-000000000001"
#define DED_CORE_BLE_TELEMETRY "6f1a0002-9a2b-4c3d-8e4f-000000000002"
#define DED_CORE_BLE_COMMAND   "6f1a0003-9a2b-4c3d-8e4f-000000000003"
#define DED_CORE_BLE_NOTIFY_MS 200
// Состояние подключения к домашней сети меняется редко — отдельным кадром раз в две секунды,
// чтобы кадр телеметрии оставался коротким и укладывался в одну нотификацию.
#define DED_CORE_BLE_WIFI_MS 2000

static NimBLEServer* bleServer = nullptr;
static NimBLECharacteristic* bleTelemetry = nullptr;
static volatile bool bleConnected = false;
static unsigned long bleLastNotify = 0;
static unsigned long bleLastWifiNotify = 0;

class DedCoreBleServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server) override {
    bleConnected = true;
    Serial.println("BLE connected");
  }

  void onDisconnect(NimBLEServer* server) override {
    bleConnected = false;
    Serial.println("BLE disconnected");
    // Клиент мог отойти и вернуться — снова начинаем вещать.
    server->startAdvertising();
  }
};

// Очередь команд Bluetooth.
//
// Обработчик записи GATT выполняется в задаче стека Bluetooth, а её стек небольшой:
// запись в NVS с выводом в лог переполняла его (Guru Meditation, double exception —
// падение в log_printfv). Поэтому в колбэке только копируем кадр в очередь, а разбирает
// его loop() с обычным стеком — как и команды WebSocket.
// Размер кадра = согласованный MTU (255) минус заголовок; очередь на четыре команды.
#define DED_CORE_BLE_QUEUE_LEN 4
#define DED_CORE_BLE_FRAME_MAX 256

static QueueHandle_t bleCommandQueue = nullptr;

class DedCoreBleCommandCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic) override {
    std::string value = characteristic->getValue();
    if (value.length() == 0 || bleCommandQueue == nullptr) {
      return;
    }

    char frame[DED_CORE_BLE_FRAME_MAX] = {0};
    size_t length = value.length() < DED_CORE_BLE_FRAME_MAX - 1 ? value.length() : DED_CORE_BLE_FRAME_MAX - 1;
    memcpy(frame, value.c_str(), length);

    // Ждать нечего: если очередь полна, кадр уже неактуален.
    xQueueSend(bleCommandQueue, frame, 0);
  }
};

// Ответ на команду по Bluetooth: только уведомлением, канал запроса и есть канал ответа.
void bleNotify(const String &body) {
  if (!bleConnected || bleTelemetry == nullptr) {
    return;
  }

  bleTelemetry->setValue((uint8_t*)body.c_str(), body.length());
  bleTelemetry->notify();
}

String blePairInfoJson() {
  // Приложение по Bluetooth узнаёт MAC и код, чтобы привязать блок к аккаунту:
  // со страницы HTTPS приложение не может обратиться к блоку по обычному HTTP.
  StaticJsonBuffer<256> replyBuffer;
  JsonObject& reply = replyBuffer.createObject();
  JsonObject& data = replyBuffer.createObject();

  reply["event"] = "pair_info";
  data["mac"] = WiFi.macAddress();
  data["code"] = devicePairCode();
  data["paired"] = otaManager.getDeviceKey().length() > 0;
  reply["data"] = data;

  String body = "";
  reply.printTo(body);
  return body;
}

// Вызывается из loop(): здесь обычный стек, места хватает и на разбор JSON, и на запись в NVS.
void handleBleCommands() {
  if (bleCommandQueue == nullptr) {
    return;
  }

  char frame[DED_CORE_BLE_FRAME_MAX];

  while (xQueueReceive(bleCommandQueue, frame, 0) == pdTRUE) {
    String payload = String(frame);

    // Провижининг ключа: остальное разбирает общий обработчик — тот же JSON приходит
    // и по WebSocket, и по wss.
    StaticJsonBuffer<512> jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(payload);

    if (root.success() && root["event"] == "provision") {
      const char* key = root["data"]["key"];
      if (key != nullptr && strlen(key) > 0) {
        otaManager.setDeviceKey(String(key));
        Serial.println("BLE PROVISION ok");
      }

      continue;
    }

    if (root.success() && root["event"] == "pair_info") {
      bleNotify(blePairInfoJson());
      Serial.println("BLE pair info sent");
      continue;
    }

    String reply = "";
    if (handleDeviceCommand(payload, reply)) {
      bleNotify(reply);
    }
  }
}

// Bluetooth остановлен, чтобы отдать память под TLS.
static bool bleSuspended = false;

void initBleServer();

bool bleClientConnected() {
  return bleConnected;
}

bool releaseBleMemory() {
  if (bleServer == nullptr) {
    return false;
  }

  bleConnected = false;
  NimBLEDevice::deinit(true);

  bleServer = nullptr;
  bleTelemetry = nullptr;
  bleSuspended = true;

  Serial.printf("BLE released for TLS: free=%u maxAlloc=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  return true;
}

void restoreBleMemory() {
  if (!bleSuspended) {
    return;
  }

  bleSuspended = false;
  initBleServer();

  Serial.printf("BLE restored: free=%u maxAlloc=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

void initBleServer() {
  NimBLEDevice::init(DED_CORE_BLE_NAME);

  if (bleCommandQueue == nullptr) {
    bleCommandQueue = xQueueCreate(DED_CORE_BLE_QUEUE_LEN, DED_CORE_BLE_FRAME_MAX);
  }

  bleServer = NimBLEDevice::createServer();
  bleServer->setCallbacks(new DedCoreBleServerCallbacks());

  NimBLEService* service = bleServer->createService(DED_CORE_BLE_SERVICE);

  bleTelemetry = service->createCharacteristic(DED_CORE_BLE_TELEMETRY, NIMBLE_PROPERTY::NOTIFY);

  NimBLECharacteristic* command = service->createCharacteristic(
      DED_CORE_BLE_COMMAND, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  command->setCallbacks(new DedCoreBleCommandCallbacks());

  service->start();

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(DED_CORE_BLE_SERVICE);
  advertising->setScanResponse(true);
  advertising->start();

  Serial.println("BLE server started");
}

// Периодически шлём показания: тот же набор полей, что и в WS-сообщении прошивки.
void handleBleTelemetry() {
  if (!bleConnected || bleTelemetry == nullptr) {
    return;
  }

  if (millis() - bleLastNotify < DED_CORE_BLE_NOTIFY_MS) {
    return;
  }
  bleLastNotify = millis();

  StaticJsonBuffer<256> jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();
  json["micros"] = (uint32_t)micros();
  json["select_cart"] = select_cart;
  json["rpm"] = RPM;
  json["termo"] = Termo;
  json["throttle"] = Throttle;

  String body;
  json.printTo(body);

  bleTelemetry->setValue((uint8_t*)body.c_str(), body.length());
  bleTelemetry->notify();

  // Домашняя сеть и обновления: отдельный кадр, чтобы основной оставался коротким.
  if (millis() - bleLastWifiNotify >= DED_CORE_BLE_WIFI_MS) {
    bleLastWifiNotify = millis();

    String wifiBody = "{\"event\":\"wifi_status\",\"data\":" + wifiStateJson() + "}";
    bleTelemetry->setValue((uint8_t*)wifiBody.c_str(), wifiBody.length());
    bleTelemetry->notify();
  }
}

#endif // BLE_SERVER_H
