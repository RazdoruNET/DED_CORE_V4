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

// Команды: провижининг ключа и выбор карты — те же события, что в WebSocket.
class DedCoreBleCommandCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic) override {
    std::string value = characteristic->getValue();
    if (value.length() == 0) {
      return;
    }

    StaticJsonBuffer<512> jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(String(value.c_str()));
    if (!root.success()) {
      return;
    }

    if (root["event"] == "provision") {
      const char* key = root["data"]["key"];
      if (key != nullptr && strlen(key) > 0) {
        otaManager.setDeviceKey(String(key));
        Serial.println("BLE PROVISION ok");
      }
    } else if (root["event"] == "cart") {
      int cart = root["data"]["cart"];
      if (cart > 0 && cart <= 3) {
        active_cart = cart - 1;
        select_cart = cart;
        preferences.putInt("active_cart", active_cart);
        Serial.println("BLE cart set");
      }
    } else if (root["event"] == "pair_info") {
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
      bleTelemetry->setValue((uint8_t*)body.c_str(), body.length());
      bleTelemetry->notify();
      Serial.println("BLE pair info sent");
    } else {
      // WiFi, карты, ping — общий разбор: тот же JSON приходит и по WebSocket.
      String reply = "";
      if (handleDeviceCommand(String(value.c_str()), reply)) {
        bleTelemetry->setValue((uint8_t*)reply.c_str(), reply.length());
        bleTelemetry->notify();
      }
    }
  }
};

void initBleServer() {
  NimBLEDevice::init(DED_CORE_BLE_NAME);

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
