#ifndef DEVICE_FS_H
#define DEVICE_FS_H

// Файловая система блока: вшитый интерфейс, сертификат TLS, /config.json завода.
//
// В новой разметке это LittleFS. SPIFFS в разделе 1088 КБ вмещает максимум ~780 КБ
// (проверено перебором на mkspiffs), а вшитый фронт занимает ~900 КБ — образ файловой
// системы просто не собирался. LittleFS укладывает тот же фронт и оставляет ~150 КБ
// запаса: она не тратит ёмкость на индекс больших файлов.
//
// В сборке перехода остаётся SPIFFS: она работает с заводской разметкой и читает
// /config.json из заводской SPIFFS. Любая другая ФС на этом разделе отформатировала бы
// его, и после обновления владелец получил бы заводские карты вместо своей настройки.
//
// Обе ФС дают одинаковый API (begin/open/exists/remove/end), поэтому остальной код
// работает с DEVICE_FS и не знает, какая именно ФС под ним.

#ifdef DED_CORE_LEGACY_LAYOUT
#include <SPIFFS.h>
#define DEVICE_FS SPIFFS
#define DEVICE_FS_NAME "SPIFFS"
#else
#include <LittleFS.h>
#define DEVICE_FS LittleFS
#define DEVICE_FS_NAME "LittleFS"
#endif

// Монтирует файловую систему и печатает занятое место: иначе «интерфейс не залит»
// выглядит как пустая страница блока без единого слова в логе.
inline bool initFileSystem() {
  if (!DEVICE_FS.begin(true)) {
    Serial.print(DEVICE_FS_NAME);
    Serial.println(" mount failed");
    return false;
  }

  Serial.print(DEVICE_FS_NAME);
  Serial.print(": занято ");
  Serial.print((unsigned long)DEVICE_FS.usedBytes());
  Serial.print(" из ");
  Serial.print((unsigned long)DEVICE_FS.totalBytes());
  Serial.println(" байт");

  if (!DEVICE_FS.exists("/index.html")) {
    Serial.println("Внимание: вшитый интерфейс не найден (/index.html)");
  }

  return true;
}

#endif
