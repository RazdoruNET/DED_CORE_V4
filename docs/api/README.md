# API документация DED_CORE_V4

## Обзор API

DED_CORE_V4 предоставляет RESTful HTTP API и WebSocket интерфейс для управления системой зажигания и мониторинга параметров в реальном времени.

## Базовый URL

| Интерфейс | Адрес | Что там |
|---|---|---|
| HTTP | `http://192.168.4.1/` | вшитый веб-интерфейс (из SPIFFS), страница `/update`, API устройства |
| WebSocket | `ws://192.168.4.1:81/` | телеметрия и команды (см. ниже) |
| HTTPS | `https://core.dedbox.pro/` | тот же фронт по TLS; имя резолвит AP-DNS блока, сертификат в SPIFFS |
| WSS | `wss://core.dedbox.pro/ws` | телеметрия для HTTPS-приложения |
| HTTPS | `/api/status`, `/api/tls`, `/api/provision`, `/api/migration`, `/api/migrate` | служебные эндпоинты блока |

Порт 443 нужен потому, что браузер запрещает `ws://` и `http://` со страницы HTTPS:
приложение открывается по HTTPS и общается с блоком по WSS.

## Аутентификация

Эндпоинты настройки (`/max`, `/servo`, `/card`, …) аутентификации не требуют — доступ
ограничен самой точкой доступа блока (WPA2). Служебные эндпоинты строже:

- `/update` и `POST /api/update` — только из сети блока (`192.168.4.x`) либо с заголовком
  `X-Device-Key`, совпадающим с ключом устройства;
- `/api/status`, `/api/tls`, `/api/tls/refresh` — из сети блока или по `X-Device-Key`;
- `/api/provision` — приём сертификата и ключа при провижининге;
- `/api/migration`, `/api/migrate` — переход на новую разметку: команду отдаёт сервис,
  адрес образа берётся из его ответа.

## Форматы данных

### HTTP запросы
- **Метод**: GET/POST
- **Параметры**: query string
- **Ответ**: plain text или JSON

### WebSocket сообщения
- **Формат**: JSON
- **Кодировка**: UTF-8
- **Направление**: двунаправленный обмен

## HTTP эндпоинты

### Системные эндпоинты

#### Получение версии прошивки
```http
GET /core/version
```
**Ответ**: `4.0.0`

#### Перезагрузка системы
```http
GET /reinit
```
**Ответ**: `FIRMWARE_VERSION`

### Управление картами зажигания

#### Выбор активной карты
```http
GET /card?id={1|2|3}
```
**Параметры**:
- `id` - номер карты (1-3)

**Ответ**: `OK`

#### Обновление точки карты
```http
GET /update_card?id={0-21}&rpm={value}&angle={value}&angle_adv={value}&quick_adv={value}&servo={value}
```
**Параметры**:
- `id` - индекс точки (0-21)
- `rpm` - обороты
- `angle` - угол зажигания
- `angle_adv` - коррекция по дросселю
- `quick_adv` - настройка быстрого переключения
- `servo` - значение сервопривода

**Ответ**: `OK`

### Настройка отсечек

#### Настройка максимальных оборотов
```http
GET /max?rpm={value}&angle={value}
```
**Параметры**:
- `rpm` - максимальные обороты
- `angle` - коррекция угла при отсечке

**Ответ**: `OK`

### Управление углом зажигания

#### Включение коррекции по дросселю
```http
GET /angle/advance?status={0|1}
```
**Параметры**:
- `status` - 0 (выкл) или 1 (вкл)

**Ответ**: `OK`

#### Настройка начального угла
```http
GET /angle?init={value}
```
**Параметры**:
- `init` - начальный угол в градусах

**Ответ**: `OK`

### Тип карты зажигания

#### Установка типа карты
```http
GET /type?set={0|1|2}&new_chart_type={1-6}
```
**Параметры**:
- `set` - тип карты (0=7 точек, 1=12 точек, 2=22 точки)
- `new_chart_type` - предустановленный тип двигателя

**Ответ**: `OK`

### Управление модулятором

#### Настройка времени искры
```http
GET /modulator?time={value}
```
**Параметры**:
- `time` - время искры в микросекундах

**Ответ**: `OK`

#### Настройка фильтра
```http
GET /modulator?filter={value}
```
**Параметры**:
- `filter` - значение фильтра RPM

**Ответ**: `OK`

#### Тип выхода
```http
GET /modulator?high={0|1}
```
**Параметры**:
- `high` - 0 (LOW) или 1 (HIGH) выход

**Ответ**: `OK`

### Управление датчиками

#### Инверсия датчика положения
```http
GET /sensor?invert={0|1}
```
**Параметры**:
- `invert` - 0 (прямой) или 1 (инверсный)

**Ответ**: `OK`

#### Инверсия датчика дросселя
```http
GET /throttle?invert={0|1}
```
**Параметры**:
- `invert` - 0 (прямой) или 1 (инверсный)

**Ответ**: `OK`

### Дополнительные функции

#### Быстрое переключение передач
```http
GET /quickshifter?status={0|1}
```
**Параметры**:
- `status` - 0 (выкл) или 1 (вкл)

**Ответ**: `OK`

#### Переключение карт кнопками
```http
GET /carts/switching?status={0|1}
```
**Параметры**:
- `status` - 0 (выкл) или 1 (вкл)

**Ответ**: `OK`

#### Управление сервоприводом
```http
GET /servo?status={0|1}&servo_type={0|1}&servo_rpm={value}&servo_percent={value}
```
**Параметры**:
- `status` - 0 (выкл) или 1 (вкл)
- `servo_type` - тип сервопривода
- `servo_rpm` - RPM активации
- `servo_percent` - процент открытия

**Ответ**: `OK`

#### Тест сервопривода
```http
GET /servo_test?status={0|1}
```
**Параметры**:
- `status` - 0 (выкл) или 1 (вкл)

**Ответ**: `OK`

### Настройка WiFi

#### Основная сеть
```http
GET /wifi?ssid={name}&appsk={password}
```
**Параметры**:
- `ssid` - имя сети
- `appsk` - пароль

**Ответ**: `OK`

#### Сеть обновлений
```http
GET /wifi/updator?ssid={name}&appsk={password}
```
**Параметры**:
- `ssid` - имя сети обновлений
- `appsk` - пароль

**Ответ**: `OK`

### Системные команды

#### Сохранение конфигурации
```http
GET /flash
```
**Ответ**: `OK`

## WebSocket события

### Отправка данных клиенту

#### Структура сообщения
```json
{
  "event": "message",
  "data": {
    "micros": 123456789,
    "select_cart": 0,
    "select_qs": 0,
    "aa": true,
    "qs": false,
    "cc": true,
    "modulator": 1,
    "max_rpm": 10000,
    "max_rpm_angle_kiker": 4,
    "advance_angle": 35.5,
    "rpm": 3500,
    "termo": 45,
    "throttle": 75.2,
    "init_angle": 43,
    "filter_val": 30,
    "high": 1,
    "init_flash_time": 10,
    "cpu_termo": 65.3,
    "card": 0,
    "wsid": "DED_BOX_V4",
    "wpass": "162534Bnm123q",
    "INVERT_SENSOR": false,
    "INVERT_THROTTLE": false,
    "servo": true,
    "servo_position": 15,
    "servo_type": 1,
    "servo_rpm": 8000,
    "servo_percent": 30,
    "type": 2,
    "motor_running": true,
    "wifi_auto_update_enabled": 1,
    "wifi_update_ssid": "DED_UPDATOR",
    "wifi_update_status": "idle",
    "wifi_last_check_time": 0,
    "wifi_last_attempt_time": 0,
    "wifi_connected": false,
    "ota_update_available": false,
    "ota_last_version": "",
    "angles": [
      [0, 21, 0, 0, 0],
      [500, 23, 0, 0, 0],
      [1000, 24, 0, 0, 0],
      ...
    ]
  }
}
```

### Получение команд от клиента

#### Переключение карты
```json
{
  "event": "cart",
  "data": {
    "cart": 2
  }
}
```

#### Пинг-понг
```json
{
  "event": "ping"
}
```

#### Ошибка сервопривода
```json
{
  "event": "servo_error"
}
```

## Примеры использования

### JavaScript - выбор карты
```javascript
fetch('http://192.168.4.1/card?id=2')
  .then(response => response.text())
  .then(data => console.log(data));
```

### JavaScript - обновление точки карты
```javascript
const params = new URLSearchParams({
  id: 5,
  rpm: 4000,
  angle: 32.5,
  angle_adv: 0.2,
  quick_adv: 135,
  servo: 10
});

fetch(`http://192.168.4.1/update_card?${params}`)
  .then(response => response.text())
  .then(data => console.log(data));
```

### WebSocket - подключение
```javascript
const ws = new WebSocket('ws://192.168.4.1:81/');

ws.onmessage = function(event) {
  const data = JSON.parse(event.data);
  console.log('RPM:', data.data.rpm);
  console.log('Угол:', data.data.advance_angle);
};

// Переключение карты
ws.send(JSON.stringify({
  event: 'cart',
  data: { cart: 3 }
}));
```

### Python - запрос версии
```python
import requests

response = requests.get('http://192.168.4.1/core/version')
print(f'Версия прошивки: {response.text}')
```

### Python - мониторинг через WebSocket
```python
import websocket
import json

def on_message(ws, message):
    data = json.loads(message)
    print(f"RPM: {data['data']['rpm']}")
    print(f"Угол зажигания: {data['data']['advance_angle']}")

ws = websocket.WebSocketApp("ws://192.168.4.1:81/", on_message=on_message)
ws.run_forever()
```

## Служебные эндпоинты блока

Эти маршруты появились позже остальных и нужны приложению и сервису, а не человеку с браузером.

| Метод | Назначение | Права |
|---|---|---|
| `GET /api/status` | Состояние блока: MAC, код сопряжения, версия, слот, режим | сеть блока или `X-Device-Key` |
| `POST /api/tls` | Приём пары сертификат/ключ (JSON) | `X-Device-Key` |
| `GET /api/tls/refresh` | Запросить новый сертификат у сервиса по HTTPS | сеть блока или ключ |
| `POST /api/provision` | Провижининг: ключ устройства и параметры | по проводу или `X-Device-Key` |
| `GET /api/migration` | Нужен ли переход на новую разметку и параметры образа | — |
| `POST /api/migrate` | Подтверждение перехода (адрес образа берётся из ответа сервиса) | — |
| `GET /update` | Страница загрузки образа в блок (ElegantOTA-подобная) | сеть блока или ключ |
| `POST /api/update` | Приём образа прошивки (`app` / `fs`) | сеть блока или ключ |

Проверка обновления устройством идёт не с блока, а **от блока к сервису**:
`GET /device/check?current=&type=&slot=` и `GET /device/download` с заголовком
`X-Device-Key` на стороне сервиса (`dedbox.pro`), поэтому в таблицах выше они не описаны.

## Обновление прошивки

- `GET /ota/check` — блок сам проверяет релиз на сервисе и отдаёт результат;
- `GET /ota/status` — состояние: режим, подключение к сети обновления, наличие новой версии;
- `POST /ota/update` — запустить загрузку и установку;
- `POST /ota/mode?mode=0|1|2` — режим OTA: 0 AUTO, 1 MANUAL, 2 OFF;
- `POST /ota/password?password=` — пароль локального обновления (ArduinoOTA; в текущей
  сборке этот путь выключен флагом `DED_CORE_NO_ARDUINO_OTA`).

## Ошибки

### HTTP коды ответа
- **200** - Успешное выполнение
- **404** - Эндпоинт не найден
- **500** - Внутренняя ошибка сервера

### WebSocket ошибки
- **Отключение** - потеря соединения
- **Неверный JSON** - ошибка парсинга
- **Событие не найдено** - неизвестный тип события

## Ограничения

### Частота запросов
- **HTTP**: не более 10 запросов в секунду
- **WebSocket**: не более 50 сообщений в секунду

### Размер данных
- **HTTP**: максимальная длина URL 2048 символов
- **WebSocket**: максимальный размер сообщения 5KB

### Диапазоны значений
- **RPM**: 0-12000 об/мин
- **Угол зажигания**: 0-90 градусов
- **Дроссель**: 0-100%
- **Карта**: 1-3

---

API предоставляет полный контроль над системой зажигания и возможность интеграции с внешними системами мониторинга.
