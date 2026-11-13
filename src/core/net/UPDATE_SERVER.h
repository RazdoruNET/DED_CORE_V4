#ifndef UPDATE_SERVER_H
#define UPDATE_SERVER_H

// Обновление прошивки и файловой системы по воздуху.
//
// Зачем это в DED CORE V4: на заводских устройствах обновление по воздуху делала
// ElegantOTA, и именно через неё устройства и будут переводиться на новую прошивку.
// Если в новой прошивке такого пути не будет, следующее обновление потребует кабеля —
// а устройства стоят у людей, иногда годами без доступа.
//
// Загрузка идёт сырым телом запроса (без multipart): страница /update берёт файл
// и отправляет его целиком, разбирать multipart на устройстве не нужно.
//
// Права: загрузка разрешена либо из сети самой точки доступа (192.168.4.x — туда
// пускают только по паролю точки доступа), либо с заголовком X-Device-Key. Так сосед
// по домашней сети не зальёт в блок чужую прошивку.
//
// Обновление пишется в неактивный слот: если питание пропадёт, устройство
// загрузит прежнюю прошивку.

#include <Arduino.h>
#include <Update.h>
#include <ESPAsyncWebServer.h>
#include "config/PROPS.h"
#include "core/ota/HYBRID_OTA.h"

extern HybridOTAManager otaManager;

// Страница обновления. Лежит в самой прошивке, а не в файловой системе: путь к обновлению
// должен работать и тогда, когда файловая система стёрта или не смонтирована.
static const char DED_CORE_UPDATE_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>DED CORE — обновление</title>
<style>
body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:20px}
h1{font-size:20px;margin:0 0 12px}
.card{background:#1c1c1c;border:1px solid #333;border-radius:10px;padding:16px;margin-bottom:14px}
label{display:block;margin:10px 0 6px;font-size:14px;color:#aaa}
input[type=file]{width:100%;color:#eee}
button{margin-top:14px;background:#c8102e;color:#fff;border:0;border-radius:8px;padding:12px 18px;font-size:15px}
button:disabled{opacity:.5}
pre{white-space:pre-wrap;font-size:13px;color:#9ad}</style></head>
<body>
<h1>DED CORE — обновление по воздуху</h1>
<div class="card">
  <label>Прошивка (.bin)</label>
  <input id="fw" type="file" accept=".bin">
  <button id="go">Обновить</button>
  <label>Файловая система (.bin, встроенный веб-интерфейс)</label>
  <input id="fs" type="file" accept=".bin">
  <button id="gofs">Обновить файловую систему</button>
</div>
<div class="card">
  <h2 style="font-size:16px;margin:0 0 8px">Состояние</h2>
  <div class="row"><span class="dim">Прошивка:</span><span id="fw">—</span></div>
  <div class="row"><span class="dim">HTTPS:</span><span id="tls">—</span></div>
  <div class="row"><span class="dim">Сертификат:</span><span id="cert">—</span></div>
  <div class="row"><span class="dim">Сеть с интернетом:</span><span id="wifi">—</span></div>
</div>

<div class="card">
  <h2 style="font-size:16px;margin:0 0 8px">Сертификат HTTPS</h2>
  <p style="font-size:13px;color:#aaa;margin:0 0 6px">
    Нужен, чтобы приложение подключалось к блоку по WiFi без предупреждения о небезопасном
    соединении. Если интернета у блока нет — скачайте <code>cert.json</code> в приложении
    и выберите его здесь (или отдельно <code>cert.pem</code> и <code>key.pem</code>).
  </p>
  <label>Файл сертификата (cert.json, либо cert.pem)</label>
  <input id="certFile" type="file" accept=".json,.pem,.crt,.cer">
  <label>Приватный ключ (key.pem) — если выбран не json</label>
  <input id="keyFile" type="file" accept=".pem,.key">
  <button id="installCert">Установить сертификат</button>
  <button id="fetchCert" style="background:#333">Получить у сервиса</button>
</div>

<div class="card">
  <h2 style="font-size:16px;margin:0 0 8px">Привязка к учётной записи</h2>
  <div class="row"><span class="dim">MAC блока:</span><span id="mac">—</span></div>
  <div class="row"><span class="dim">Код сопряжения:</span><span id="pairCode">—</span></div>
  <p style="font-size:13px;color:#aaa;margin:8px 0 6px">
    Введите MAC и этот код в приложении DED BOX — оно выдаст ключ устройства.
    Вставьте ключ здесь, чтобы блок начал получать обновления.
  </p>
  <label>Ключ устройства</label>
  <input id="pairKey" type="text" placeholder="64 символа" style="width:100%">
  <button id="pairGo">Привязать</button>
</div>

<div class="card"><pre id="log">Готово к обновлению.</pre></div>
<script>
const log = t => document.getElementById('log').textContent = t;
// Ключ устройства вводится в поле «Привязка» и уходит заголовком: без него блок
// отказывает в обновлении и в установке сертификата (см. updateRequestAllowed).
function authHeaders(){
  const k = (document.getElementById('pairKey').value || '').trim();
  return k ? {'X-Device-Key': k} : {};
}
async function send(input, target){
  const f = input.files[0];
  if(!f){ log('Выберите файл'); return; }
  log('Отправляем ' + f.name + ' (' + Math.round(f.size/1024) + ' КБ)… не выключайте питание');
  try{
    const r = await fetch('/api/update?target=' + target, {method:'POST', body:f, headers:authHeaders()});
    log(await r.text());
  }catch(e){ log('Ошибка: ' + e); }
}
document.getElementById('go').onclick = function(){
  this.disabled = true; send(document.getElementById('fw'), 'app');
};
document.getElementById('gofs').onclick = function(){
  this.disabled = true; send(document.getElementById('fs'), 'fs');
};

async function refreshStatus(){
  try{
    const s = await (await fetch('/api/status', {headers:authHeaders()})).json();
    document.getElementById('fw').textContent = s.firmware || '—';
    document.getElementById('tls').textContent = s.tls ? 'работает' : 'выключен (HTTP)';
    document.getElementById('tls').className = 'row ' + (s.tls ? 'ok' : 'bad');
    let c = 'нет';
    if(s.cert){
      c = 'установлен';
      if(s.certDaysLeft === -1) c += ', срок неизвестен';
      else if(s.certDaysLeft === -2) c += ', часы не выставлены';
      else if(s.certDaysLeft < 0) c = 'истёк';
      else c += ', осталось дней: ' + s.certDaysLeft;
    }
    document.getElementById('cert').textContent = c;
    document.getElementById('cert').className = 'row ' + (s.cert && s.certDaysLeft !== 0 && s.certDaysLeft > 0 ? 'ok' : 'bad');
    document.getElementById('wifi').textContent = s.wifiConnected ? 'да' : 'нет (' + (s.wifiStatus || '—') + ')';
    document.getElementById('mac').textContent = s.mac || '—';
    document.getElementById('pairCode').textContent = s.pairCode || '—';
    document.getElementById('pairGo').textContent = s.paired ? 'Привязан — заменить ключ' : 'Привязать';
  }catch(e){ /* страница работает и без статуса */ }
}

document.getElementById('installCert').onclick = async function(){
  const c = document.getElementById('certFile').files[0];
  const k = document.getElementById('keyFile').files[0];
  if(!c){ log('Выберите файл сертификата'); return; }
  this.disabled = true;
  try{
    let payload;
    if(c.name.toLowerCase().endsWith('.json')){
      payload = await c.text();
    }else{
      if(!k){ log('Нужен ещё и файл ключа key.pem'); this.disabled = false; return; }
      payload = JSON.stringify({cert: await c.text(), key: await k.text()});
    }
    log('Отправляем сертификат…');
    const r = await fetch('/api/tls', {method:'POST', body:payload, headers:authHeaders()});
    log(await r.text());
    setTimeout(refreshStatus, 1500);
  }catch(e){ log('Ошибка: ' + e); }
  this.disabled = false;
};

document.getElementById('fetchCert').onclick = async function(){
  this.disabled = true;
  log('Запрашиваем сертификат у сервиса…');
  try{
    const r = await fetch('/api/tls/refresh', {method:'POST', headers:authHeaders()});
    log(await r.text());
    setTimeout(refreshStatus, 1500);
  }catch(e){ log('Ошибка: ' + e); }
  this.disabled = false;
};

document.getElementById('pairGo').onclick = async function(){
  const key = document.getElementById('pairKey').value.trim();
  if(key.length < 32){ log('Вставьте ключ устройства из приложения'); return; }
  this.disabled = true;
  try{
    const r = await fetch('/api/provision', {method:'POST', body:JSON.stringify({key:key}), headers:authHeaders()});
    log(await r.text());
    setTimeout(refreshStatus, 500);
  }catch(e){ log('Ошибка: ' + e); }
  this.disabled = false;
};

refreshStatus();
</script>
</body></html>)HTML";

static bool updateInProgress = false;
static bool updateAuthorized = false;

// Запрос пришёл из сети самой точки доступа. Это НЕ аутентификация: подсеть 192.168.4.0/24
// — просто адрес по умолчанию у ESP32, а пароль точки доступа знает каждый, кто к ней
// подключился. Поэтому признак используется только как «устройство ещё не привязано,
// настраивать нечем» — см. updateRequestAllowed.
// Определение — core/net/MODULE_PAIRING.h: модуль сопряжения подключается позже,
// поэтому объявляем проверку здесь.
bool devicePairingWindowOpen();

bool updateFromAccessPoint(AsyncWebServerRequest *request) {
  // Во время окна сопряжения модулей сеть блока открыта — «из сети блока» ничего
  // не доказывает, образ принимаем только по ключу устройства.
  if (devicePairingWindowOpen()) {
    return false;
  }

  // См. DEVICE_API_ROUTES.h: подсеть клиента ничего не доказывает, сравниваем
  // локальный адрес соединения с адресом точки доступа.
  return request->client()->localIP() == WiFi.softAPIP();
}

// Ключ устройства сравниваем в постоянном времени: длина известна и так, а ранний
// выход на первом несовпадении даёт лишний канал.
bool updateKeyEquals(const String &a, const String &b) {
  if (a.length() == 0 || a.length() != b.length()) {
    return false;
  }

  unsigned char diff = 0;

  for (size_t i = 0; i < a.length(); i++) {
    diff |= static_cast<unsigned char>(a[i] ^ b[i]);
  }

  return diff == 0;
}

bool updateRequestAllowed(AsyncWebServerRequest *request) {
  String deviceKey = otaManager.getDeviceKey();

  // Привязанный блок принимает образ только от владельца с ключом — из точки доступа
  // тоже. Иначе любой, кто узнал пароль AP (а он печатается в лог и живёт в NVS),
  // мог бы залить свою прошивку поверх штатной.
  if (deviceKey.length() > 0) {
    if (!request->hasHeader("X-Device-Key")) {
      return false;
    }

    return updateKeyEquals(request->getHeader("X-Device-Key")->value(), deviceKey);
  }

  // Ключа нет — блок не привязан. Обновление разрешаем только из его же сети:
  // это первичная настройка, и других учётных данных у блока пока нет.
  return updateFromAccessPoint(request);
}

void registerUpdateRoutes(AsyncWebServer *server) {
  server->on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", DED_CORE_UPDATE_PAGE);
  });

  server->on("/api/update", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      // Итог отдаём, когда тело уже принято и записано (см. обработчик тела).
      if (!updateAuthorized) {
        // Ответ уже отправлен в обработчике тела (нет прав, идёт обновление или
        // работает двигатель); здесь дублировать его нельзя.
        return;
      }

      if (!updateInProgress) {
        request->send(200, "text/plain", "Обновление завершено, устройство перезагружается.");
      }
    },
    nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        updateAuthorized = updateRequestAllowed(request) &&
                           !otaManager.isUpdating() &&
                           RPM <= 100;

        if (!updateAuthorized) {
          // Ответ отправляем сразу: иначе клиент видит только оборванное соединение.
          if (!updateRequestAllowed(request)) {
            request->send(403, "text/plain", "Нужен ключ устройства (X-Device-Key)");
          } else if (RPM > 100) {
            request->send(409, "text/plain", "Двигатель работает — обновление отложено");
          } else {
            request->send(409, "text/plain", "Обновление уже идёт");
          }

          return;
        }

        bool toFileSystem = request->hasParam("target") &&
                            request->getParam("target")->value() == "fs";

        int command = toFileSystem ? U_SPIFFS : U_FLASH;

        // Если вызывающая сторона передала md5, образ проверяется по нему: без этого
        // в блок уходит ровно то, что прислали, и сверить содержимое будет нечем.
        if (request->hasParam("md5")) {
          String md5 = request->getParam("md5")->value();
          md5.trim();
          md5.toLowerCase();

          if (md5.length() != 32) {
            Serial.println("Update: md5 parameter must be 32 hex characters");
            updateAuthorized = false;
            return;
          }

          Update.setMD5(md5.c_str());
        }

        // Update сам откажется, если образ больше неактивного слота.
        if (!Update.begin(total, command)) {
          Serial.print("Update: begin failed: ");
          Serial.println(Update.errorString());
          updateInProgress = false;
          return;
        }

        updateInProgress = true;
        Serial.print("Update: started, ");
        Serial.print(total);
        Serial.println(" bytes");
      }

      if (!updateAuthorized || !updateInProgress) {
        return;
      }

      if (Update.write(data, len) != len) {
        Serial.print("Update: write failed: ");
        Serial.println(Update.errorString());
        Update.abort();
        updateInProgress = false;
        return;
      }

      if (index + len >= total) {
        if (Update.end(true)) {
          Serial.println("Update: done, rebooting");
          updateInProgress = false;
          delay(500);
          ESP.restart();
        } else {
          Serial.print("Update: end failed: ");
          Serial.println(Update.errorString());
          updateInProgress = false;
        }
      }
    });
}

#endif // UPDATE_SERVER_H
