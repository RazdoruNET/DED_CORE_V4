#ifndef TLS_SERVER_H
#define TLS_SERVER_H

// HTTPS/WSS сервер устройства (IDF httpd + свой TLS-транспорт). Реализация — TLS_SERVER.cpp.
//
// Зачем: PWA открывается по HTTPS, а браузер запрещает ws:// со страницы HTTPS (mixed content).
// Значит блок должен отдавать wss://, а для этого нужен сертификат, которому доверяет браузер.
//
// Сертификат выпускаем МЫ (Let's Encrypt DNS-01 на core.dedbox.pro) и кладём на устройство
// файлами /cert.pem и /key.pem в файловой системе. В офлайне DNS/интернет не нужны:
// имя резолвит AP-DNS
// устройства (initApDns в servers/WIFI_SERVER.h), доверие проверяет браузер телефона.
//
// Плейн-HTTP (80) и WS (81) остаются fallback-ом для уже выпущенных фронтов.
//
// Реализация вынесена в .cpp: заголовки IDF (esp_http_server.h) и ESPAsyncWebServer
// объявляют несовместимые перечисления HTTP_*, в одной единице трансляции они не уживаются.

#include <Arduino.h>

// Домен сертификата; AP-DNS резолвит его на адрес устройства.
#define DED_CORE_TLS_HOST    "core.dedbox.pro"
#define DED_CORE_TLS_WS_PATH "/ws"
#define DED_CORE_TLS_PORT    443

// За сколько дней до истечения начинаем менять сертификат сами.
#define DED_CORE_TLS_RENEW_DAYS 14

// Время окончания сертификата (секунды UNIX), 0 — неизвестно.
#define DED_CORE_TLS_EXPIRY_KEY "tls_not_after"

// true — HTTPS/WSS поднялся. Без сертификата устройство остаётся на HTTP (80) + WS (81).
bool startTlsServer();

// Арбитраж памяти между Bluetooth и HTTPS.
//
// NimBLE с контроллером занимает ~100 КБ, TLS-серверу нужно ~20 КБ плюс буферы сессии.
// Вместе они в кучу ESP32 не помещаются, поэтому владелец памяти один: по умолчанию
// Bluetooth (блок виден приложению), а HTTPS включается по запросу — на время сессии
// Bluetooth отпускает память и возвращает её после простоя.
// Останов/запуск обычных серверов (HTTP 80 + WebSocket 81). Пока работает HTTPS,
// они не нужны и занимают память; реализация — servers/WEB_SERVER_FUNCS.h.
void stopPlainWebServers();
void startPlainWebServers();

// Реализация — servers/WEB_SERVER_FUNCS.h (там доступен модуль сопряжения).
void dedModuleForget();
String dedModulePairStart();
String dedModulePairStop();
void startPlainWebServers();

void tlsModeInit();          // вызывается один раз при старте сервера
void tlsModeHandle();        // вызывается в цикле задачи сервера
void tlsModeRequestHttps();  // запрос HTTPS (сокет 443 или POST /api/https/start)
void tlsModeRequestBle();    // вернуть Bluetooth (POST /api/ble/start)
bool tlsModeHttpsActive();   // true — сейчас работает HTTPS, Bluetooth отпущен
void stopTlsServer();
bool tlsIsRunning();

// Из задачи сервера: рассылка телеметрии wss-клиентам и отложенный перезапуск после смены
// сертификата (останавливать httpd из его же задачи нельзя).
void handleTlsServer();

// Кадр wss-клиентам: команды блока (например, список сетей) отвечают не только
// запросившему, поэтому ответ уходит по всем каналам сразу.
void tlsBroadcastFrame(const String &json);

// Приём сертификата: {"cert":"…PEM…","key":"…PEM…","notAfter":<секунды UNIX>}
// Пишет /cert.pem и /key.pem в файловую систему и помечает сервер на перезапуск.
// При ошибке заполняет error.
bool applyTlsMaterial(const String &body, String &error);

// Сколько дней осталось сертификату: -1 — срок неизвестен, -2 — часы ещё не выставлены.
int tlsCertificateDaysLeft();
uint32_t tlsCertificateExpiry();

// Вызывается из задачи сервера: выставляет часы (NTP) и сама меняет сертификат
// за DED_CORE_TLS_RENEW_DAYS до истечения, если у устройства есть интернет.
void tlsHandleCertificateMaintenance();

// Забрать сертификат у сервиса (нужен интернет на устройстве). Работает на старте
// блока, пока веб-сервер и точка доступа ещё не заняли память.
bool tlsFetchFromService(String &error);

// Проверка сертификата на старте: если файла нет или до истечения меньше
// DED_CORE_TLS_RENEW_DAYS, сертификат забирается у сервиса. Вызывается из setup()
// до initSystemTasks(): рукопожатию TLS нужно ~33 КБ одним куском, а после подъёма
// веб-сервера такого участка уже нет.
void tlsStartupCertificateCheck();

// Запросить обновление сертификата при следующей загрузке: ставит отметку в NVS и
// перезагружает блок из задачи сервера. В рабочем режиме рукопожатие к сервису
// не помещается в кучу, а на старте — помещается.
void tlsRequestRefreshOnBoot();

#endif // TLS_SERVER_H
