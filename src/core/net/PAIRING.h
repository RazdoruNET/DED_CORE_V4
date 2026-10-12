#ifndef PAIRING_H
#define PAIRING_H

// Привязка блока к учётной записи владельца.
//
// Порядок: владелец открывает приложение, вводит MAC и код, который показывает сам блок,
// и получает ключ устройства. Ключ попадает в блок только по локальному каналу —
// со страницы блока в его же сети, по Bluetooth или по проводу.
//
// Почему ключ приходит не от сервиса напрямую: блок бывает без интернета, и тогда
// единственный путь — телефон владельца. И наоборот: чужой человек, зная MAC, может
// получить ключ у сервиса, но установить его в блок не сможет без доступа к самому блоку
// (сеть блока закрыта паролем, Bluetooth и провод требуют физической близости).
//
// Код сопряжения — не секрет и не защита: сервис не может его проверить, потому что
// блок не в сети. Это подтверждение для владельца, что он привязывает именно свой блок.

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>

extern Preferences preferences;

#define PAIR_CODE_KEY "pair_code"
#define PAIR_CODE_LENGTH 6

// Код создаётся один раз и хранится в NVS: он нужен и после перезагрузки, чтобы
// владелец мог привязать блок не в момент включения.
inline String devicePairCode() {
  String code = preferences.getString(PAIR_CODE_KEY, "");

  if (code.length() == PAIR_CODE_LENGTH) {
    return code;
  }

  // randomSeed от аппаратного генератора, иначе после перезагрузки код повторялся бы.
  randomSeed(esp_random());
  code = "";

  for (int i = 0; i < PAIR_CODE_LENGTH; i++) {
    code += String(random(0, 10));
  }

  preferences.putString(PAIR_CODE_KEY, code);
  return code;
}

// Код владельца: показывается в интерфейсе блока и в логе при включении, чтобы владелец
// мог его прочитать, не подключаясь к блоку (например, при первичной настройке).
inline void printPairCodeToSerial() {
  Serial.print("Pair code: ");
  Serial.println(devicePairCode());
  Serial.print("MAC: ");
  Serial.println(WiFi.macAddress());
}

#endif // PAIRING_H
