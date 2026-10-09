#ifndef SERVERTASK_H
#define SERVERTASK_H

extern HybridOTAManager otaManager;

void TaskServerCode(void * parameter) {
  initWifi();
  initWebSocket();
  initServer();
    
  for(;;){ 
    dnsServer.processNextRequest();
    handleServer();
    otaManager.handle();
  
    vTaskDelay(2);
  }
}

#endif // SERVERTASK_H
