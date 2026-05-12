#ifndef SYSTEM_INIT_H
#define SYSTEM_INIT_H

#include <Arduino.h>
#include "config/PROPS.h"
#include "functions/SOUND.h"

void initSystemHardware() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  
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

void initSystemTasks() {
  xTaskCreatePinnedToCore(TaskServerCode,  "TaskServerHandler",  20000, NULL, 2, &TaskServerHandler,     0);
  xTaskCreatePinnedToCore(TaskControlCode, "TaskControlHandler", 1024, NULL, 1, &TaskControlHandler,     0);

  if (SOUND == true) {
    pinMode(select_cart_up_pin, OUTPUT);  
    xTaskCreatePinnedToCore(TaskAlarmCode,   "TaskAlarmHandler",   2000, NULL, 1, &TaskAlarmHandler,   0);
  }
}

#endif // SYSTEM_INIT_H
