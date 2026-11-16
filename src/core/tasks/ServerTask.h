#ifndef SERVERTASK_H
#define SERVERTASK_H

extern HybridOTAManager otaManager;

void TaskServerCode(void * parameter) {
  initWifi();
  initWebSocket();
  initServer();
  tlsModeInit();
    
  for(;;){ 
    dnsServer.processNextRequest();

    // Обычные серверы в режиме HTTPS остановлены, а фоновые переходы на новую разметку
    // пишут во флеш — под HTTPS их не запускаем.
    if (!tlsModeHttpsActive()) {
      handleServer();
      maybeStartMigrationFromService();
      otaManager.handle();
    }

    tlsModeHandle();
    tlsHandleCertificateMaintenance();
  
    vTaskDelay(2);
  }
}

#endif // SERVERTASK_H
