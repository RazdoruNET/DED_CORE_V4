#ifndef SERVERTASK_H
#define SERVERTASK_H

void TaskServerCode(void * parameter) {
  initWifi();
  initWebSocket();
  initServer();
    
  for(;;){ 
    dnsServer.processNextRequest();
    handleServer();
  
    vTaskDelay(2);
  }
}

#endif // SERVERTASK_H
