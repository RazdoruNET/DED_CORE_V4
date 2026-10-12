#ifndef OTATASK_H
#define OTATASK_H

#include "core/net/DEVICE_COMMANDS.h"
#include "servers/WIFI_SERVER.h"

// Проверка обновлений и получение сертификата от сервиса.
//
// Вынесено в отдельную задачу, а не в loop(), по двум причинам:
//   - рукопожатие TLS требует около 16 КБ стека, а у задачи loop() заметно меньше;
//   - ожидание часов по NTP и скачивание образа — долгие операции, их место в стороне
//     от управления зажиганием (loop()).
//
// Задача сама ничего не начинает, пока идёт переход на новую разметку: и то и другое
// пишет во флеш.

void TaskOtaCode(void * parameter) {
  serviceEnsureTime(30000);

  for (;;) {
    if (!migrationInProgress()) {
      if (WIFI_SCAN_REQUESTED) {
        // Сканирование занимает секунды, поэтому идёт здесь, а не в обработчике команды.
        WIFI_SCAN_REQUESTED = false;
        sendDeviceFrame(buildWifiNetworksJson());
      } else {
        handleWifiAutoUpdate();
      }
    }

    // Пока владелец ждёт ответа на команду, опрашиваем чаще.
    vTaskDelay(WIFI_SCAN_REQUESTED ? 250 : 5000);
  }
}

#endif // OTATASK_H
