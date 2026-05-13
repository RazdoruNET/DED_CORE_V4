# 06. Конфигурационные параметры

Этот раздел документирует все конфигурационные параметры ядра системы зажигания DED_CORE_V4.

## 6.1 Основные параметры

| Параметр | Тип | Диапазон | Описание |
|----------|-----|----------|-----------|
| `Init_angle` | int | 0-60 | Базовый угол зажигания в градусах до ВМТ |
| `Init_flash_time` | int | 100-5000 | Длительность импульса катушки в микросекундах |
| `max_rpm` | int | 5000-20000 | Порог максимальных оборотов двигателя |
| `max_rpm_angle_kiker` | int | 0-20 | Уменьшение угла на максимальных оборотах (градусы) |
| `filter` | int | 5-50 | Порог фильтрации шума RPM (процент изменения) |

**Пример конфигурации:**
```cpp
#define Init_angle 35           // Искра при 35° до ВМТ
#define Init_flash_time 2000    // 2мс импульс катушки
#define max_rpm 14000           // Лимит максимальных RPM
#define max_rpm_angle_kiker 10  // Уменьшить на 10° на максимальных RPM
#define filter 20               // Разрешить 20% изменение RPM
```

---

## 6.2 Функциональные флаги

| Флаг | Тип | По умолчанию | Описание |
|------|-----|--------------|----------|
| `ANGLE_ADVANCE` | bool | false | Включить корректировку угла по дросселю |
| `QUICKSHIFTER` | bool | false | Включить функциональность QuickShifter |
| `HIGH_OUTPUT` | bool | true | Выход катушки активный HIGH (false = LOW) |
| `INVERT_SENSOR` | bool | false | Инвертировать сигнал датчика ВМТ |
| `INVERT_THROTTLE` | bool | false | Инвертировать чтение датчика дросселя |
| `SERVO` | bool | false | Включить управление сервоприводом |

**Пример конфигурации:**
```cpp
#define ANGLE_ADVANCE true       // Использовать корректировку по дросселю
#define QUICKSHIFTER true        // Включить QuickShifter
#define HIGH_OUTPUT true         // Катушка активная HIGH
#define INVERT_SENSOR false      // Нормальная полярность датчика
#define INVERT_THROTTLE true     // Инвертированный датчик дросселя
#define SERVO true               // Включить сервопривод
```

---

## 6.3 Параметры сервопривода

| Параметр | Тип | Диапазон | Описание |
|----------|-----|----------|-----------|
| `Servo_type` | int | 0-3 | 0=карта, 1=RPM вкл/выкл, 2=ПИД, 3=кривая по температуре |
| `Servo_rpm` | int | 1000-15000 | Порог активации RPM (тип 1) |
| `Servo_percent` | int | 0-100 | Процент открытия сервопривода (тип 1) |
| `Servo_angles[2]` | int[2] | 0-180 | Мин/макс углы для карты (тип 0) |

**Примеры конфигурации:**

**Тип 0 (по карте):**
```cpp
#define Servo_type 0
#define Servo_angles {0, 90}  // 0° на холостых, 90° на максимальных RPM
```

**Тип 1 (RPM вкл/выкл):**
```cpp
#define Servo_type 1
#define Servo_rpm 5000         // Открыть при 5000 RPM
#define Servo_percent 50      // Открыть на 50%
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
#define throttle_pin A0         // Дроссель на ADC0
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
