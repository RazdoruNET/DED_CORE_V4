#ifndef SENSORS_H
#define SENSORS_H

void handlePin()
{
  datchikModulator = (INVERT_SENSOR) ? !digitalRead(sensor_pin) : digitalRead(sensor_pin);

  datchikThrottle  = analogRead(throttle_pin);

  if (ANGLE_ADVANCE != true) datchikThrottle = 0;

  float new_throttle = (INVERT_THROTTLE) ? 100 - (datchikThrottle * 100 / 4095) : datchikThrottle * 100 / 4095;

  if (Throttle < new_throttle) Throttle += 5;
  if (Throttle > new_throttle) Throttle -= 5;
    
  if (Throttle > 100) Throttle = 100;
  if (Throttle < 0)   Throttle = 0;
}

void handleRpm()
{
  switch (datchikModulator) {
    case HIGH:

      if (datchikModulator != Modulator)
      {
        int __micros = micros();

        int timePrePerDegree = oldRpmMicro - preOldRpmMicro;

        timePerDegree = __micros - oldRpmMicro;

        int one_percent = timePrePerDegree / 100;

        // На первом импульсе после старта и после сброса (500 мс без сигнала)
        // предыдущего периода ещё нет: one_percent == 0, и деление на него роняло
        // блок. Нулевой период (два фронта в одну микросекунду) отсекается ниже.
        int actual = (one_percent != 0) ? (timePerDegree / one_percent) : 0;

        if ((RPM < 200 || actual > filter || preOldRpmMicro == 0) && timePerDegree > 0)
        {
          RPM = fix_micros / timePerDegree;
          preOldRpmMicro = oldRpmMicro;
          oldRpmMicro = __micros;
          Modulator = datchikModulator;
        }
      }

      break;

    case LOW:

      int __micros = micros();

      if ((__micros - oldRpmMicro) > 500000)
      {
        RPM = 0;
        oldRpmMicro = 0;
        preOldRpmMicro = 0;
        oldIgnitionMicro = 0;
        oldIgnitionModulator = 0;
        diffMicro = 0;
      }

      Modulator = datchikModulator;
  }
}

#endif // SENSORS_H
