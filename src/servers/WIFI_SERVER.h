#ifndef WIFI_SERVER_H
#define WIFI_SERVER_H

#include <WiFi.h>
#include <DNSServer.h>
#include "config/PROPS.h"
#include "core/ota/HYBRID_OTA.h"
#include "core/net/DEVICE_COMMANDS.h"
#include "core/net/SERVICE_CLIENT.h"

extern Preferences preferences;
extern HybridOTAManager otaManager;
extern DNSServer dnsServer;

// Сколько памяти нужно рукопожатию TLS одним куском (буферы mbedTLS 16+16 КБ и запас).
#define DED_CORE_TLS_BLOCK_NEEDED 40000

// Сколько ждём домашнюю сеть и часы на старте, прежде чем продолжить загрузку без них.
#define DED_CORE_STARTUP_CONNECT_MS 12000
#define DED_CORE_STARTUP_TIME_MS 15000

// AP-DNS: в офлайне у телефона нет доступа к публичному DNS, поэтому имя устройства
// (core.dedbox.pro) резолвим сами на адрес точки доступа — иначе https:// откроется
// с предупреждением или не откроется вовсе.
void initApDns() {
  dnsServer.start(53, "*", WiFi.softAPIP());
  Serial.println("AP-DNS started");
}

// Forward declarations
bool connectToConfiguredWifi();
void handleWifiAutoUpdate();

void runStartupUpdateCheck();

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

      // Буферам mbedTLS (33 КБ) нужен один непрерывный кусок памяти, а рядом с Bluetooth
      // его обычно нет. Отдаём память Bluetooth — но только если к блоку не подключено
      // приложение: обрывать живую сессию владельца ради проверки нельзя. Если памяти
      // всё равно не хватит, проверка просто отложится до следующего включения блока,
      // где она идёт до подъёма Bluetooth и точки доступа (runStartupUpdateCheck).
      bool bleReleased = false;

#ifndef DED_CORE_NO_BLE
      if (!bleClientConnected() && ESP.getMaxAllocHeap() < DED_CORE_TLS_BLOCK_NEEDED) {
        bleReleased = releaseBleMemory();
      }
#endif

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

      // Удачный ответ сервиса запоминаем в секундах эпохи: следующее включение блока
      // проверку не повторяет, пока не пройдёт интервал.
      if (time(nullptr) >= (time_t)SERVICE_MIN_VALID_TIME) {
        WIFI_LAST_CHECK_EPOCH = (unsigned long)time(nullptr);
        preferences.putULong("wifi_check_at", WIFI_LAST_CHECK_EPOCH);
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

// Обновление на старте блока: подключиться к домашней сети, выставить часы, проверить
// обновление и при наличии — скачать его. Вызывается до запуска задач (Bluetooth, точка
// доступа, веб-сервер), пока в памяти есть непрерывный участок: рукопожатию TLS нужно
// около 33 КБ одним куском, а эти подсистемы дробят память так, что в рабочем режиме
// такого участка уже не найти.
//
// Работает только с разрешённым автообновлением и заданной сетью. Долгие ожидания
// ограничены: сеть может быть недоступна, и держать блок без точки доступа нельзя.
void runStartupUpdateCheck() {
  if (!WIFI_AUTO_UPDATE_ENABLED || WIFI_UPDATE_SSID.length() == 0) {
    return;
  }

  static const unsigned long CHECK_INTERVAL_SECONDS = WIFI_CHECK_INTERVAL_MS / 1000;

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_UPDATE_SSID.c_str(), WIFI_UPDATE_PASSWORD.c_str());

  Serial.print("Startup: connecting to ");
  Serial.println(WIFI_UPDATE_SSID);

  unsigned long started = millis();
  bool connected = false;

  while (millis() - started < DED_CORE_STARTUP_CONNECT_MS) {
    if (WiFi.status() == WL_CONNECTED) {
      connected = true;
      break;
    }

    if (WiFi.status() == WL_NO_SSID_AVAIL || WiFi.status() == WL_CONNECT_FAILED) {
      break;
    }

    delay(100);
  }

  if (!connected) {
    Serial.println("Startup: home network is not available, skipping update check");
    return;
  }

  Serial.println("Startup: connected, checking for update");

  if (!serviceEnsureTime(DED_CORE_STARTUP_TIME_MS)) {
    Serial.println("Startup: no clock, skipping update check");
    return;
  }

  // Проверку повторяем не чаще интервала: время последней удачной хранится в секундах
  // эпохи, поэтому его можно сравнивать после перезагрузки.
  unsigned long now = (unsigned long)time(nullptr);

  if (WIFI_LAST_CHECK_EPOCH != 0 && (now - WIFI_LAST_CHECK_EPOCH) < CHECK_INTERVAL_SECONDS) {
    Serial.println("Startup: update check is not due yet");
    return;
  }

  WIFI_LAST_CHECK_TIME = millis();
  WIFI_UPDATE_STATUS = "checking_updates";

  // На старте подсистем ещё нет, но сторожевой таймер ядра 0 уже работает: рукопожатие
  // считает криптографию несколько секунд подряд, не уступая процессор.
  disableCore0WDT();

  bool updateAvailable = otaManager.checkForUpdate();

  if (updateAvailable) {
    WIFI_UPDATE_STATUS = "update_available";
    Serial.println("Startup: update available, installing");
    otaManager.performUpdate();
  } else {
    WIFI_UPDATE_STATUS = "up_to_date";
  }

  enableCore0WDT();

  WIFI_LAST_CHECK_EPOCH = (unsigned long)time(nullptr);
  preferences.putULong("wifi_check_at", WIFI_LAST_CHECK_EPOCH);
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
