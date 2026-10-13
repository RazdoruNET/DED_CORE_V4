#ifndef WIFI_SERVER_H
#define WIFI_SERVER_H

#include <WiFi.h>
#include <DNSServer.h>
#include "config/PROPS.h"
#include "core/ota/HYBRID_OTA.h"
#include "core/net/DEVICE_COMMANDS.h"

extern Preferences preferences;
extern HybridOTAManager otaManager;
extern DNSServer dnsServer;

// Сколько памяти нужно рукопожатию TLS одним куском (буферы mbedTLS 16+16 КБ и запас).
#define DED_CORE_TLS_BLOCK_NEEDED 40000

// Forward declarations
bool connectToConfiguredWifi();
void handleWifiAutoUpdate();

// AP-DNS: в офлайне у телефона нет доступа к публичному DNS, поэтому имя устройства
// (core.dedbox.pro) резолвим сами на адрес точки доступа — иначе https:// откроется
// с предупреждением или не откроется вовсе.
void initApDns()
{
  dnsServer.start(53, "*", WiFi.softAPIP());
  Serial.println("AP-DNS started");
}

void initWifi() 
{
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(true);
  WiFi.softAP(APSSID1, APPSK1);
  WIFI_UPDATE_STATUS = "ap_ready";

  initApDns();

  Serial.printf("WiFi: auto=%d ssid='%s' pass=%d sta=%d\n",
                WIFI_AUTO_UPDATE_ENABLED ? 1 : 0, WIFI_UPDATE_SSID.c_str(),
                WIFI_UPDATE_PASSWORD.length() > 0 ? 1 : 0, (int)WiFi.status());

  if (WIFI_AUTO_UPDATE_ENABLED && WIFI_UPDATE_SSID.length() > 0) {
    connectToConfiguredWifi();
  }
}

bool connectToConfiguredWifi() {
  if (!WIFI_AUTO_UPDATE_ENABLED || WIFI_UPDATE_SSID.length() == 0) {
    return false;
  }

  if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == WIFI_UPDATE_SSID) {
    WIFI_UPDATE_STATUS = "connected";
    return true;
  }

  if (WIFI_LAST_ATTEMPT_TIME != 0 && (millis() - WIFI_LAST_ATTEMPT_TIME) < WIFI_RETRY_INTERVAL_MS) {
    return false;
  }

  WIFI_LAST_ATTEMPT_TIME = millis();
  WIFI_UPDATE_STATUS = "connecting";

  Serial.print("Connecting to configured WiFi: ");
  Serial.println(WIFI_UPDATE_SSID);

  // Подключаемся напрямую, без предварительного сканирования: асинхронный
  // WiFi.scanNetworks(true) возвращает признак «идёт скан» и список оставался пустым,
  // поэтому автообновление никогда не находило сеть. Статус подключения esp-idf
  // сообщает сам: нет сети — WL_NO_SSID_AVAIL, не подошёл пароль — WL_CONNECT_FAILED.
  WiFi.begin(WIFI_UPDATE_SSID.c_str(), WIFI_UPDATE_PASSWORD.c_str());

  unsigned long startTime = millis();
  while (millis() - startTime < WIFI_CONNECT_TIMEOUT_MS) {
    wl_status_t wifiStatus = WiFi.status();

    if (wifiStatus == WL_CONNECTED) {
      WIFI_UPDATE_STATUS = "connected";
      Serial.println("Connected to configured WiFi network");
      return true;
    }

    if (wifiStatus == WL_NO_SSID_AVAIL) {
      WIFI_UPDATE_STATUS = "network_not_found";
      Serial.println("Configured WiFi network not found");
      return false;
    }

    if (wifiStatus == WL_CONNECT_FAILED) {
      WIFI_UPDATE_STATUS = "connect_failed";
      Serial.println("Configured WiFi rejected the password");
      return false;
    }

    delay(100);
  }

  WIFI_UPDATE_STATUS = "connect_failed";
  return false;
}

void handleWifiAutoUpdate() {
  if (!WIFI_AUTO_UPDATE_ENABLED || UPDATE_MODE || WIFI_UPDATE_SSID.length() == 0) {
    static unsigned long lastReport = 0;

    if (millis() - lastReport > 30000) {
      lastReport = millis();
      Serial.printf("Auto-update off: auto=%d mode=%d ssid='%s'\n",
                    WIFI_AUTO_UPDATE_ENABLED ? 1 : 0, UPDATE_MODE ? 1 : 0, WIFI_UPDATE_SSID.c_str());
    }

    return;
  }

  if (RPM > 100) {
    WIFI_UPDATE_STATUS = "motor_running";
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (WIFI_LAST_CHECK_TIME == 0 || (millis() - WIFI_LAST_CHECK_TIME) >= WIFI_CHECK_INTERVAL_MS) {
      WIFI_LAST_CHECK_TIME = millis();
      WIFI_UPDATE_STATUS = "checking_updates";

      // Буферам mbedTLS (33 КБ) нужен один непрерывный кусок памяти, и рядом с Bluetooth
      // его может не найтись: тогда отдаём память Bluetooth на время запроса и поднимаем
      // стек обратно. Обновление перезагружает блок, поэтому возвращать Bluetooth после
      // скачивания не нужно.
      bool bleReleased = false;

      if (ESP.getMaxAllocHeap() < DED_CORE_TLS_BLOCK_NEEDED) {
#ifndef DED_CORE_NO_BLE
        bleReleased = releaseBleMemory();
#endif
      }

      // Рукопожатие TLS считает криптографию несколько секунд подряд, не уступая
      // процессор: сторожевой таймер ядра 0 (5 секунд) принимает это за зависание
      // и перезагружает блок. На время запроса снимаем его с дежурства — у запроса
      // свои таймауты, — и возвращаем сразу после.
      disableCore0WDT();

      bool updateAvailable = otaManager.checkForUpdate();

      if (updateAvailable) {
        WIFI_UPDATE_STATUS = "update_available";
        otaManager.performUpdate();
      } else {
        WIFI_UPDATE_STATUS = "up_to_date";
      }

      enableCore0WDT();

      if (bleReleased) {
#ifndef DED_CORE_NO_BLE
        restoreBleMemory();
#endif
      }
    }

    return;
  }

  connectToConfiguredWifi();
}

void initRecoveryWifi() 
{
  WiFi.mode(WIFI_AP);
  
  String recovery_wifi_sid = "DED_RECOVERY";
  String recovery_wifi_password = "1234567890";

  WiFi.softAP(recovery_wifi_sid, recovery_wifi_password);
  
  // Serial.print("Setting recovery soft-AP ... ");
  // Serial.println(recovery_wifi_sid);
  // Serial.println(recovery_wifi_password);
  // Serial.println(WiFi.softAP(recovery_wifi_sid, recovery_wifi_password) ? "Ready" : "Failed!");
  // Serial.print("Recovery Soft-AP IP address = ");         
  // Serial.println(WiFi.softAPIP());
}

#endif // WIFI_SERVER_H
