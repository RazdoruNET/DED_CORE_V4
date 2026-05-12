#ifndef ALARMTASK_H
#define ALARMTASK_H

void TaskAlarmCode(void * parameter) {
  Serial.println("Start TaskAlarmCode CORE :: " + String(xPortGetCoreID()));
  for(;;){ 
    handleAlarm();
    handleRPMAlarm();
    vTaskDelay(10);
  }
}

#endif // ALARMTASK_H
