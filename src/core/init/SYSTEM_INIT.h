#ifndef SYSTEM_INIT_H
#define SYSTEM_INIT_H

#include <Arduino.h>
#include "core/storage/DEVICE_FS.h"
#include "config/PROPS.h"
#include "functions/SOUND.h"
#include "core/ignition/Init.h"
#include "core/tasks/TASKS.h"

// Task handler declarations
extern TaskHandle_t TaskServerHandler;
extern TaskHandle_t TaskControlHandler;
extern TaskHandle_t TaskAlarmHandler;
extern TaskHandle_t TaskOtaHandler;

void initSystemHardware() {
  // Провижининг шлёт сертификат одной строкой (~3 КБ) — дефолтных 256 байт не хватит.
  Serial.setRxBufferSize(4096);
  Serial.begin(115200);
  Serial.setDebugOutput(true);

  // Файловую систему монтируем до всего остального: там встроенный фронт,
  // сертификат TLS и настройки завода.
  initFileSystem();
  
  pinMode(select_cart_down_pin, INPUT);   
  gpio_set_pull_mode(GPIO_NUM_33, GPIO_PULLUP_ONLY);
}

void initSystemSound() {
  if (SOUND == true) {
    beep(1, 0, 150, 698.5);
    beep(1, 0, 150, 880);
    beep(1, 0, 150, 1050);
  }
}

#include "core/net/DIAGNOSTICS_UPLINK.h"

// Дескриптор задачи канала диагностики: определён в основном файле прошивки.
extern TaskHandle_t TaskDiagHandler;

void initSystemTasks() {
  if (DIAGNOSTICS_WIFI_ENABLED) {
    // Режим удалённой диагностики: ни веб-сервера, ни точки доступа — их буферы дробят
    // память, а рукопожатию TLS нужен непрерывный участок около 33 КБ. Канал работает
    // отдельной задачей со своим стеком: буферы mbedTLS живут на стеке вызывающего.
    xTaskCreatePinnedToCore(TaskDiagnosticsCode, "TaskDiagHandler", 24576, NULL, 2, &TaskDiagHandler, 0);
    xTaskCreatePinnedToCore(TaskControlCode,     "TaskControlHandler", 1024, NULL, 1, &TaskControlHandler, 0);

    return;
  }

  xTaskCreatePinnedToCore(TaskServerCode,  "TaskServerHandler",  20000, NULL, 2, &TaskServerHandler,     0);
  xTaskCreatePinnedToCore(TaskControlCode, "TaskControlHandler", 1024, NULL, 1, &TaskControlHandler,     0);
  // Стек под TLS-рукопожатие при обращении к сервису (буферы mbedTLS и разбор
  // сертификатов живут на стеке): 16 КБ не хватало, блок перезагружался на рукопожатии.
  xTaskCreatePinnedToCore(TaskOtaCode,     "TaskOtaHandler",     24576, NULL, 1, &TaskOtaHandler,        0);

  if (SOUND == true) {
    pinMode(select_cart_up_pin, OUTPUT);  
    xTaskCreatePinnedToCore(TaskAlarmCode,   "TaskAlarmHandler",   2000, NULL, 1, &TaskAlarmHandler,   0);
  }
}

#endif // SYSTEM_INIT_H
