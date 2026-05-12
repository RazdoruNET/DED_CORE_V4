#ifndef RECOVERY_MODE_H
#define RECOVERY_MODE_H

#include <Arduino.h>
#include "config/PROPS.h"

bool isRecoveryModeActivated() {
  int down_btn = digitalRead(select_cart_down_pin);
  return down_btn == LOW;
}

void enterRecoveryMode() {
  Serial.println("Entering recovery mode");
  initRecoveryWifi();
  initRecoveryServer();
}

#endif // RECOVERY_MODE_H
