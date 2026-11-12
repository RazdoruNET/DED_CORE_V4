#ifndef IGNITION_H
#define IGNITION_H

void handleAngle()
{
  if (RPM <= rpmsArr[active_cart][0])
  {
    Advance_angle = anglesArr[active_cart][0];
    
    if (ANGLE_ADVANCE == true) 
    {
      // Угол коррекции задан в градусах, дроссель — в процентах: умножаем, потом делим.
      Advance_angle = Advance_angle + (angles_adjustmentArr[active_cart][0] * Throttle) / 100;
    }
    
    if (Servo_type == 0)
    {
      Servo_position = servo_valueArr[active_cart][0];
    }
    else if (Servo_type == 1 && RPM >= Servo_rpm)
    {
      Servo_position = Servo_percent;
    }
    else if (Servo_type == 1 && RPM < Servo_rpm)
    {
      Servo_position = 0;
    } 
  }
  else
  {
    int count = (type == 0) ? 7 : (type == 1) ? 12 : 22;
    int last = count - 1;
    int segment = last;

    // Ищем отрезок карты, в который попали обороты. Раньше условие требовало, чтобы
    // следующая точка была уже пройдена (rpmsArr[i + 1] <= RPM): угол считался по
    // предыдущему отрезку с продолжением за его конец, а для последней точки
    // читался rpmsArr[last + 1] — за границей строки карты.
    for (byte i = 0; i < last; i++)
    {
      if (RPM > rpmsArr[active_cart][i] && RPM <= rpmsArr[active_cart][i + 1])
      {
        segment = i;
        break;
      }
    }

    if (segment != last)
    {
      // Внутри отрезка rpmFrom < rpmTo (это следует из условия выше), поэтому map()
      // не делит на ноль даже на плоском «хвосте» карты.
      int rpmFrom = rpmsArr[active_cart][segment];
      int rpmTo = rpmsArr[active_cart][segment + 1];

      Advance_angle = map(RPM, rpmFrom, rpmTo, anglesArr[active_cart][segment], anglesArr[active_cart][segment + 1]);

      if (Servo_type == 0)
      {
        Servo_position = map(RPM, rpmFrom, rpmTo, servo_valueArr[active_cart][segment], servo_valueArr[active_cart][segment + 1]);
      }
    }
    else
    {
      // Обороты выше последней точки карты: угол берём с неё самой.
      Advance_angle = anglesArr[active_cart][last];

      if (Servo_type == 0)
      {
        Servo_position = servo_valueArr[active_cart][last];
      }
    }

    // Коррекции применяем один раз за вызов и по текущему отрезку: раньше они стояли
    // внутри цикла, складывались до count раз и брали значения последней точки.
    if (Servo_type == 1)
    {
      Servo_position = (Servo_rpm <= RPM) ? Servo_percent : 0;
    }

    if (ANGLE_ADVANCE == true)
    {
      // Угол коррекции задан в градусах, дроссель — в процентах.
      Advance_angle = Advance_angle + (angles_adjustmentArr[active_cart][segment] * Throttle) / 100;
    }

    if (QUICKSHIFTER == true)
    {
      QS_time = quick_adjustmentArr[active_cart][segment];
    }

    // Отсечка работает, только если она настроена: при max_rpm == 0 условие
    // «RPM >= max_rpm» истинно всегда, и ретард применялся бы на любых оборотах.
    if ((RPM > rpmsArr[active_cart][last]) || (max_rpm > 0 && RPM >= max_rpm))
    {
      Advance_angle = Advance_angle - max_rpm_angle_kiker;
    }
  }
}

void handleIgnition()
{
  if (Modulator && Modulator != oldIgnitionModulator)
  {
    int diffAngle = Init_angle - Advance_angle;
    int diffMicros = diffAngle * (timePerDegree / 360);

    nextFlashMicro = oldRpmMicro + diffMicros;
    oldIgnitionModulator = Modulator;
  }
  else if (!Modulator && Modulator != oldIgnitionModulator)
  {
    oldIgnitionModulator = Modulator;
  }
}

#endif // IGNITION_H
