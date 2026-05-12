#ifndef CONTROLTASK_H
#define CONTROLTASK_H

void TaskControlCode(void * parameter) {
  for(;;){ 
    handleControllPin();
    vTaskDelay(1);
  }
}

#endif // CONTROLTASK_H
