# Взаимодействие компонентов DED_CORE_V4

## Обзор

Данный документ описывает детальное взаимодействие между основными компонентами системы управления зажиганием DED_CORE_V4.

## Основные потоки данных

### 1. Поток управления зажиганием

```
Датчик ВМТ → Расчет RPM → Расчет угла опережения → Выход на катушку
     ↓                ↓                    ↓                    ↓
  sensor_pin    →   handleMoto()   →   calculateIgnition() →   out_pin
```

**Компоненты:**
- **Sensors.h**: чтение данных с датчика ВМТ
- **MOTO.h**: основная логика управления зажиганием
- **Ignition.h**: управление искровой свечой
- **Variables.h**: хранение состояния системы

### 2. Поток конфигурации

```
Web интерфейс → HTTP сервер → Обработчик → NVS хранилище → Глобальные переменные
      ↓              ↓            ↓              ↓              ↓
  Браузер   →  WEB_SERVER  →  HANDLERS    →  PREFERENCES  →   PROPS
```

**Компоненты:**
- **WEB_SERVER.h**: HTTP сервер для веб-интерфейса
- **HANDLERS.h**: обработчики HTTP запросов
- **PREFERENCES_MANAGER.h**: работа с NVS хранилищем
- **PROPS.h/PROPS_DEF.h**: глобальные переменные конфигурации

### 3. Поток выбора профилей

```
Кнопки → Обработчик → Переключение профилей → Применение настроек
   ↓        ↓              ↓                ↓
Пины → CartSelection → selectCart() → applyCartProfile()
```

**Компоненты:**
- **CartSelection.h**: обработка кнопок выбора
- **MOTO_CARTS.h**: управление профилями карт
- **Angles_*.h**: профили углов опережения

## Детальное взаимодействие

### Система зажигания

#### Основной цикл (MOTO.h)
```cpp
void handleMoto() {
  // 1. Чтение датчиков
  currentRPM = readSensor();
  currentThrottle = readThrottle();
  
  // 2. Расчет угла опережения
  baseAngle = anglesArr[active_cart][rpmIndex];
  adjustedAngle = baseAngle + angles_adjustmentArr[active_cart][rpmIndex];
  
  // 3. Управление искрой
  ignitionTiming = calculateIgnitionTiming(adjustedAngle, currentRPM);
  setIgnitionTiming(ignitionTiming);
  
  // 4. Обработка быстрого переключения
  if (quickshifterTriggered()) {
    handleQuickShift();
  }
}
```

#### Взаимодействие с переменными
- **Глобальные переменные** (PROPS.h):
  - `RPM`: текущие обороты
  - `Throttle`: положение дросселя
  - `Advance_angle`: текущий угол опережения
  - `active_cart`: активный профиль карты

- **Массивы данных**:
  - `anglesArr[3][22]`: базовые углы для 3 профилей
  - `angles_adjustmentArr[22][3]`: корректировки углов
  - `quick_adjustmentArr[22][3]`: быстрые корректировки

### Сетевая подсистема

#### WebSocket сервер (WEB_SOCKET_SERVER.h)
```cpp
void onEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_TEXT:
      // Обработка команд от клиента
      if (strcmp(payload, "get_data") == 0) {
        String json = getDataJson();
        webSocket.sendTXT(num, json);
      }
      break;
  }
}

String getDataJson() {
  // Формирование JSON с текущими параметрами
  StaticJsonBuffer<2048> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  
  root["rpm"] = RPM;
  root["throttle"] = Throttle;
  root["advance_angle"] = Advance_angle;
  root["active_cart"] = active_cart;
  root["termo"] = Termo;
  
  String output;
  root.printTo(output);
  return output;
}
```

#### HTTP сервер (WEB_SERVER.h)
```cpp
void initServerRoutes() {
  // Получение текущих данных
  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = getDataJson();
    request->send(200, "application/json", json);
  });
  
  // Обновление конфигурации
  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("cart")) {
      active_cart = request->getParam("cart")->value().toInt();
      saveConfig();
    }
    request->send(200, "text/plain", "OK");
  });
  
  // Управление профилями
  server.on("/cart_profile", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("profile_data")) {
      updateCartProfile(request->getParam("profile_data")->value());
    }
    request->send(200, "text/plain", "OK");
  });
}
```

### Система задач RTOS

#### Задача управления зажиганием (MotoTask.h)
```cpp
void TaskMotoCode(void *pvParameters) {
  while(1) {
    handleMoto();
    vTaskDelay(1); // Высокоприоритетная задача
  }
}
```

#### Задача сервера (ServerTask.h)
```cpp
void TaskServerCode(void *pvParameters) {
  initWifi();
  initWebSocket();
  initServer();
  
  while(1) {
    dnsServer.processNextRequest();
    handleServer();
    handleWebSocket();
    vTaskDelay(10); // Низкоприоритетная задача
  }
}
```

#### Задача управления (ControlTask.h)
```cpp
void TaskControlCode(void *pvParameters) {
  while(1) {
    handleControllPin();
    handleCartSelection();
    vTaskDelay(50); // Средний приоритет
  }
}
```

#### Аварийная задача (AlarmTask.h)
```cpp
void TaskAlarmCode(void *pvParameters) {
  while(1) {
    handleAlarm();
    handleRPMAlarm();
    vTaskDelay(100); // Низкий приоритет
  }
}
```

## Синхронизация и безопасность

### Механизмы синхронизации
1. **Атомарные операции** для критических переменных
2. **RTOS очереди** для обмена данными между задачами
3. **Мьютексы** для защиты общих ресурсов

### Безопасность данных
1. **Валидация входных параметров** в HTTP обработчиках
2. **Ограничения диапазонов** для всех конфигурационных значений
3. **Защита от переполнения** буферов

### Обработка ошибок
1. **Логирование** критических событий в Serial
2. **Graceful degradation** при ошибках датчиков
3. **Автоматическое восстановление** при сбоях конфигурации

## Производительность и оптимизация

### Эффективное использование памяти
1. **Статическое выделение** буферов при компиляции
2. **Переиспользование буферов** для временных данных
3. **Минимизация динамической памяти**

### Оптимизация вычислений
1. **Предвычисленные таблицы** для углов опережения
2. **Линейная интерполяция** между точками
3. **Кэширование** часто используемых значений

### Сетевая оптимизация
1. **Асинхронная обработка** HTTP запросов
2. **Бинарные WebSocket сообщения** для эффективности
3. **Сжатие данных** при передаче

## Расширяемость архитектуры

### Добавление новых профилей карт
1. **Создание файла** `Angles_NewModel.h` с массивом углов
2. **Добавление include** в `MOTO_CARTS.h`
3. **Обновление веб-интерфейса** для выбора нового профиля

### Добавление новых датчиков
1. **Создание обработчика** в `Sensors.h`
2. **Добавление переменных** в `Variables.h`
3. **Интеграция в основной цикл** `handleMoto()`

### Расширение сетевых функций
1. **Добавление новых endpoints** в `WEB_SERVER.h`
2. **Расширение WebSocket протокола** новыми сообщениями
3. **Интеграция с внешними API** через HTTP клиент

Эта архитектура обеспечивает гибкую, модульную и расширяемую систему управления зажиганием с четким разделением ответственности между компонентами.
