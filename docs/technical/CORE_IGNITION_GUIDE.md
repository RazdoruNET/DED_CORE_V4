# DED_CORE_V4 Ядро системы зажигания - Техническое руководство

**Этот документ был декомпозирован на отдельные разделы для лучшей навигации и сопровождения.**

Используйте навигацию ниже для доступа к конкретным разделам документации:

## Разделы документации

- **[01. Математические основы](ignition/01-mathematical-foundations.md)** - Расчет RPM, угол опережения зажигания, время искры, положение дросселя
- **[02. Временные диаграммы](ignition/02-timing-diagrams.md)** - Датчик ВМТ, синхронизация зажигания, QuickShifter, работа сервопривода
- **[03. Блок-схемы](ignition/03-flowcharts.md)** - Основной цикл, расчет угла, фильтрация RPM, алгоритмы управления зажиганием
- **[04. Расширяемые места кода](ignition/04-extensible-code-points.md)** - Добавление новых карт, кастомные алгоритмы, новые датчики
- **[05. Примеры кода](ignition/05-code-examples.md)** - Адаптивный QuickShifter, сплайн-интерполяция, фильтр Калмана, корректировка по температуре
- **[06. Конфигурационные параметры](ignition/06-configuration-parameters.md)** - Основные параметры, функциональные флаги, настройки сервопривода и QuickShifter

## Быстрая навигация

- **[← Назад к README технической документации](README.md)**
- **[Начать с математических основ →](ignition/01-mathematical-foundations.md)**

---

**Версия документа:** 1.0  
**Последнее обновление:** 2024  
**Целевая система:** DED_CORE_V4 Ядро системы зажигания  
**Область:** Только ядро системы зажигания (сетевой слой — в docs/api/README.md и docs/technical/ota-guide.md)

### 1.1 Engine RPM Calculation

**Formula:**
```
RPM = 60000000 / timePerDegree
```

**Variable Explanation:**
- `timePerDegree`: Time between TDC (Top Dead Center) sensor pulses in microseconds
- `RPM`: Engine revolutions per minute
- `60000000`: Conversion factor (60 seconds × 1,000,000 microseconds)

**Noise Filtering Algorithm:**

Фильтр сравнивает **периоды между импульсами**, а не обороты: он отбрасывает импульс,
пришедший слишком рано (паразитное срабатывание датчика), и в этом случае оставляет
прежнее значение RPM.

```cpp
// src/core/ignition/Sensors.h, handleRpm()
int timePrePerDegree = oldRpmMicro - preOldRpmMicro;   // предыдущий период
timePerDegree        = __micros - oldRpmMicro;         // текущий период

int one_percent = timePrePerDegree / 100;
int actual      = timePerDegree / one_percent;         // текущий период в % от предыдущего

// RPM обновляется, если это первый импульс, если RPM ещё не набраны,
// или если период не «схлопнулся» ниже порога filter — то есть импульс не лишний.
if (RPM < 200 || actual > filter || preOldRpmMicro == 0) {
    RPM = fix_micros / timePerDegree;
    preOldRpmMicro = oldRpmMicro;
    oldRpmMicro = __micros;
}
```

При типовом `filter = 30` условие `actual > filter` выполнено почти всегда (период
обычно близок к 100 % предыдущего), поэтому фильтр срабатывает только на резком
укорочении периода — то есть ловит лишние импульсы, а не «шумные» скачки оборотов.

**Example Calculations:**

| timePerDegree (µs) | RPM |
|-------------------|-----|
| 20000             | 3000 |
| 10000             | 6000 |
| 6667              | 9000 |
| 5000              | 12000 |

**Engine Stop Detection:**
If no sensor pulse is received within 500ms, the engine is considered stopped:
```cpp
if (micros() - lastRpmMicro > 500000) {
    RPM = 0;
}
```

---

### 1.2 Ignition Advance Angle Calculation

**Base Angle from Map:**
```
Advance_angle = anglesArr[active_cart][rpmIndex]
```

**Linear Interpolation Between Map Points:**
```
Advance_angle = map(RPM, rpmsArr[i], rpmsArr[i+1], 
                    anglesArr[i], anglesArr[i+1])
```

Where `map()` performs linear interpolation:
```
map(x, in_min, in_max, out_min, out_max) = 
    (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min
```

**Throttle Correction (if ANGLE_ADVANCE=true):**
```
Advance_angle = Advance_angle + ((angles_adjustmentArr[i] / 100) * Throttle)
```

- `angles_adjustmentArr[i]`: Adjustment factor from map (percentage)
- `Throttle`: Throttle position (0-100%)

**High RPM Limit:**
```
if (RPM >= max_rpm) {
    Advance_angle = Advance_angle - max_rpm_angle_kiker
}
```

- `max_rpm`: Maximum engine RPM threshold
- `max_rpm_angle_kiker`: Angle reduction at high RPM (degrees)

---

### 1.3 Spark Timing Calculation

**Angle Difference:**
```
diffAngle = Init_angle - Advance_angle
```

- `Init_angle`: Base ignition angle (degrees before TDC)
- `diffAngle`: Degrees to wait before spark

**Time Until Spark:**
```
diffMicros = diffAngle * (timePerDegree / 360)
```

- `diffMicros`: Microseconds to wait before spark
- `timePerDegree / 360`: Microseconds per degree of crank rotation

**Spark Time:**
```
nextFlashMicro = oldRpmMicro + diffMicros
```

- `oldRpmMicro`: Timestamp of last TDC sensor pulse
- `nextFlashMicro`: Absolute timestamp for spark trigger

---

### 1.4 Throttle Position Calculation

**ADC Reading:**
```
datchikThrottle = analogRead(throttle_pin)
```

**Normalization (0-100%):**
```
Throttle = datchikThrottle * 100 / 4095
```

- `4095`: Maximum ADC value (12-bit ADC)

**Inversion (if INVERT_THROTTLE=true):**
```
Throttle = 100 - Throttle
```

**Smooth Filtering:**
The throttle value changes in ±5% steps to prevent jitter:
```cpp
if (Throttle > previousThrottle + 5) {
    Throttle = previousThrottle + 5;
} else if (Throttle < previousThrottle - 5) {
    Throttle = previousThrottle - 5;
}
```

---

## 2. Timing Diagrams

### 2.1 TDC Sensor Operation Diagram

```
Time →
     ┌─────┐     ┌─────┐     ┌─────┐
     │     │     │     │     │     │
─────┘     └─────┘     └─────┘     └─────  Sensor Signal
     ↑     ↑     ↑     ↑     ↑
     t0    t1    t2    t3    t4

t1 - t0 = timePerDegree
RPM = 60000000 / (t1 - t0)

Engine Stop Detection:
If (t_current - t_last) > 500ms → RPM = 0
```

**Key Events:**
- Sensor pulse edges trigger RPM calculation
- Time between pulses measured in microseconds
- 500ms timeout indicates engine stopped

---

### 2.2 Ignition Synchronization Diagram

```
Crank Angle (degrees) →
0° (TDC)    360° (TDC)
    │         │
    │         │
    └─────────┘
     ↑         ↑
     │         │
   Init_angle  │
     │         │
     ↓         │
  Advance_angle│
     │         │
     ↓         │
  Spark Point  │
     │         │
     ↓         │
  Init_flash_time
     │
     ↓
  Coil Pulse Duration

Timeline:
TDC Sensor → [diffMicros] → Spark Trigger → [Init_flash_time] → Coil OFF
```

**Sequence:**
1. TDC sensor pulse detected
2. Wait `diffMicros` microseconds
3. Trigger spark (activate coil)
4. Keep coil active for `Init_flash_time` microseconds
5. Deactivate coil

---

### 2.3 QuickShifter Operation Diagram

```
Time →
Normal Operation:
     ┌─────┐     ┌─────┐     ┌─────┐
─────┘     └─────┘     └─────┘     └─────  Spark Signals
     ↑     ↑     ↑     ↑
    Spark Spark Spark Spark

QuickShifter Activated:
     ┌─────┐     ┌─────┐     ┌─────┐
─────┘     └─────┘     └─────┘     └─────  Spark Signals
     ↑     X     X     X     ↑
    Spark  Blocked by QS
           ↑
           QS_start
           │
           └─── QS_time (ms) ───┘
```

**Operation:**
- QuickShifter pin goes LOW when gear shift detected
- `QS_start` set to current time + `QS_time` milliseconds
- All sparks blocked until `QS_start` time expires
- Typical QS_time: 50-100ms

---

### 2.4 Servo Operation Diagrams

**Type 0: Position by RPM (Map-based)**
```
RPM →
0    2000  4000  6000  8000  10000  12000
│     │     │     │     │     │      │
│     │     │     │     │     │      │
└─────┴─────┴─────┴─────┴─────┴──────┘
Servo Position (degrees) from map
```

**Type 1: RPM-based On/Off**
```
RPM →
0    2000  4000  6000  8000  10000  12000
│     │     │     │     │     │      │
│     │     │     │     │     │      │
└─────┴─────┴─────┴─────┴─────┴──────┘
           ↑
         Servo_rpm
           │
           └──→ Servo_percent (e.g., 50%)
```

**Type 0:** Servo position follows RPM map curve  
**Type 1:** Servo opens to fixed percentage when RPM exceeds threshold

---

## 3. Flowcharts

### 3.1 Main Loop - handleMoto()

```
┌─────────────────────────────────┐
│         handleMoto()            │
└──────────────┬──────────────────┘
               │
               ▼
┌─────────────────────────────────┐
│      handlePin()                │
│  - Read TDC sensor              │
│  - Read throttle ADC            │
│  - Read QuickShifter pin        │
└──────────────┬──────────────────┘
               │
               ▼
┌─────────────────────────────────┐
│      handleRpm()                │
│  - Calculate timePerDegree      │
│  - Calculate RPM                │
│  - Apply noise filter           │
│  - Detect engine stop           │
└──────────────┬──────────────────┘
               │
               ▼
┌─────────────────────────────────┐
│      handleAngle()              │
│  - Get base angle from map      │
│  - Interpolate between points   │
│  - Apply throttle correction    │
│  - Apply high RPM limit         │
│  - Calculate spark timing       │
│  - Control servo (if enabled)   │
└──────────────┬──────────────────┘
               │
               ▼
┌─────────────────────────────────┐
│      handleIgnition()           │
│  - Detect sensor edge           │
│  - Calculate nextFlashMicro     │
│  - Set coil output state        │
└──────────────┬──────────────────┘
               │
               ▼
┌─────────────────────────────────┐
│      handleFlash()              │
│  - Check if spark time reached  │
│  - Activate coil                │
│  - Deactivate after duration    │
│  - Handle QuickShifter block    │
└──────────────┬──────────────────┘
               │
               ▼
         Loop to handleMoto()
```

---

### 3.2 Angle Calculation Algorithm - handleAngle()

```
┌─────────────────────────────────┐
│      handleAngle()              │
└──────────────┬──────────────────┘
               │
               ▼
       ┌───────────────┐
       │ RPM <= minRPM?│
       └───────┬───────┘
               │
      Yes ─────┴───── No
       │              │
       ▼              ▼
  ┌─────────┐  ┌──────────────────┐
  │ Return  │  │ Get cart type    │
  │ (no spark)│  │ (0=7pt, 1=12pt, │
  └─────────┘  │  2=22pt)         │
               └────────┬─────────┘
                        │
                        ▼
               ┌──────────────────┐
               │ Find RPM index   │
               │ in rpmsArr[]     │
               └────────┬─────────┘
                        │
                        ▼
               ┌──────────────────┐
               │ Linear interpolate│
               │ angle between     │
               │ map points        │
               └────────┬─────────┘
                        │
                        ▼
               ┌──────────────────┐
               │ ANGLE_ADVANCE?  │
               └────────┬─────────┘
                        │
               Yes ─────┴───── No
                │              │
                ▼              │
       ┌────────────────┐      │
       │ Apply throttle │      │
       │ correction     │      │
       └────────┬───────┘      │
                │              │
                └──────┬───────┘
                       │
                       ▼
              ┌──────────────────┐
              │ RPM >= max_rpm?  │
              └────────┬─────────┘
                       │
              Yes ─────┴───── No
               │              │
               ▼              │
      ┌────────────────┐      │
      │ Subtract       │      │
      │ max_rpm_kiker  │      │
      └────────┬───────┘      │
               │              │
               └──────┬───────┘
                      │
                      ▼
             ┌──────────────────┐
             │ Calculate       │
             │ diffAngle =     │
             │ Init_angle -    │
             │ Advance_angle   │
             └────────┬─────────┘
                      │
                      ▼
             ┌──────────────────┐
             │ Calculate        │
             │ diffMicros =     │
             │ diffAngle *      │
             │ (timePerDegree/360)│
             └────────┬─────────┘
                      │
                      ▼
             ┌──────────────────┐
             │ SERVO enabled?   │
             └────────┬─────────┘
                      │
             Yes ─────┴───── No
              │              │
              ▼              │
     ┌────────────────┐     │
     │ Control servo  │     │
     │ (type 0 or 1)  │     │
     └────────┬───────┘     │
              │             │
              └─────┬───────┘
                    │
                    ▼
           ┌──────────────────┐
           │ Set             │
           │ nextFlashMicro = │
           │ oldRpmMicro +   │
           │ diffMicros      │
           └──────────────────┘
```

---

### 3.3 RPM Filtering Algorithm

```
┌─────────────────────────────────┐
│      handleRpm()                │
└──────────────┬──────────────────┘
               │
               ▼
┌─────────────────────────────────┐
│ Measure time between pulses    │
│ timePerDegree = current - last │
└──────────────┬──────────────────┘
               │
               ▼
┌─────────────────────────────────┐
│ Calculate raw RPM               │
│ rawRPM = 60000000 / timePerDegree│
└──────────────┬──────────────────┘
               │
               ▼
       ┌───────────────┐
       │ First reading?│
       └───────┬───────┘
               │
      Yes ─────┴───── No
       │              │
       ▼              ▼
  ┌─────────┐  ┌──────────────────┐
  │ RPM =   │  │ Период в % от    │
  │ rawRPM  │  │ предыдущего:     │
  └─────────┘  │ actual = t /     │
               │ (prevPeriod/100) │
               └────────┬─────────┘
                        │
                        ▼
        ┌────────────────────────────┐
        │ RPM < 200 || actual > filter? │
        └───────────────┬────────────┘
                        │
               Yes ─────┴───── No
                │              │
                ▼              ▼
       ┌────────────────┐ ┌──────────────────────┐
       │ RPM = rawRPM   │ │ Импульс слишком ранний│
       │ prev = raw     │ │ → RPM не меняем       │
       └────────────────┘ └──────────────────────┘
```

---

### 3.4 Ignition Control Algorithm - handleIgnition()

```
┌─────────────────────────────────┐
│      handleIgnition()            │
└──────────────┬──────────────────┘
               │
               ▼
┌─────────────────────────────────┐
│ Read TDC sensor pin             │
└──────────────┬──────────────────┘
               │
               ▼
       ┌───────────────┐
       │ Sensor state  │
       │ changed?      │
       └───────┬───────┘
               │
      Yes ─────┴───── No
       │              │
       ▼              │
┌─────────────────┐   │
│ Detect edge     │   │
│ (rising/falling)│   │
└────────┬────────┘   │
         │            │
         ▼            │
┌─────────────────┐   │
│ Record timestamp│   │
│ oldRpmMicro =  │   │
│ micros()       │   │
└────────┬────────┘   │
         │            │
         ▼            │
┌─────────────────┐   │
│ Calculate spark │   │
│ timing from     │   │
│ handleAngle()   │   │
└────────┬────────┘   │
         │            │
         ▼            │
┌─────────────────┐   │
│ Set coil output │   │
│ based on        │   │
│ HIGH_OUTPUT     │   │
└────────┬────────┘   │
         │            │
         └─────┬──────┘
               │
               ▼
         Return to loop
```

---

## 4. Extensible Code Points

### 4.1 Добавление нового набора углов (пресета двигателя)

**Где:** `src/core/ignition/carts/` — по файлу на набор, плюс правка `MOTO_CARTS.h`
и обработчика `/type`.

Реально набор — это массив `float[3][22]`: три карты по 22 точки.

```cpp
// src/core/ignition/carts/Angles_NewModel.h
#ifndef ANGLES_NEWMODEL_H
#define ANGLES_NEWMODEL_H

float anglesArr_newmodel[][22] = {
  { 5, 8, 12, 18, 25, 32, 38, 42, 45, 47, 48, 47, 45, 42, 38, 35, 32, 30, 28, 26, 24, 22 },
  { 5, 8, 12, 18, 25, 32, 38, 42, 45, 47, 48, 47, 45, 42, 38, 35, 32, 30, 28, 26, 24, 22 },
  { 5, 8, 12, 18, 25, 32, 38, 42, 45, 47, 48, 47, 45, 42, 38, 35, 32, 30, 28, 26, 24, 22 }
};

#endif // ANGLES_NEWMODEL_H
```

Подключение и выбор:

```cpp
// 1. src/core/ignition/carts/MOTO_CARTS.h — добавить include
#include "Angles_NewModel.h"

// 2. src/servers/DEVICE_API_ROUTES.h, обработчик /type — добавить номер пресета.
//    Номер общий для углов и оборотов: для углов он берётся из anglesArr_*, для RPM —
//    из rpmsArr_* (в текущем коде: 1..6, где 4 — rpmsArr_4 + Angles_166FMM).
else if (new_chart_type == 7)
{
  for (int i = 0; i < 22; i++)
  {
    anglesArr[0][i] = anglesArr_newmodel[0][i];
    anglesArr[1][i] = anglesArr_newmodel[1][i];
    anglesArr[2][i] = anglesArr_newmodel[2][i];
  }
}
```

Что стоит проверить после добавления:

- таблицу оборотов (`RPMs.h`: `rpmsArr_*`) — она выбирается тем же `new_chart_type`,
  и её значения должны совпадать по смыслу с новыми углами;
- блок, где пресет задаёт `Init_angle` (`switch (new_chart_type)` в том же обработчике);
- сохранение в NVS: обработчик перезаписывает все точки карт в область `ded_box`,
  поэтому пресет применяется сразу и переживает перезагрузку.

---

### 4.2 Custom RPM Filtering Algorithm

**Location:** `src/core/ignition/Sensors.h`, function `handleRpm()`

**Текущий алгоритм (по периодам, не по оборотам):**
```cpp
// src/core/ignition/Sensors.h, handleRpm()
int timePrePerDegree = oldRpmMicro - preOldRpmMicro;   // предыдущий период между импульсами
timePerDegree        = __micros - oldRpmMicro;         // текущий период

int one_percent = timePrePerDegree / 100;
int actual      = timePerDegree / one_percent;         // текущий период в % от предыдущего

if (RPM < 200 || actual > filter || preOldRpmMicro == 0) {
    RPM = fix_micros / timePerDegree;                  // fix_micros = 60000000
    preOldRpmMicro = oldRpmMicro;
    oldRpmMicro = __micros;
    Modulator = datchikModulator;
}
```

**Example 1: Moving Average Filter**
```cpp
#define RPM_WINDOW_SIZE 5

int rpmWindow[RPM_WINDOW_SIZE] = {0};
int rpmWindowIndex = 0;

void handleRpm() {
    int rawRPM = 60000000 / timePerDegree;
    
    // Add to window
    rpmWindow[rpmWindowIndex] = rawRPM;
    rpmWindowIndex = (rpmWindowIndex + 1) % RPM_WINDOW_SIZE;
    
    // Calculate average
    long sum = 0;
    for (int i = 0; i < RPM_WINDOW_SIZE; i++) {
        sum += rpmWindow[i];
    }
    RPM = sum / RPM_WINDOW_SIZE;
}
```

**Example 2: Kalman Filter**
```cpp
float kalman_rpm = 0;
float kalman_p = 1.0;  // Estimate error
float kalman_q = 0.1;  // Process noise
float kalman_r = 0.1;  // Measurement noise

void handleRpm() {
    int rawRPM = 60000000 / timePerDegree;
    
    // Predict
    kalman_p = kalman_p + kalman_q;
    
    // Update
    float kalman_k = kalman_p / (kalman_p + kalman_r);
    kalman_rpm = kalman_rpm + kalman_k * (rawRPM - kalman_rpm);
    kalman_p = (1 - kalman_k) * kalman_p;
    
    RPM = (int)kalman_rpm;
}
```

---

### 4.3 Custom Angle Interpolation Algorithm

**Location:** `src/core/ignition/Ignition.h`, function `handleAngle()`

**Current Algorithm (Linear Interpolation):**
```cpp
Advance_angle = map(RPM, rpmsArr[i], rpmsArr[i+1], 
                    anglesArr[i], anglesArr[i+1]);
```

**Example 1: Cubic Spline Interpolation**
```cpp
// Cubic interpolation between 4 points
float cubicInterpolate(float x, float x0, float x1, float x2, float x3,
                       float y0, float y1, float y2, float y3) {
    float t = (x - x1) / (x2 - x1);
    
    float a0 = y0;
    float a1 = y1;
    float a2 = y2;
    float a3 = y3;
    
    float c0 = a1;
    float c1 = 0.5 * (a2 - a0);
    float c2 = a0 - 2.5 * a1 + 2 * a2 - 0.5 * a3;
    float c3 = 0.5 * (a3 - a0) + 1.5 * (a1 - a2);
    
    return c0 + c1 * t + c2 * t * t + c3 * t * t * t;
}

// Usage in handleAngle()
if (i > 0 && i < mapSize - 2) {
    Advance_angle = cubicInterpolate(RPM, 
        rpmsArr[i-1], rpmsArr[i], rpmsArr[i+1], rpmsArr[i+2],
        anglesArr[i-1], anglesArr[i], anglesArr[i+1], anglesArr[i+2]);
} else {
    // Fall back to linear at edges
    Advance_angle = map(RPM, rpmsArr[i], rpmsArr[i+1], 
                        anglesArr[i], anglesArr[i+1]);
}
```

**Example 2: Exponential Interpolation**
```cpp
float expInterpolate(float x, float x0, float x1, float y0, float y1, float curve) {
    float t = (x - x0) / (x1 - x0);
    t = pow(t, curve);  // curve > 1 = exponential, curve < 1 = logarithmic
    return y0 + t * (y1 - y0);
}

// Usage
Advance_angle = expInterpolate(RPM, rpmsArr[i], rpmsArr[i+1], 
                               anglesArr[i], anglesArr[i+1], 1.5);
```

---

### 4.4 Adding New Sensor

**Location:** `src/core/ignition/Sensors.h`, function `handlePin()`

**Example 1: Temperature Sensor**
```cpp
// Add to Config.h (ADC1 ESP32: свободны, например, GPIO 34 — только вход)
#define TEMP_PIN 34
int Termo = 0;  // Global variable

// In handlePin()
void handlePin() {
    // ... existing sensor reads ...
    
    // Read temperature sensor
    datchikTermo = analogRead(TEMP_PIN);
    
    // Convert to Celsius (example for LM35)
    // LM35: 10mV per degree, 3.3V reference, 12-bit ADC
    Termo = (datchikTermo * 3300.0 / 4095.0) / 10.0;
}
```

**Integration in Angle Calculation:**
```cpp
// In handleAngle()
float tempCorrection = 0;
if (Termo > 80) {
    // Reduce advance angle at high temperature
    tempCorrection = (Termo - 80) * 0.1;  // 0.1 degree per degree over 80°C
}
Advance_angle = Advance_angle - tempCorrection;
```

**Example 2: MAP (Manifold Absolute Pressure) Sensor**
```cpp
// Add to Config.h
#define MAP_PIN 39   // ADC1_CH3, только вход
int MAP = 0;  // Global variable (kPa)

// In handlePin()
void handlePin() {
    // ... existing sensor reads ...
    
    // Read MAP sensor
    datchikMAP = analogRead(MAP_PIN);
    
    // Convert to kPa (example for MPX4250)
    // MPX4250: 0.2V at 20kPa, 4.7V at 250kPa
    float voltage = datchikMAP * 3.3 / 4095.0;
    MAP = (voltage - 0.2) * (250 - 20) / (4.7 - 0.2) + 20;
}
```

**Integration:**
```cpp
// In handleAngle()
float mapCorrection = (MAP - 100) * 0.05;  // Adjust based on pressure
Advance_angle = Advance_angle + mapCorrection;
```

---

### 4.5 Custom QuickShifter Logic

**Location:** `src/core/handlers/QuickShifter.h`

**Current Algorithm (Simple Time-based Block):**
```cpp
void handleQuickShifterControllPin() {
    int quickshifter = digitalRead(quickshifter_pin);
    
    if (quickshifter == LOW) {
        if (quickshifter != quickshifter_proces) {
            QS_start = micros() + (QS_time * 1000);
        }
    }
    
    quickshifter_proces = quickshifter;
}

bool isQuickShifterActive() {
    return micros() < QS_start;
}
```

**Example 1: Adaptive Time by RPM**
```cpp
void handleQuickShifterControllPin() {
    int quickshifter = digitalRead(quickshifter_pin);
    
    if (quickshifter == LOW) {
        if (quickshifter != quickshifter_proces) {
            // Adaptive: higher RPM = shorter cut time
            int adaptive_qs_time = QS_time * (10000 / max(RPM, 1000));
            QS_start = micros() + (adaptive_qs_time * 1000);
        }
    }
    
    quickshifter_proces = quickshifter;
}
```

**Example 2: Multi-spark Block**
```cpp
int QS_sparks_to_block = 3;  // Block next 3 sparks
int QS_sparks_blocked = 0;

void handleQuickShifterControllPin() {
    int quickshifter = digitalRead(quickshifter_pin);
    
    if (quickshifter == LOW) {
        if (quickshifter != quickshifter_proces) {
            QS_sparks_blocked = QS_sparks_to_block;
        }
    }
    
    quickshifter_proces = quickshifter;
}

bool isQuickShifterActive() {
    if (QS_sparks_blocked > 0) {
        QS_sparks_blocked--;
        return true;
    }
    return false;
}
```

---

### 4.6 Extending Servo Control

**Location:** `src/core/ignition/Ignition.h`, function `handleAngle()`

**Current Algorithm (Two Types):**
```cpp
// Позиции берутся не из пары min/max, а из карты servo_valueArr (22 точки на карту):
if (Servo_type == 0) {
    Servo_position = servo_valueArr[active_cart][i];   // интерполяция по точке карты
} else if (Servo_type == 1) {
    // вкл/выкл по порогу Servo_rpm, без интерполяции
    if (RPM >= Servo_rpm) {
        Servo_position = Servo_percent;
    } else {
        Servo_position = 0;
    }
    }
}
```

**Example 1: PID Controller**
```cpp
// PID variables
float pid_target = 50.0;  // Target servo position (%)
float pid_kp = 0.5;
float pid_ki = 0.01;
float pid_kd = 0.1;
float pid_integral = 0;
float pid_last_error = 0;

void handleServoPID() {
    float error = pid_target - Servo_pos;
    pid_integral += error;
    float derivative = error - pid_last_error;
    
    float output = pid_kp * error + pid_ki * pid_integral + pid_kd * derivative;
    
    Servo_pos = constrain(Servo_pos + output, 0, 100);
    pid_last_error = error;
}

// In handleAngle()
if (SERVO && Servo_type == 2) {
    handleServoPID();
}
```

**Example 2: Temperature-based Curve**
```cpp
void handleServoTempCurve() {
    // Open servo more at higher temperatures
    float temp_factor = constrain((Termo - 20) / 80.0, 0, 1);  // 0 at 20°C, 1 at 100°C
    
    if (RPM >= Servo_rpm) {
        Servo_pos = Servo_percent + (temp_factor * 20);  // Add up to 20% based on temp
    } else {
        Servo_pos = temp_factor * 10;  // Base opening based on temp
    }
    
    Servo_pos = constrain(Servo_pos, 0, 100);
}

// In handleAngle()
if (SERVO && Servo_type == 3) {
    handleServoTempCurve();
}
```

---

## 5. Code Examples with Motor Context

### 5.1 Adaptive QuickShifter

**Context:** Adjusts spark cut duration based on engine RPM for smoother shifts at different speeds.

```cpp
// In QuickShifter.h
void handleQuickShifterControllPin() {
    int quickshifter = digitalRead(quickshifter_pin);
  
    if (quickshifter == LOW) {
        if (quickshifter != quickshifter_proces) {
            // Adaptive time: higher RPM = shorter cut time
            // Base QS_time at 10000 RPM, scale inversely
            int adaptive_qs_time = QS_time * (10000 / max(RPM, 1000));
            
            // Constrain to reasonable limits (30ms to 150ms)
            adaptive_qs_time = constrain(adaptive_qs_time, 30, 150);
            
            QS_start = micros() + (adaptive_qs_time * 1000);
        }
    }
  
    quickshifter_proces = quickshifter;
}
```

**Usage:** Replace existing `handleQuickShifterControllPin()` with this version. The adaptive algorithm provides:
- Shorter cut times at high RPM (faster shifts)
- Longer cut times at low RPM (more complete gear engagement)
- Automatic adjustment without manual tuning per gear

---

### 5.2 Spline Interpolation for Angle

**Context:** Smoother angle transitions between map points, especially useful for high-resolution maps.

```cpp
// In Ignition.h, add helper function
float splineInterpolate(float x, float x0, float x1, float y0, float y1) {
    float t = (x - x0) / (x1 - x0);
    
    // Hermite spline for smooth transitions
    float t2 = t * t;
    float t3 = t2 * t;
    
    float h1 = 2*t3 - 3*t2 + 1;
    float h2 = -2*t3 + 3*t2;
    float h3 = t3 - 2*t2 + t;
    float h4 = t3 - t2;
    
    // Simple case: linear with smooth easing
    return y0 + (y1 - y0) * (3*t2 - 2*t3);
}

// In handleAngle(), replace map() call
if (RPM >= rpmsArr[i] && RPM <= rpmsArr[i+1]) {
    Advance_angle = splineInterpolate(RPM, rpmsArr[i], rpmsArr[i+1], 
                                      anglesArr[i], anglesArr[i+1]);
}
```

**Benefits:**
- Smoother angle transitions
- Reduces abrupt changes at map boundaries
- Better throttle response

---

### 5.3 Kalman Filter for RPM

**Context:** Provides noise-resistant RPM measurement with minimal lag, ideal for racing applications.

```cpp
// In Sensors.h, add global variables
float kalman_rpm = 0;
float kalman_p = 1.0;  // Estimate covariance
float kalman_q = 0.05; // Process noise (adjust for responsiveness)
float kalman_r = 0.5;  // Measurement noise (adjust for filtering)

void handleRpm() {
    // ... existing timePerDegree calculation ...
    
    int rawRPM = 60000000 / timePerDegree;
    
    if (firstRpm) {
        kalman_rpm = rawRPM;
        firstRpm = false;
    } else {
        // Kalman filter prediction
        kalman_p = kalman_p + kalman_q;
        
        // Kalman filter update
        float kalman_k = kalman_p / (kalman_p + kalman_r);
        kalman_rpm = kalman_rpm + kalman_k * (rawRPM - kalman_rpm);
        kalman_p = (1 - kalman_k) * kalman_p;
        
        RPM = (int)kalman_rpm;
    }
}
```

**Tuning:**
- Increase `kalman_q` for faster response (more noise)
- Increase `kalman_r` for more filtering (more lag)
- Typical values: q=0.05, r=0.5 for balanced performance

---

### 5.4 Temperature-based Angle Correction

**Context:** Reduces ignition advance at high engine temperatures to prevent knock and protect the engine.

```cpp
// In Ignition.h, add to handleAngle()
float tempCorrection = 0;

// Temperature correction: reduce advance above 80°C
if (Termo > 80) {
    // Subtract 0.1 degree per degree over 80°C
    tempCorrection = (Termo - 80) * 0.1;
    
    // Maximum correction of 5 degrees
    tempCorrection = min(tempCorrection, 5.0);
}

// Cold engine enrichment: add advance below 40°C
if (Termo < 40 && Termo > 0) {
    // Add 0.05 degree per degree under 40°C
    tempCorrection = tempCorrection - ((40 - Termo) * 0.05);
}

Advance_angle = Advance_angle - tempCorrection;

// Ensure advance doesn't go negative
Advance_angle = max(Advance_angle, 0);
```

**Behavior:**
- **Hot engine (>80°C):** Gradually reduces advance to prevent knock
- **Cold engine (<40°C):** Increases advance for better cold-start performance
- **Normal range (40-80°C):** No correction applied

---

## 6. Configuration Parameters

### 6.1 Main Parameters

| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| `Init_angle` | int | 0-60 | Base ignition angle in degrees before TDC |
| `Init_flash_time` | int | 100-5000 | Coil pulse duration in microseconds |
| `max_rpm` | int | 5000-20000 | Maximum engine RPM threshold |
| `max_rpm_angle_kiker` | int | 0-20 | Angle reduction at max RPM (degrees) |
| `filter` | int | 5-50 | RPM noise filter threshold (percentage change) |

**Example Configuration:**
```cpp
#define Init_angle 35           // Spark at 35° before TDC
#define Init_flash_time 2000    // 2ms coil pulse
#define max_rpm 14000           // Max RPM limit
#define max_rpm_angle_kiker 10  // Reduce by 10° at max RPM
#define filter 20               // Allow 20% RPM change
```

---

### 6.2 Functional Flags

| Flag | Type | Default | Description |
|------|------|---------|-------------|
| `ANGLE_ADVANCE` | bool | false | Enable throttle-based angle correction |
| `QUICKSHIFTER` | bool | false | Enable QuickShifter functionality |
| `HIGH_OUTPUT` | bool | true | Coil output active HIGH (false = LOW) |
| `INVERT_SENSOR` | bool | false | Invert TDC sensor signal |
| `INVERT_THROTTLE` | bool | false | Invert throttle sensor reading |
| `SERVO` | bool | false | Enable servo control |

**Example Configuration:**
```cpp
int ANGLE_ADVANCE = 1;       // throttle correction (int in the code)
int QUICKSHIFTER = 1;
boolean HIGH_OUTPUT = true;
boolean INVERT_SENSOR = false;
boolean INVERT_THROTTLE = true;
boolean SERVO = true;
```

---

### 6.3 Servo Parameters

| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| `Servo_type` | int | 0-1 | 0=map (`servo_valueArr`), 1=RPM on/off (`Servo_rpm`) |
| `Servo_rpm` | int | 1000-15000 | RPM activation threshold (type 1) |
| `Servo_percent` | int | 0-100 | Servo opening percentage (type 1) |

**Example Configurations:**

**Type 0 (по карте):**
```cpp
int Servo_type = 0;   // позиция на каждую точку карты задаётся в servo_valueArr
```

**Type 1 (RPM on/off):**
```cpp
int Servo_type = 1;
int Servo_rpm = 5000;      // открыть при 5000 RPM
int Servo_percent = 50;    // открыть на 50%
```

---

### 6.4 QuickShifter Parameters

| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| `QS_time` | int | 30-200 | Spark cut duration in milliseconds |
| `quickshifter_pin` | int | GPIO | Pin number for QuickShifter sensor |

**Example Configuration:**
```cpp
// QS_time не задаётся напрямую: он берётся из quick_adjustmentArr активной карты
// (мс, значение по умолчанию 135). Контакт быстрого шифтера — GPIO 17.
int QS_time = 135;
```

---

### 6.5 Sensor Pin Configuration

| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| `sensor_pin` | int | GPIO | Датчик ВМТ (реально GPIO 16) |
| `throttle_pin` | int | GPIO/ADC | Дроссель (реально GPIO 35, ADC1_CH7) |

**Example Configuration:**
```cpp
// Реальные контакты: PINS_DEF.h
const int sensor_pin   = 16;   // датчик ВМТ
const int throttle_pin = 35;   // дроссель (ADC1_CH7)
const int out_pin      = 19;   // выход на коммутатор
```

---

## Appendix

### File Structure Reference

```
src/core/ignition/
├── Ignition.h              # Main ignition control (handleAngle, handleIgnition)
├── Sensors.h               # Sensor reading (handlePin, handleRpm)
├── carts/
│   ├── Angles_7pt.h        # 7-point ignition maps
│   ├── Angles_12pt.h       # 12-point ignition maps
│   ├── Angles_22pt.h       # 22-point ignition maps
│   └── MOTO_CARTS.h        # Cart integration
└── RPMs.h                  # RPM point arrays

src/core/handlers/
└── QuickShifter.h          # QuickShifter control

Config.h                    # All configuration parameters
```

### Key Functions Reference

| Function | Location | Purpose |
|----------|----------|---------|
| `handleMoto()` | Main | Main control loop |
| `handlePin()` | Sensors.h | Read all sensors |
| `handleRpm()` | Sensors.h | Calculate RPM with filtering |
| `handleAngle()` | Ignition.h | Calculate advance angle |
| `handleIgnition()` | Ignition.h | Control coil output |
| `handleFlash()` | Ignition.h | Trigger spark |
| `handleQuickShifterControllPin()` | QuickShifter.h | Process QS input |
| `isQuickShifterActive()` | QuickShifter.h | Check QS block status |

---

**Document Version:** 1.0  
**Last Updated:** 2024  
**Target System:** DED_CORE_V4 Ignition Core  
**Scope:** Core ignition system only (network layer — see docs/api/README.md)
