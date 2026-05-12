#ifndef ALARM_H
#define ALARM_H

void handleAlarm()
{
  if (off_time_alarm == true)
  {
    tone(select_cart_up_pin, 1000, 50);
    tone(select_cart_up_pin, 0, 50);
    tone(select_cart_up_pin, 1000, 50);
    tone(select_cart_up_pin, 0, 50);
    tone(select_cart_up_pin, 1000, 50);

    tone(select_cart_up_pin, 0, 500);

    tone(select_cart_up_pin, 1000, 50);
    tone(select_cart_up_pin, 0, 50);
    tone(select_cart_up_pin, 1000, 50);
    tone(select_cart_up_pin, 0, 50);
    tone(select_cart_up_pin, 1000, 50);

    tone(select_cart_up_pin, 0, 500);

    tone(select_cart_up_pin, 1000, 50);
    tone(select_cart_up_pin, 0, 50);
    tone(select_cart_up_pin, 1000, 50);
    tone(select_cart_up_pin, 0, 50);
    tone(select_cart_up_pin, 1000, 50);

    start_micros = micros();
    off_time_alarm = false;
  }
}

void handleRPMAlarm()
{  
  if ((RPM > rpmsArr[active_cart][6]) || (RPM >= max_rpm))
  {
    digitalWrite(select_cart_up_pin, HIGH);
    vTaskDelay(25);
    digitalWrite(select_cart_up_pin, LOW);
    vTaskDelay(25);
  }
}

#endif // ALARM_H
