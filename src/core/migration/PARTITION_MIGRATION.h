#ifndef PARTITION_MIGRATION_H
#define PARTITION_MIGRATION_H

// Перевод устройства с заводской разметки флеша на разметку DED CORE V4.
//
// Зачем вообще менять разметку. Заводские устройства приходят со слотом приложения
// 1280 КБ (их собственная partitions.bin). Полная прошивка DED CORE V4 с TLS и Bluetooth
// занимает 1.41 МБ и в такой слот не помещается, а слот изменить обычным обновлением
// нельзя: библиотека Update пишет образ строго в существующий слот.
//
// Разметку приходится переписывать самим: она лежит в одном секторе по адресу 0x8000.
// Это единственный по-настоящему рискованный момент — если питание пропадёт ровно
// во время стирания сектора (десятки миллисекунд), загрузчик не найдёт таблицу и
// устройство придётся подключать по проводу. Всё остальное устроено так, чтобы
// прерывание ничего не ломало: пока таблица не записана, устройство грузит прежнюю
// прошивку, а новая сначала целиком укладывается в неактивный слот и проверяется.
//
// Порядок работы (между шагами устройство перезагружается):
//   1. Приложение DED CORE V4 (облегчённая сборка, она помещается в заводской слот)
//      скачивает и укладывает полный образ в будущий слот app1 по адресу 0x190000.
//      В заводской таблице этот адрес занят файловой системой, поэтому настройки
//      владельца переносятся в NVS ДО этого шага (LEGACY_CONFIG_IMPORT.h).
//   2. Пишется новая таблица, читается обратно и сверяется. Не совпало — вернём старую.
//   3. Перезагрузка. Загрузчик работает по новой таблице, но грузит app0 — ту же
//      облегчённую прошивку с того же адреса 0x10000, поэтому всё продолжает работать.
//   4. Устройство убеждается, что уложенный образ похож на прошивку, и переключает
//      загрузку на app1 (esp_ota_set_boot_partition) и перезагружается.
//   5. Загружается полная прошивка. Если с ней что-то не так, загрузчик возвращается
//      на app0 — устройство остаётся рабочим и может повторить попытку.
//
// Состояние шагов хранится в NVS: между ними устройство выключают, а передумать
// должно быть так же безопасно, как продолжить.

#include <Arduino.h>
#include <Preferences.h>
#include <MD5Builder.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_flash.h>

extern Preferences preferences;

// Куда писать новую таблицу и что в ней должно быть (совпадает с partitions_ded_core.csv).
#define PARTITION_TABLE_ADDR     0x8000
#define PARTITION_TABLE_SECTOR   0x8000
// Записей в таблице — пять (nvs, otadata, app0, app1, spiffs), плюс отдельная
// запись с контрольной суммой. Формат совпадает с тем, что генерирует PlatformIO
// из partitions_ded_core.csv, иначе полная прошивка не найдёт свою файловую систему.
#define PARTITION_TABLE_ENTRIES  5
#define PARTITION_TABLE_SIZE     192

// Новая разметка: app-слоты по 1472 КБ, файловая система 1088 КБ.
// Должно совпадать с partitions_ded_core.csv — сверить можно так:
//   python3 ~/.platformio/packages/framework-arduinoespressif32/tools/gen_esp32part.py partitions_ded_core.csv /tmp/ref.bin
#define NEW_APP0_OFFSET  0x010000
#define NEW_APP0_SIZE    0x170000
#define NEW_APP1_OFFSET  0x180000
#define NEW_APP1_SIZE    0x170000
#define NEW_SPIFFS_OFFSET 0x2F0000
#define NEW_SPIFFS_SIZE  0x110000

// Отметки шагов в NVS.
#define MIGRATION_STATE_KEY "mig_state"
#define MIGRATION_STATE_STAGED 1   // образ уложен и проверен, таблица ещё старая
#define MIGRATION_STATE_TABLE_WRITTEN 2 // таблица записана, ждём переключения на app1
#define MIGRATION_STATE_DONE 3     // переключение выполнено

#define MIGRATION_MAX_FIRMWARE (NEW_APP1_SIZE - 0x1000)

struct PartitionEntry {
  uint16_t magic;
  uint8_t type;
  uint8_t subtype;
  uint32_t offset;
  uint32_t size;
  uint8_t label[16];
  uint32_t flags;
} __attribute__((packed));

struct PartitionTableImage {
  PartitionEntry entries[PARTITION_TABLE_ENTRIES];
  // Запись с суммой: два байта 0xEB 0xEB, четырнадцать 0xFF, затем сама сумма.
  uint8_t md5Marker[16];
  uint8_t md5[16];
} __attribute__((packed));

PartitionEntry makePartitionEntry(uint8_t type, uint8_t subtype, uint32_t offset,
                                 uint32_t size, const char *label) {
  PartitionEntry entry = {};
  entry.magic = 0x50AA;
  entry.type = type;
  entry.subtype = subtype;
  entry.offset = offset;
  entry.size = size;
  entry.flags = 0;
  strncpy((char *)entry.label, label, sizeof(entry.label) - 1);
  return entry;
}

// Собирает образ таблицы партиций вместе с контрольной суммой, как её проверяет загрузчик.
void buildPartitionTable(PartitionTableImage &table) {
  table.entries[0] = makePartitionEntry(1, 2, 0x009000, 0x005000, "nvs");
  table.entries[1] = makePartitionEntry(1, 0, 0x00E000, 0x002000, "otadata");
  table.entries[2] = makePartitionEntry(0, 0x10, NEW_APP0_OFFSET, NEW_APP0_SIZE, "app0");
  table.entries[3] = makePartitionEntry(0, 0x11, NEW_APP1_OFFSET, NEW_APP1_SIZE, "app1");
  table.entries[4] = makePartitionEntry(1, 0x82, NEW_SPIFFS_OFFSET, NEW_SPIFFS_SIZE, "spiffs");

  memset(table.md5Marker, 0xFF, sizeof(table.md5Marker));
  table.md5Marker[0] = 0xEB;
  table.md5Marker[1] = 0xEB;

  MD5Builder md5;
  md5.begin();
  md5.add((uint8_t *)table.entries, sizeof(table.entries));
  md5.calculate();
  md5.getBytes(table.md5);
}

// Размер слота, по которому сейчас работает устройство. 0 — таблица не читается.
uint32_t currentApp0Size() {
  const esp_partition_t *running = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);

  if (running == nullptr) {
    return 0;
  }

  return running->size;
}

// Нужна ли миграция: устройство пришло с заводской разметкой.
bool partitionMigrationNeeded() {
  uint32_t size = currentApp0Size();

  if (size == 0) {
    return false;
  }

  return size != NEW_APP0_SIZE;
}

int migrationState() {
  return preferences.getInt(MIGRATION_STATE_KEY, 0);
}

void setMigrationState(int state) {
  preferences.putInt(MIGRATION_STATE_KEY, state);
}

// Проверка, что в буфере действительно прошивка ESP32, а не что попало.
bool looksLikeFirmware(const uint8_t *data, size_t len) {
  if (len < 8 || data[0] != 0xE9) {
    return false;
  }

  uint8_t segments = data[1];
  return segments > 0 && segments <= 16;
}

// --- Укладка образа в будущий слот app1 (пока таблица ещё заводская) ---

static size_t stageWritten = 0;
static size_t stageTotal = 0;
static bool stageActive = false;
static MD5Builder stageMd5;

bool stageBegin(size_t totalSize, String &error) {
  if (totalSize < 64 * 1024 || totalSize > MIGRATION_MAX_FIRMWARE) {
    error = "размер образа не подходит для нового слота";
    return false;
  }

  if (!partitionMigrationNeeded()) {
    error = "разметка уже новая";
    return false;
  }

  if (migrationState() == MIGRATION_STATE_DONE) {
    error = "переход уже выполнен";
    return false;
  }

  // Стираем ровно столько, сколько займёт образ, выровняв по секторам.
  size_t eraseSize = (totalSize + 0xFFF) & ~0xFFFUL;

  Serial.print("Migration: staging ");
  Serial.print(totalSize);
  Serial.println(" bytes to app1");

  if (esp_flash_erase_region(NULL, NEW_APP1_OFFSET, eraseSize) != ESP_OK) {
    error = "не удалось очистить будущий слот";
    return false;
  }

  stageWritten = 0;
  stageTotal = totalSize;
  stageActive = true;
  stageMd5.begin();
  return true;
}

bool stageWrite(const uint8_t *data, size_t len) {
  if (!stageActive || stageWritten + len > stageTotal) {
    return false;
  }

  if (esp_flash_write(NULL, data, NEW_APP1_OFFSET + stageWritten, len) != ESP_OK) {
    stageActive = false;
    return false;
  }

  stageMd5.add((uint8_t *)data, len);
  stageWritten += len;
  return true;
}

// Сверяет уложенное с тем, что читается из флеша, и с ожидаемой суммой от сервера.
bool stageFinish(const String &expectedMd5, String &error) {
  if (!stageActive || stageWritten != stageTotal) {
    error = "образ уложен не полностью";
    stageActive = false;
    return false;
  }

  stageMd5.calculate();

  String actual = stageMd5.toString();
  actual.toLowerCase();

  String expected = expectedMd5;
  expected.toLowerCase();

  if (expected.length() == 32 && actual != expected) {
    error = "контрольная сумма образа не совпала";
    stageActive = false;
    return false;
  }

  uint8_t header[8] = {};
  if (esp_flash_read(NULL, header, NEW_APP1_OFFSET, sizeof(header)) != ESP_OK ||
      !looksLikeFirmware(header, sizeof(header))) {
    error = "уложенный образ не похож на прошивку";
    stageActive = false;
    return false;
  }

  stageActive = false;
  setMigrationState(MIGRATION_STATE_STAGED);
  Serial.println("Migration: image staged and verified");
  return true;
}

uint8_t migrationProgressPercent() {
  if (stageTotal == 0) {
    return 0;
  }

  return (uint8_t)((stageWritten * 100) / stageTotal);
}

// --- Запись таблицы партиций ---

bool writePartitionTable(String &error) {
  PartitionTableImage table = {};
  buildPartitionTable(table);

  uint8_t original[4096] = {};
  if (esp_flash_read(NULL, original, PARTITION_TABLE_ADDR, sizeof(original)) != ESP_OK) {
    error = "не удалось прочитать текущую таблицу";
    return false;
  }

  if (esp_flash_erase_region(NULL, PARTITION_TABLE_SECTOR, 4096) != ESP_OK) {
    error = "не удалось стереть сектор таблицы";
    return false;
  }

  if (esp_flash_write(NULL, &table, PARTITION_TABLE_ADDR, sizeof(table)) != ESP_OK) {
    // Не получилось записать — возвращаем заводскую таблицу на место.
    esp_flash_write(NULL, original, PARTITION_TABLE_ADDR, sizeof(original));
    error = "не удалось записать таблицу";
    return false;
  }

  // Читаем обратно и сверяем: без этого нельзя считать шаг выполненным.
  uint8_t verify[sizeof(table)] = {};
  if (esp_flash_read(NULL, verify, PARTITION_TABLE_ADDR, sizeof(verify)) != ESP_OK ||
      memcmp(verify, &table, sizeof(table)) != 0) {
    Serial.println("Migration: table readback mismatch, restoring factory table");
    esp_flash_erase_region(NULL, PARTITION_TABLE_SECTOR, 4096);
    esp_flash_write(NULL, original, PARTITION_TABLE_ADDR, sizeof(original));
    error = "таблица не совпала при проверке, восстановлена заводская";
    return false;
  }

  setMigrationState(MIGRATION_STATE_TABLE_WRITTEN);
  Serial.println("Migration: partition table written and verified");
  return true;
}

// --- Переключение загрузки на новый слот (после перезагрузки с новой таблицей) ---

bool finishMigrationAfterTableWrite(String &error) {
  const esp_partition_t *target = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);

  if (target == nullptr || target->size != NEW_APP1_SIZE) {
    error = "новая разметка не применилась";
    return false;
  }

  uint8_t header[8] = {};
  if (esp_flash_read(NULL, header, target->address, sizeof(header)) != ESP_OK ||
      !looksLikeFirmware(header, sizeof(header))) {
    error = "в новом слоте нет прошивки";
    return false;
  }

  if (esp_ota_set_boot_partition(target) != ESP_OK) {
    error = "не удалось переключить загрузку";
    return false;
  }

  setMigrationState(MIGRATION_STATE_DONE);
  Serial.println("Migration: boot switched to the new slot");
  return true;
}

// Вызывается при старте: доводит до конца шаги, между которыми устройство перезагружали.
// true — начата перезагрузка, дальше работать не нужно.
bool handleMigrationBootStep() {
  int state = migrationState();

  if (state == MIGRATION_STATE_TABLE_WRITTEN) {
    String error;

    if (finishMigrationAfterTableWrite(error)) {
      Serial.println("Migration: rebooting into the new firmware");
      delay(300);
      ESP.restart();
      return true;
    }

    // Не получилось — оставляем устройство рабочим на текущей прошивке.
    Serial.print("Migration: cannot switch boot: ");
    Serial.println(error);
    setMigrationState(0);
  }

  return false;
}

#endif // PARTITION_MIGRATION_H
