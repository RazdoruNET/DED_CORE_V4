#ifndef IGNITION_H
#define IGNITION_H

void handleAngle()
{
  if (RPM <= rpmsArr[active_cart][0])
  {
    Advance_angle = anglesArr[active_cart][0];
    
    if (ANGLE_ADVANCE == true) 
    {
      Advance_angle = Advance_angle + ((angles_adjustmentArr[active_cart][0] / 100) * Throttle);
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

    int index = 0;

    // Требует рефаторинга!!!!
    for (byte i = 0; i < count; i++)
    {

      const int __index_off = (count - i);

      switch (__index_off) {
        case 0:
          if (RPM > rpmsArr[active_cart][i])
          {
            Advance_angle = anglesArr[active_cart][i];
            if (Servo_type == 0)
            {
              Servo_position = servo_valueArr[active_cart][i];
            }
          }
          break;

        default:
          if (RPM > rpmsArr[active_cart][i] && rpmsArr[active_cart][i + 1] <= RPM)
          {
            Advance_angle = map(RPM, rpmsArr[active_cart][i], rpmsArr[active_cart][i + 1], anglesArr[active_cart][i], anglesArr[active_cart][i + 1]);
            
            if (Servo_type == 0)
            {
              Servo_position = map(RPM, rpmsArr[active_cart][i], rpmsArr[active_cart][i + 1], servo_valueArr[active_cart][i], servo_valueArr[active_cart][i + 1]);
            } 
          }
          break;
      }

      if (Servo_type == 1 && Servo_rpm <= RPM)
      {
        Servo_position = Servo_percent;
      }
      
      else if (Servo_type == 1 && Servo_rpm > RPM)
      {
        Servo_position = 0;
      }
      
      if (ANGLE_ADVANCE == true) {
        Advance_angle = Advance_angle + ((angles_adjustmentArr[active_cart][i] / 100) * Throttle);
      }
      
      if (QUICKSHIFTER == true) {
        QS_time = quick_adjustmentArr[active_cart][i];
      }

      if ((RPM > rpmsArr[active_cart][count-1]) || (RPM >= max_rpm))
      {
        Advance_angle = Advance_angle - max_rpm_angle_kiker;
      }      
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
