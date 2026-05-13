# 04. Расширяемые места кода

Этот раздел описывает ключевые места в кодовой базе, где можно расширить или настроить функциональность системы зажигания.

## 4.1 Добавление нового типа карты зажигания

**Расположение:** `src/core/ignition/carts/Angles_NewModel.h`

**Пример кода:**
```cpp
// Angles_NewModel.h
#ifndef ANGLES_NEWMODEL_H
#define ANGLES_NEWMODEL_H

// 16-точечная карта зажигания для новой модели двигателя
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

// Значения корректировки по дросселю (процент)
const int angles_adjustmentArr_NewModel[16] = {
    0, 0, 2, 4, 6, 8, 10, 12, 14, 15, 15, 14, 12, 10, 8, 6
};

#endif
```

**Интеграция в `MOTO_CARTS.h`:**
```cpp
// Добавить к существующим включениям
#include "Angles_NewModel.h"

// Добавить к массиву карт
const int* anglesArr[] = {
    anglesArr_7pt,      // Карта 0
    anglesArr_12pt,     // Карта 1
    anglesArr_22pt,     // Карта 2
    anglesArr_NewModel  // Карта 3 (новая)
};

// Добавить точки RPM в RPMs.h при необходимости
const int rpmsArr_NewModel[16] = {
    0, 1000, 2000, 3000, 4000, 5000, 6000, 7000,
    8000, 9000, 10000, 11000, 12000, 13000, 14000, 15000
};
```

---

## 4.2 Кастомный алгоритм фильтрации RPM

**Расположение:** `src/core/ignition/Sensors.h`, функция `handleRpm()`

**Текущий алгоритм (процентный):**
```cpp
void handleRpm() {
    // ... измерение времени ...
    
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

**Пример 1: Фильтр скользящего среднего**
```cpp
#define RPM_WINDOW_SIZE 5

int rpmWindow[RPM_WINDOW_SIZE] = {0};
int rpmWindowIndex = 0;

void handleRpm() {
    int rawRPM = 60000000 / timePerDegree;
    
    // Добавить в окно
    rpmWindow[rpmWindowIndex] = rawRPM;
    rpmWindowIndex = (rpmWindowIndex + 1) % RPM_WINDOW_SIZE;
    
    // Рассчитать среднее
    long sum = 0;
    for (int i = 0; i < RPM_WINDOW_SIZE; i++) {
        sum += rpmWindow[i];
    }
    RPM = sum / RPM_WINDOW_SIZE;
}
```

**Пример 2: Фильтр Калмана**
```cpp
float kalman_rpm = 0;
float kalman_p = 1.0;  // Ошибка оценки
float kalman_q = 0.1;  // Шум процесса
float kalman_r = 0.1;  // Шум измерения

void handleRpm() {
    int rawRPM = 60000000 / timePerDegree;
    
    // Прогноз
    kalman_p = kalman_p + kalman_q;
    
    // Обновление
    float kalman_k = kalman_p / (kalman_p + kalman_r);
    kalman_rpm = kalman_rpm + kalman_k * (rawRPM - kalman_rpm);
    kalman_p = (1 - kalman_k) * kalman_p;
    
    RPM = (int)kalman_rpm;
}
```

---

## 4.3 Кастомный алгоритм интерполяции угла

**Расположение:** `src/core/ignition/Ignition.h`, функция `handleAngle()`

**Текущий алгоритм (линейная интерполяция):**
```cpp
Advance_angle = map(RPM, rpmsArr[i], rpmsArr[i+1], 
                    anglesArr[i], anglesArr[i+1]);
```

**Пример 1: Кубическая сплайн-интерполяция**
```cpp
// Кубическая интерполяция между 4 точками
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

// Использование в handleAngle()
if (i > 0 && i < mapSize - 2) {
    Advance_angle = cubicInterpolate(RPM, 
        rpmsArr[i-1], rpmsArr[i], rpmsArr[i+1], rpmsArr[i+2],
        anglesArr[i-1], anglesArr[i], anglesArr[i+1], anglesArr[i+2]);
} else {
    // Откат к линейной на краях
    Advance_angle = map(RPM, rpmsArr[i], rpmsArr[i+1], 
                        anglesArr[i], anglesArr[i+1]);
}
```

**Пример 2: Экспоненциальная интерполяция**
```cpp
float expInterpolate(float x, float x0, float x1, float y0, float y1, float curve) {
    float t = (x - x0) / (x1 - x0);
    t = pow(t, curve);  // curve > 1 = экспоненциальная, curve < 1 = логарифмическая
    return y0 + t * (y1 - y0);
}

// Использование
Advance_angle = expInterpolate(RPM, rpmsArr[i], rpmsArr[i+1], 
                               anglesArr[i], anglesArr[i+1], 1.5);
```

---

## 4.4 Добавление нового датчика

**Расположение:** `src/core/ignition/Sensors.h`, функция `handlePin()`

**Пример 1: Датчик температуры**
```cpp
// Добавить в Config.h
#define TEMP_PIN A3
int Termo = 0;  // Глобальная переменная

// В handlePin()
void handlePin() {
    // ... существующие чтения датчиков ...
    
    // Чтение датчика температуры
    datchikTermo = analogRead(TEMP_PIN);
    
    // Преобразование в градусы Цельсия (пример для LM35)
    // LM35: 10мВ на градус, опорное 3.3В, 12-битный ADC
    Termo = (datchikTermo * 3300.0 / 4095.0) / 10.0;
}
```

**Интеграция в расчет угла:**
```cpp
// В handleAngle()
float tempCorrection = 0;
if (Termo > 80) {
    // Уменьшить угол опережения при высокой температуре
    tempCorrection = (Termo - 80) * 0.1;  // 0.1 градуса на градус выше 80°C
}
Advance_angle = Advance_angle - tempCorrection;
```

**Пример 2: Датчик MAP (абсолютное давление во впускном коллекторе)**
```cpp
// Добавить в Config.h
#define MAP_PIN A4
int MAP = 0;  // Глобальная переменная (кПа)

// В handlePin()
void handlePin() {
    // ... существующие чтения датчиков ...
    
    // Чтение датчика MAP
    datchikMAP = analogRead(MAP_PIN);
    
    // Преобразование в кПа (пример для MPX4250)
    // MPX4250: 0.2В при 20кПа, 4.7В при 250кПа
    float voltage = datchikMAP * 3.3 / 4095.0;
    MAP = (voltage - 0.2) * (250 - 20) / (4.7 - 0.2) + 20;
}
```

**Интеграция:**
```cpp
// В handleAngle()
float mapCorrection = (MAP - 100) * 0.05;  // Корректировка по давлению
Advance_angle = Advance_angle + mapCorrection;
```

---

## 4.5 Кастомная логика QuickShifter

**Расположение:** `src/core/handlers/QuickShifter.h`

**Текущий алгоритм (простая блокировка по времени):**
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

**Пример 1: Адаптивное время по RPM**
```cpp
void handleQuickShifterControllPin() {
    int quickshifter = digitalRead(quickshifter_pin);
    
    if (quickshifter == LOW) {
        if (quickshifter != quickshifter_proces) {
            // Адаптивное: выше RPM = короче время блокировки
            int adaptive_qs_time = QS_time * (10000 / max(RPM, 1000));
            QS_start = micros() + (adaptive_qs_time * 1000);
        }
    }
    
    quickshifter_proces = quickshifter;
}
```

**Пример 2: Блокировка нескольких искр**
```cpp
int QS_sparks_to_block = 3;  // Блокировать следующие 3 искры
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

## 4.6 Расширение управления сервоприводом

**Расположение:** `src/core/ignition/Ignition.h`, функция `handleAngle()`

**Текущий алгоритм (два типа):**
```cpp
if (SERVO) {
    if (Servo_type == 0) {
        // Позиция по карте
        Servo_pos = map(RPM, 0, max_rpm, Servo_angles[0], Servo_angles[1]);
    } else if (Servo_type == 1) {
        // Включение/выключение по RPM
        if (RPM >= Servo_rpm) {
            Servo_pos = Servo_percent;
        } else {
            Servo_pos = 0;
        }
    }
}
```

**Пример 1: ПИД-регулятор**
```cpp
// Переменные ПИД
float pid_target = 50.0;  // Целевая позиция сервопривода (%)
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

// В handleAngle()
if (SERVO && Servo_type == 2) {
    handleServoPID();
}
```

**Пример 2: Кривая по температуре**
```cpp
void handleServoTempCurve() {
    // Больше открывать сервопривод при высокой температуре
    float temp_factor = constrain((Termo - 20) / 80.0, 0, 1);  // 0 при 20°C, 1 при 100°C
    
    if (RPM >= Servo_rpm) {
        Servo_pos = Servo_percent + (temp_factor * 20);  // Добавить до 20% в зависимости от температуры
    } else {
        Servo_pos = temp_factor * 10;  // Базовое открытие в зависимости от температуры
    }
    
    Servo_pos = constrain(Servo_pos, 0, 100);
}

// В handleAngle()
if (SERVO && Servo_type == 3) {
    handleServoTempCurve();
}
```

---

**[← Назад: Блок-схемы](03-flowcharts.md)** | **[Далее: Примеры кода →](05-code-examples.md)**
