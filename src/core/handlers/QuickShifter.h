#ifndef QUICKSHIFTER_H
#define QUICKSHIFTER_H

void handleQuickShifterControllPin() 
{
  int quickshifter  = digitalRead(quickshifter_pin);
  
  if (quickshifter == LOW)
  {
    if (quickshifter != quickshifter_proces) 
    {
      QS_start = micros() + (QS_time * 1000);
      
      // Serial.println("QuickShifter!!! - " + String(micros()) + " | " + String(QS_start));
    }
  }

  quickshifter_proces = quickshifter;
} 

#endif // QUICKSHIFTER_H
