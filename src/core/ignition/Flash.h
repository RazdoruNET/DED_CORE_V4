#ifndef FLASH_H
#define FLASH_H

void flashOn() {
  if (QS_start < micros()) {
    if (HIGH_OUTPUT) {
      digitalWrite(out_pin, HIGH);
    } else {
      digitalWrite(out_pin, LOW);
    }
  } else {
    // Serial.println("QUICK SHIFFTER IS ACTIVE!!!");
  }

  Flash = 1;
}

void flashOff() {
  if (HIGH_OUTPUT) {
    digitalWrite(out_pin, LOW);
  } else {
    digitalWrite(out_pin, HIGH);
  }

  Flash = 0;
}

void handleFlash()
{
  if (nextFlashMicro <= micros())
  {
    if (nextFlashMicro > 0)
    {
      if (Flash == 0)
      {
        flashOn();
        lastFlashMicro = nextFlashMicro;
        nextFlashMicro = 0;
      }
      else if (Flash == 1 && micros() >= (Init_flash_time + lastFlashMicro))
      {
        flashOff();
      }
    }
    else
    {
      if (Flash == 1 && micros() >= (Init_flash_time + lastFlashMicro))
      {
        flashOff();
      }
    }
  }
}

#endif // FLASH_H
