# Структура кода DED_CORE_V4

## Обзор файловой системы

### Основные директории

```
src/
├── config/                 # Конфигурация и переменные
├── core/                   # Ядро системы
│   ├── handlers/           # Обработчики событий
│   ├── ignition/           # Система зажигания
│   │   └── carts/        # Профили карт
│   ├── init/               # Инициализация системы
│   ├── preferences/        # Управление настройками
│   ├── recovery/          # Режим восстановления
│   └── tasks/             # Задачи RTOS
├── functions/              # Вспомогательные функции
├── servers/               # Сетевые серверы
└── DED_CORE_V4.ino       # Основной файл
```

## Детальное описание модулей

### 1. Конфигурация (`config/`)

#### PROPS.h / PROPS_DEF.h
```cpp
// PROPS.h - Объявления глобальных переменных
extern boolean INIT_SYSTEM;
extern boolean SOUND;
extern int active_cart;
extern float Throttle;
extern int RPM;
extern float Advance_angle;
// ... другие переменные

// PROPS_DEF.h - Определения переменных
boolean INIT_SYSTEM = true;
boolean SOUND = false;
int active_cart = 0;
float Throttle = 0.0;
int RPM = 0;
float Advance_angle = 0.0;
// ... другие определения
```

#### PINS_DEF.h
```cpp
// Определения пинов ESP32
const int sensor_pin = 16;        // Датчик ВМТ
const int quickshifter_pin = 17;   // Быстрое переключение
const int out_pin = 19;           // Выход на катушку
const int select_cart_up_pin = 32;   // Выбор карты вверх
const int select_cart_down_pin = 33; // Выбор карты вниз
const int throttle_pin = 35;       // Датчик дросселя
```

### 2. Инициализация системы (`core/init/`)

#### SYSTEM_INIT.h
```cpp
void initSystemHardware() {
  Serial.begin(115200);
  pinMode(select_cart_down_pin, INPUT);   
  gpio_set_pull_mode(GPIO_NUM_33, GPIO_PULLUP_ONLY);
}

void initSystemSound() {
  if (SOUND == true) {
    // Инициализация звука
  }
}

void initSystemTasks() {
  xTaskCreatePinnedToCore(TaskServerCode,  "TaskServerHandler",  20000, NULL, 2, &TaskServerHandler, 0);
  xTaskCreatePinnedToCore(TaskControlCode, "TaskControlHandler", 1024, NULL, 1, &TaskControlHandler, 0);
  xTaskCreatePinnedToCore(TaskAlarmCode,   "TaskAlarmHandler",   2000, NULL, 1, &TaskAlarmHandler,   0);
}
```

### 3. Система зажигания (`core/ignition/`)

#### MOTO.h
```cpp
void handleMoto() {
  // Основная логика управления зажиганием
  // Расчет угла опережения
  // Управление выходом на катушку
  // Обработка датчиков
}

void initMoto() {
  // Инициализация подсистемы зажигания
  // Настройка таймеров
  // Конфигурация прерываний
}
```

#### Ignition.h
```cpp
void handleIgnition() {
  // Управление искровой свечой
  // Синхронизация с ВМТ
  // Управление временем опережения
}
```

#### Variables.h
```cpp
// Переменные состояния системы
int Modulator = 0;
float Throttle = 0.0;
int RPM = 0;
int Termo = 0;
float Advance_angle = 0.0;
int Init_angle = 43;
int filter = 30;
// ... другие переменные
```

### 4. Профили карт (`core/ignition/carts/`)

#### MOTO_CARTS.h
```cpp
// Общий заголовок для всех профилей карт
#include "Angles_YX125.h"
#include "Angles_YX140.h"
#include "Angles_SmartCom.h"
// ... другие профили
```

#### Angles_*.h
```cpp
// Углы опережения для конкретной модели
float anglesArr[3][22] = {
  {21, 23, 24, 26, 29, 29, 31, 33, 35, 37, 39, 40, 41, 42, 43, 43, 42, 41, 40, 39, 28, 28},
  // ... другие строки
};
```

### 5. Обработчики событий (`core/handlers/`)

#### HANDLERS.h
```cpp
void handleServer() {
  // Обработка HTTP запросов
  // Управление конфигурацией
  // Отправка данных клиенту
}

void handleAlarm() {
  // Обработка аварийных ситуаций
  // Звуковая сигнализация
  // Логирование событий
}
```

#### CartSelection.h
```cpp
void handleCartSelection() {
  // Обработка кнопок выбора карт
  // Переключение профилей
  // Сохранение выбора
}
```

### 6. Задачи RTOS (`core/tasks/`)

Задачи объявлены в `TASK_DECL.h` (через `TASKS.h`), тела — в отдельных файлах, которые
подключает `.ino` после обработчиков. Создаются в `core/init/SYSTEM_INIT.h`:

| Задача | Файл | Стек | Приоритет |
|---|---|---|---|
| `TaskServerCode` | `ServerTask.h` | 20000 | 2 |
| `TaskControlCode` | `ControlTask.h` | 1024 | 1 |
| `TaskOtaCode` | `OtaTask.h` | 16384 | 1 |
| `TaskAlarmCode` | `AlarmTask.h` | 2000 | 1 (только когда `SOUND`) |
| `TaskMotoCode` | `MotoTask.h` — **не используется**: `handleMoto()` вызывается прямо из `loop()` | — | — |

```cpp
// ServerTask.h — реальное содержимое
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
```

`TaskOtaCode` появилась ради TLS-рукопожатия с сервисом: 16 КБ стека под него не
помещались в задачу сервера.

### 7. Сетевые серверы (`servers/`)

#### WIFI_SERVER.h
```cpp
void initWifi() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(APSSID1, APPSK1);
}

void initRecoveryWifi() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(APSSID1, APPSK1);
}
```

#### WEB_SERVER_FUNCS.h и DEVICE_API_ROUTES.h

`initServer()` и базовые маршруты — в `WEB_SERVER_FUNCS.h`, API устройства
(`/max`, `/servo`, `/update_card`, `/wifi`, …) — в `DEVICE_API_ROUTES.h`,
функция `registerDeviceApiRoutes()`.

```cpp
// WEB_SERVER_FUNCS.h
void initServer() {
  SPIFFS.begin(true);
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

  initServerRoutes();
  registerDeviceApiRoutes(&server);
  registerUpdateRoutes(&server);      // /update, /api/update
  registerMigrationRoutes(&server);   // /api/migration, /api/migrate
  registerTlsHttpRoutes(&server);     // /api/status, /api/tls, /api/provision

  server.begin();
}
```

Маршруты статики проверяют наличие файла: если запрошенного файла в SPIFFS нет,
запрос уходит следующему обработчику — на этом и держатся `/update` и `/api/*`.

#### WEB_SOCKET_FUNCS.h (телеметрия и команды, порт 81)
```cpp
void onEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("WebSocket disconnected");
      break;
    case WStype_CONNECTED:
      Serial.println("WebSocket connected");
      break;
    case WStype_TEXT:
      // Обработка текстовых данных
      break;
  }
}

void initWebSocket() {
  webSocket.begin();
  webSocket.onEvent(onEvent);
}
```

## Поток данных в программе

### Инициализация (setup)
1. **Аппаратная инициализация**
   - Последовательный порт
   - Конфигурация GPIO
   - Настройка таймеров

2. **Проверка режима восстановления**
   - Если активирован → режим AP + веб-сервер
   - Иначе → нормальный режим

3. **Нормальный запуск**
   - Инициализация Мото
   - Загрузка настроек из NVS
   - Создание задач RTOS

### Основной цикл (loop)
- **Обработка Мото**: вызов `handleMoto()`
- **Задачи RTOS**: параллельная работа
  - Серверная задача: WiFi + HTTP + WebSocket
  - Управляющая задача: кнопки + датчики
  - Аварийная задача: мониторинг + сигнализация

## Взаимодействие между компонентами

### Глобальные переменные
- **Централизованное состояние**: все компоненты используют общие переменные
- **Extern объявления**: предотвращение множественных определений
- **Разделение интерфейсов**: чистые зависимости между модулями

### Событийная архитектура
- **Прерывания**: датчик ВМТ → обработчик зажигания
- **RTOS задачи**: независимые потоки выполнения
- **Сетевые события**: HTTP запросы + WebSocket сообщения

### Конфигурационная система
- **NVS хранилище**: постоянное сохранение настроек
- **Web интерфейс**: удобная настройка через браузер
- **Профили карт**: быстрая смена параметров

## Паттерны проектирования

### 1. Модульность
- **Единая ответственность**: каждый модуль выполняет одну функцию
- **Слабая связанность**: минимальные зависимости
- **Высокая связанность**: связность внутри модуля

### 2. Абстракция
- **Аппаратная абстракция**: унификация работы с GPIO
- **Интерфейсы**: стандартизированные API между модулями
- **Конфигурация**: параметризуемое поведение

### 3. Обработка ошибок
- **Graceful degradation**: плавное снижение функциональности
- **Логирование**: отладочная информация
- **Восстановление**: автоматический возврат к рабочему состоянию

## Оптимизации производительности

### 1. Память
- **Эффективное использование**: минимальное выделение динамической памяти
- **Статические буферы**: предвыделенные массивы данных
- **Оптимизированные типы**: выбор подходящих типов данных

### 2. Время выполнения
- **RTOS**: параллельная обработка задач
- **Прерывания**: быстрая реакция на события
- **Кэширование**: избежание повторных вычислений

### 3. Сетевая производительность
- **Асинхронная обработка**: неблокирующие операции
- **WebSocket**: эффективная доставка данных
- **Минимальная нагрузка**: оптимизированные ответы

## Безопасность

### 1. Защита данных
- **Валидация**: проверка входных параметров
- **Ограничения**: предельные значения для настроек
- **Резервные копии**: восстановление при повреждении

### 2. Сетевая безопасность
- **WPA2**: шифрование WiFi точки доступа
- **Аутентификация**: защита веб-интерфейса
- **Изоляция**: разделение сетевых интерфейсов

Эта структура обеспечивает модульную, расширяемую и надежную архитектуру системы управления зажиганием.
