# 06. Конфигурационные параметры

Этот раздел документирует все конфигурационные параметры ядра системы зажигания DED_CORE_V4.

## 6.1 Основные параметры

| Параметр | Тип | Диапазон | Описание |
|----------|-----|----------|-----------|
| `Init_angle` | int | 0-80 | Базовый угол зажигания в градусах до ВМТ (пресеты ставят 17…80) |
| `Init_flash_time` | int | 100-5000 | Длительность импульса катушки в микросекундах |
| `max_rpm` | int | 5000-20000 | Порог максимальных оборотов двигателя |
| `max_rpm_angle_kiker` | int | 0-20 | Уменьшение угла на максимальных оборотах (градусы) |
| `filter` | int | 5-50 | Порог фильтрации шума RPM (процент изменения) |

**Пример конфигурации:**
```cpp
// Это переменные (PROPS_DEF.h), а не #define: значения приходят из NVS и от API.
int Init_angle = 35;            // искра при 35° до ВМТ
int Init_flash_time = 2000;     // 2 мс импульс катушки (значение в микросекундах)
int max_rpm = 14000;            // лимит максимальных RPM
int max_rpm_angle_kiker = 10;   // уменьшить на 10° на максимальных RPM
int filter = 20;                // порог отбрасывания слишком ранних импульсов, %
```

---

## 6.2 Функциональные флаги

| Флаг | Тип | По умолчанию | Описание |
|------|-----|--------------|----------|
| `ANGLE_ADVANCE` | int | 0 | Включить корректировку угла по дросселю |
| `QUICKSHIFTER` | int | 0 | Включить функциональность QuickShifter |
| `HIGH_OUTPUT` | boolean | false | Выход катушки активный HIGH (false = LOW) |
| `INVERT_SENSOR` | boolean | false | Инвертировать сигнал датчика ВМТ |
| `INVERT_THROTTLE` | boolean | false | Инвертировать чтение датчика дросселя |
| `SERVO` | boolean | false | Включить управление сервоприводом |

**Пример конфигурации:**
```cpp
int ANGLE_ADVANCE = 1;       // корректировка по дросселю (в коде int, не bool)
int QUICKSHIFTER = 1;        // включить QuickShifter
boolean HIGH_OUTPUT = true;  // катушка активная HIGH
boolean INVERT_SENSOR = false;
boolean INVERT_THROTTLE = true;
boolean SERVO = true;
```

---

## 6.3 Параметры сервопривода

| Параметр | Тип | Диапазон | Описание |
|----------|-----|----------|-----------|
| `Servo_type` | int | 0-1 | 0=позиция по карте `servo_valueArr`, 1=вкл/выкл по `Servo_rpm` |
| `Servo_rpm` | int | 1000-15000 | Порог активации RPM (тип 1) |
| `Servo_percent` | int | 0-100 | Процент открытия сервопривода (тип 1) |

**Примеры конфигурации:**

**Тип 0 (по карте):**
```cpp
int Servo_type = 0;   // позиция на каждую точку карты — в servo_valueArr
```

**Тип 1 (RPM вкл/выкл):**
```cpp
int Servo_type = 1;
int Servo_rpm = 5000;      // открыть при 5000 RPM
int Servo_percent = 50;    // открыть на 50%
```

---

## 6.4 Параметры QuickShifter

| Параметр | Тип | Диапазон | Описание |
|----------|-----|----------|-----------|
| `QS_time` | int | 30-200 | Длительность блокировки искры в миллисекундах |
| `quickshifter_pin` | int | GPIO | Номер пина для датчика QuickShifter |

**Пример конфигурации:**
```cpp
#define QS_time 60              // 60мс блокировка искры
#define quickshifter_pin 15     // GPIO 15
```

---

## 6.5 Конфигурация пинов датчиков

| Параметр | Тип | Диапазон | Описание |
|----------|-----|----------|-----------|
| `rpm_pin` | int | GPIO | Входной пин датчика ВМТ |
| `throttle_pin` | int | GPIO/ADC | ADC пин датчика дросселя |

**Пример конфигурации:**
```cpp
#define rpm_pin 2               // Датчик ВМТ на GPIO 2
#define throttle_pin 35         // Дроссель: GPIO 35 (ADC1_CH7), см. config/PINS_DEF.h
```

---

## Приложение

### Справочная структура файлов

```
src/core/ignition/
├── Ignition.h              # Основное управление зажиганием (handleAngle, handleIgnition)
├── Sensors.h               # Чтение датчиков (handlePin, handleRpm)
├── carts/
│   ├── Angles_7pt.h        # 7-точечные карты зажигания
│   ├── Angles_12pt.h       # 12-точечные карты зажигания
│   ├── Angles_22pt.h       # 22-точечные карты зажигания
│   └── MOTO_CARTS.h        # Интеграция карт
└── RPMs.h                  # Массивы точек RPM

src/core/handlers/
└── QuickShifter.h          # Управление QuickShifter

Config.h                    # Все конфигурационные параметры
```

### Справочник ключевых функций

| Функция | Расположение | Назначение |
|---------|--------------|------------|
| `handleMoto()` | Main | Основной цикл управления |
| `handlePin()` | Sensors.h | Чтение всех датчиков |
| `handleRpm()` | Sensors.h | Расчет RPM с фильтрацией |
| `handleAngle()` | Ignition.h | Расчет угла опережения |
| `handleIgnition()` | Ignition.h | Управление выходом катушки |
| `handleFlash()` | Ignition.h | Запуск искры |
| `handleQuickShifterControllPin()` | QuickShifter.h | Обработка входа QS |
| `isQuickShifterActive()` | QuickShifter.h | Проверка статуса блокировки QS |

---

**[← Назад: Примеры кода](05-code-examples.md)** | **[← Назад к README](../README.md)**
