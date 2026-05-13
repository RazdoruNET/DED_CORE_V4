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
**Область:** Только ядро системы зажигания (WEB_SERVER.h исключен)

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
The system implements a percentage-based filter to reject noise:

```cpp
// Calculate percentage change from previous RPM
int changePercent = abs((currentRPM - previousRPM) * 100 / previousRPM);

// Filter if change exceeds threshold
if (changePercent <= filter) {
    RPM = currentRPM;
}
```

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
  │ RPM =   │  │ Calculate change │
  │ rawRPM  │  │ percent          │
  └─────────┘  │ change = abs((raw │
               │ - prev) * 100 /  │
               │ prev)            │
               └────────┬─────────┘
                        │
                        ▼
               ┌──────────────────┐
               │ change <= filter?│
               └────────┬─────────┘
                        │
               Yes ─────┴───── No
                │              │
                ▼              ▼
       ┌────────────────┐ ┌──────────┐
       │ RPM = rawRPM   │ │ Ignore   │
       │ prevRPM = rawRPM│ │ (noise)  │
       └────────────────┘ └──────────┘
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

### 4.1 Adding New Ignition Map Type

**Location:** `src/core/ignition/carts/Angles_NewModel.h`

**Example Code:**
```cpp
// Angles_NewModel.h
#ifndef ANGLES_NEWMODEL_H
#define ANGLES_NEWMODEL_H

// 16-point ignition map for new engine model
const int anglesArr_NewModel[16] = {
    5,   // 0 RPM
    8,   // 1000 RPM
    12,  // 2000 RPM
    18,  // 3000 RPM
    25,  // 4000 RPM
    32,  // 5000 RPM
    38,  // 6000 RPM
    42,  // 7000 RPM
    45,  // 8000 RPM
    47,  // 9000 RPM
    48,  // 10000 RPM
    47,  // 11000 RPM
    45,  // 12000 RPM
    42,  // 13000 RPM
    38,  // 14000 RPM
    35   // 15000 RPM
};

// Throttle adjustment values (percentage)
const int angles_adjustmentArr_NewModel[16] = {
    0, 0, 2, 4, 6, 8, 10, 12, 14, 15, 15, 14, 12, 10, 8, 6
};

#endif
```

**Integration in `MOTO_CARTS.h`:**
```cpp
// Add to existing includes
#include "Angles_NewModel.h"

// Add to cart array
const int* anglesArr[] = {
    anglesArr_7pt,      // Cart 0
    anglesArr_12pt,     // Cart 1
    anglesArr_22pt,     // Cart 2
    anglesArr_NewModel  // Cart 3 (new)
};

// Add RPM points in RPMs.h if needed
const int rpmsArr_NewModel[16] = {
    0, 1000, 2000, 3000, 4000, 5000, 6000, 7000,
    8000, 9000, 10000, 11000, 12000, 13000, 14000, 15000
};
```

---

### 4.2 Custom RPM Filtering Algorithm

**Location:** `src/core/ignition/Sensors.h`, function `handleRpm()`

**Current Algorithm (Percentage-based):**
```cpp
void handleRpm() {
    // ... time measurement ...
    
    int rawRPM = 60000000 / timePerDegree;
    
    if (firstRpm) {
        RPM = rawRPM;
        previousRPM = rawRPM;
        firstRpm = false;
    } else {
        int changePercent = abs((rawRPM - previousRPM) * 100 / previousRPM);
        if (changePercent <= filter) {
            RPM = rawRPM;
            previousRPM = rawRPM;
        }
    }
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
// Add to Config.h
#define TEMP_PIN A3
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
#define MAP_PIN A4
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
if (SERVO) {
    if (Servo_type == 0) {
        // Map-based position
        Servo_pos = map(RPM, 0, max_rpm, Servo_angles[0], Servo_angles[1]);
    } else if (Servo_type == 1) {
        // RPM-based on/off
        if (RPM >= Servo_rpm) {
            Servo_pos = Servo_percent;
        } else {
            Servo_pos = 0;
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
#define ANGLE_ADVANCE true       // Use throttle correction
#define QUICKSHIFTER true        // Enable QuickShifter
#define HIGH_OUTPUT true         // Coil active HIGH
#define INVERT_SENSOR false      // Normal sensor polarity
#define INVERT_THROTTLE true     // Inverted throttle sensor
#define SERVO true               // Enable servo
```

---

### 6.3 Servo Parameters

| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| `Servo_type` | int | 0-3 | 0=map, 1=RPM on/off, 2=PID, 3=temp curve |
| `Servo_rpm` | int | 1000-15000 | RPM activation threshold (type 1) |
| `Servo_percent` | int | 0-100 | Servo opening percentage (type 1) |
| `Servo_angles[2]` | int[2] | 0-180 | Min/max angles for map (type 0) |

**Example Configurations:**

**Type 0 (Map-based):**
```cpp
#define Servo_type 0
#define Servo_angles {0, 90}  // 0° at idle, 90° at max RPM
```

**Type 1 (RPM on/off):**
```cpp
#define Servo_type 1
#define Servo_rpm 5000         // Open at 5000 RPM
#define Servo_percent 50      // Open to 50%
```

---

### 6.4 QuickShifter Parameters

| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| `QS_time` | int | 30-200 | Spark cut duration in milliseconds |
| `quickshifter_pin` | int | GPIO | Pin number for QuickShifter sensor |

**Example Configuration:**
```cpp
#define QS_time 60              // 60ms spark cut
#define quickshifter_pin 15     // GPIO 15
```

---

### 6.5 Sensor Pin Configuration

| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| `rpm_pin` | int | GPIO | TDC sensor input pin |
| `throttle_pin` | int | GPIO/ADC | Throttle sensor ADC pin |

**Example Configuration:**
```cpp
#define rpm_pin 2               // TDC sensor on GPIO 2
#define throttle_pin A0         // Throttle on ADC0
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
**Scope:** Core ignition system only (WEB_SERVER.h excluded)
