#ifndef TLS_SERVER_H
#define TLS_SERVER_H

// HTTPS/WSS сервер устройства (IDF httpd, esp_https_server). Реализация — TLS_SERVER.cpp.
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
void stopTlsServer();
bool tlsIsRunning();

// Из задачи сервера: рассылка телеметрии wss-клиентам и отложенный перезапуск после смены
// сертификата (останавливать httpd из его же задачи нельзя).
void handleTlsServer();

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

// Забрать сертификат у сервиса (нужен интернет на устройстве). Используется и
// обслуживанием, и аварийной кнопкой на странице блока.
bool tlsFetchFromService(String &error);

#endif // TLS_SERVER_H
