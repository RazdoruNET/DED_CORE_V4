# Техническая документация DED_CORE_V4

## Архитектура системы

DED_CORE_V4 построен на многозадачной архитектуре ESP32 с разделением на независимые модули:

### Ядро системы
- **Модуль зажигания** - расчет углов опережения
- **Обработка датчиков** - RPM, дроссель, положение коленвала
- **Управление выходом** - таймирование искрового разряда
- **Конфигурация** - хранение и загрузка параметров

### Сетевая подсистема
- **HTTP сервер** - REST API для конфигурации
- **WebSocket сервер** - real-time передача данных
- **WiFi точка доступа** - автономная работа
- **OTA обновления** - беспроводная прошивка

### Пользовательский интерфейс
- **Angular приложение** - современный веб-интерфейс
- **Графический редактор** - визуальная настройка карт
- **Мониторинг** - отображение параметров в реальном времени

## Функциональные возможности

### Управление зажиганием
- **Интерполяция углов** - плавное изменение между точками
- **Коррекция по дросселю** - адаптивное опережение
- **Быстрое переключение** - смена карт на лету
- **Сервоуправление** - дополнительные системы

### Система безопасности
- **4 уровня отсечки** - многоуровневая защита
- **Мониторинг температуры** - перегрев двигателя
- **Диагностика** - обнаружение неисправностей
- **Режим восстановления** - сброс при сбоях

### Конфигурация
- **3 независимые карты** - для разных условий
- **22 точки настройки** - максимальная точность
- **Сохранение в NVS** - энергонезависимая память
- **Резервное копирование** - защита от потерь

## Алгоритмы работы

### Расчет RPM
```cpp
// Измерение времени между импульсами датчика
timePerDegree = micros() - oldRpmMicro;
// Расчет оборотов
RPM = fix_micros / timePerDegree;
// Фильтрация сигнала
if (actual > filter || RPM < 200) {
    // Применение нового значения
}
```

### Расчет угла зажигания
```cpp
// Интерполяция между точками
Advance_angle = map(RPM, rpmsArr[active_cart][i], 
                  rpmsArr[active_cart][i + 1], 
                  anglesArr[active_cart][i], 
                  anglesArr[active_cart][i + 1]);

// Коррекция по дросселю
if (ANGLE_ADVANCE == true) {
    Advance_angle += ((angles_adjustmentArr[active_cart][i] / 100) * Throttle);
}
```

### Управление искрой
```cpp
// Расчет времени искры
int diffAngle = Init_angle - Advance_angle;
int diffMicros = diffAngle * (timePerDegree / 360);
nextFlashMicro = oldRpmMicro + diffMicros;

// Генерация импульса
if (nextFlashMicro <= micros()) {
    digitalWrite(out_pin, HIGH);
    delayMicroseconds(Init_flash_time);
    digitalWrite(out_pin, LOW);
}
```

## Типы данных

### Конфигурационные массивы
```cpp
// RPM точки для каждой карты
float rpmsArr[3][22] = {
    {0, 500, 1000, 1500, ...}, // Карта 1
    {0, 500, 1000, 1500, ...}, // Карта 2
    {0, 500, 1000, 1500, ...}  // Карта 3
};

// Углы опережения
float anglesArr[3][22] = {
    {21, 23, 24, 26, ...}, // Карта 1
    {21, 23, 24, 26, ...}, // Карта 2
    {21, 23, 24, 26, ...}  // Карта 3
};
```

### Системные переменные
```cpp
int RPM = 0;                    // Текущие обороты
float Advance_angle = 0;          // Угол опережения
float Throttle = 0;             // Положение дросселя
int active_cart = 0;            // Активная карта
int max_rpm = 10000;           // Отсечка оборотов
int max_rpm_angle_kiker = 4;     // Коррекция отсечки
```

## Многозадачность

### Задачи FreeRTOS
```cpp
// Задача обработки сервера (Core 0)
xTaskCreatePinnedToCore(TaskServerCode, "TaskServerHandler", 
                      20000, NULL, 2, &TaskServerHandler, 0);

// Задача обработки кнопок (Core 0)
xTaskCreatePinnedToCore(TaskControlCode, "TaskControlHandler", 
                      1024, NULL, 1, &TaskControlHandler, 0);

// Задача сигнализации (Core 0)
xTaskCreatePinnedToCore(TaskAlarmCode, "TaskAlarmHandler", 
                      2000, NULL, 1, &TaskAlarmHandler, 0);

// Основной цикл (Core 1)
void loop() {
    handleMoto();
}
```

## Обработка прерываний

### Датчик положения коленвала
```cpp
void handlePin() {
    datchikModulator = digitalRead(sensor_pin);
    
    if (datchikModulator != Modulator) {
        int currentMicros = micros();
        timePerDegree = currentMicros - oldRpmMicro;
        
        if (RPM < 200 || timePerDegree > filter) {
            RPM = fix_micros / timePerDegree;
            oldRpmMicro = currentMicros;
            Modulator = datchikModulator;
        }
    }
}
```

## Система отсечек

### 4 уровня защиты
1. **Системная отсечка** - `max_rpm`
2. **Отсечка Карты 1** - `rpmsArr[0][count-1]`
3. **Отсечка Карты 2** - `rpmsArr[1][count-1]`
4. **Отсечка Карты 3** - `rpmsArr[2][count-1]`

### Логика отсечки
```cpp
if ((RPM > rpmsArr[active_cart][count-1]) || (RPM >= max_rpm)) {
    Advance_angle = Advance_angle - max_rpm_angle_kiker;
}
```

## Хранение данных

### NVS Preferences
```cpp
// Загрузка конфигурации
preferences.begin("ded_box", false);
active_cart = preferences.getInt("active_cart", 0);
max_rpm = preferences.getInt("max_rpm", 10000);
Init_angle = preferences.getInt("Init_angle", 43);

// Сохранение конфигурации
preferences.putInt("active_cart", active_cart);
preferences.putInt("max_rpm", max_rpm);
preferences.putInt("Init_angle", Init_angle);
```

## Обработка ошибок

### Диагностика системы
- **Проверка инициализации** - NVS, SPIFFS, WiFi
- **Мониторинг датчиков** - обрывы, короткие замыкания
- **Контроль производительности** - загрузка CPU, память
- **Логирование ошибок** - Serial вывод

### Режим восстановления
```cpp
void clearPref() {
    Serial.println("clearPref");
    nvs_flash_erase();   
    nvs_flash_init();    
    Serial.println("clearPref completed");
}
```

## Оптимизация производительности

### Эффективные алгоритмы
- **Интерполяция** - вместо поиска в таблице
- **Фильтрация** - устранение дребезга
- **Кэширование** - частых операций
- **Пакетная передача** - WebSocket данных

### Управление памятью
- **Статическое выделение** - избежание фрагментации
- **Оптимизация строк** - минимизация использования RAM
- **Очистка буферов** - своевременное освобождение

## Безопасность системы

### Защита от сбоев
- **Watchdog timer** - перезагрузка при зависании
- **Корректный выход** - из критических секций
- **Проверка границ** - массивов и указателей
- **Резервные копии** - конфигурации

### Безопасность данных
- **Проверка диапазонов** - входных параметров
- **Валидация JSON** - веб-запросов
- **Шифрование** - критических данных
- **Цифровые подписи** - прошивок

---

Техническая документация предоставляет полное понимание внутренней работы системы для разработчиков и инженеров.
