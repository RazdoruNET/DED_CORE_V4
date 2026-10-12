#ifndef SERVERTASK_H
#define SERVERTASK_H

extern HybridOTAManager otaManager;

void TaskServerCode(void * parameter) {
  initWifi();
  initWebSocket();
  initServer();
  startTlsServer();
    
  for(;;){ 
    dnsServer.processNextRequest();
    handleServer();
    handleTlsServer();
    tlsHandleCertificateMaintenance();
    maybeStartMigrationFromService();
    otaManager.handle();
  
    vTaskDelay(2);
  }
}

#endif // SERVERTASK_H
