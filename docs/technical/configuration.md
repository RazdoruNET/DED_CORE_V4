# Конфигурация DED_CORE_V4

## Основные параметры

### Системные настройки
```cpp
// Версия прошивки
#define FIRMWARE_VERSION "0.0.1"

// Максимальные обороты
int max_rpm = 10000;

// Коррекция при отсечке
int max_rpm_angle_kiker = 4;

// Начальный угол зажигания
int Init_angle = 43;

// Время искры
int Init_flash_time = 10;
```

### Настройки датчиков
```cpp
// Фильтр RPM
int filter = 30;

// Инверсия датчика положения
bool INVERT_SENSOR = false;

// Инверсия датчика дросселя
bool INVERT_THROTTLE = false;

// Тип выхода (HIGH/LOW)
bool HIGH_OUTPUT = true;
```

### Функциональные флаги
```cpp
// Коррекция угла по дросселю
bool ANGLE_ADVANCE = true;

// Быстрое переключение передач
bool QUICKSHIFTER = false;

// Переключение карт кнопками
bool CARTS_SWHITCHING = true;

// Звуковая сигнализация
bool SOUND = true;

// Управление сервоприводом
bool SERVO = false;
```

## Карточки зажигания

### Типы карт
- **Type 0**: 7 точек (базовая настройка)
- **Type 1**: 12 точек (средняя точность)
- **Type 2**: 22 точки (максимальная точность)

### Структура карты
```cpp
// RPM точки
float rpmsArr[3][22] = {
    {0, 500, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000, 5500, 6000, 6500, 7000, 7500, 8000, 8500, 9000, 9500, 10000, 10500},
    {0, 500, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000, 5500, 6000, 6500, 7000, 7500, 8000, 8500, 9000, 9500, 10000, 10500},
    {0, 500, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000, 5500, 6000, 6500, 7000, 7500, 8000, 8500, 9000, 9500, 10000, 10500}
};

// Углы зажигания
float anglesArr[3][22] = {
    {21, 23, 24, 26, 28, 30, 32, 34, 35, 36, 37, 37, 37, 36, 35, 34, 32, 30, 28, 26, 24, 22},
    {21, 23, 24, 26, 28, 30, 32, 34, 35, 36, 37, 37, 37, 36, 35, 34, 32, 30, 28, 26, 24, 22},
    {21, 23, 24, 26, 28, 30, 32, 34, 35, 36, 37, 37, 37, 36, 35, 34, 32, 30, 28, 26, 24, 22}
};

// Коррекция по дросселю
float angles_adjustmentArr[3][22] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
};
```

## Предустановленные двигатели

### YX125
```cpp
float anglesArr_yx125[22] = {
    21, 23, 24, 26, 28, 30, 32, 34, 35, 36, 37, 37, 37, 36, 35, 34, 32, 30, 28, 26, 24, 22
};
```

### YX140
```cpp
float anglesArr_yx140[22] = {
    20, 22, 23, 25, 27, 29, 31, 33, 34, 35, 36, 36, 36, 35, 34, 33, 31, 29, 27, 25, 23, 21
};
```

### SmartCom
```cpp
float anglesArr_smartcom[22] = {
    22, 24, 25, 27, 29, 31, 33, 35, 36, 37, 38, 38, 38, 37, 36, 35, 33, 31, 29, 27, 25, 23
};
```

### ZS190
```cpp
float anglesArr_zs190[22] = {
    18, 20, 21, 23, 25, 27, 29, 31, 32, 33, 34, 34, 34, 33, 32, 31, 29, 27, 25, 23, 21, 19
};
```

### 166FMM
```cpp
float anglesArr_166fmm[22] = {
    19, 21, 22, 24, 26, 28, 30, 32, 33, 34, 35, 35, 35, 34, 33, 32, 30, 28, 26, 24, 22, 20
};
```

### 175FMM
```cpp
float anglesArr_175fmm[22] = {
    17, 19, 20, 22, 24, 26, 28, 30, 31, 32, 33, 33, 33, 32, 31, 30, 28, 26, 24, 22, 20, 18
};
```

## Настройки WiFi

### Основная сеть
```cpp
String APSSID = "DED_BOX_V4";
String APPSK = "162534Bnm123q";
```

### Сеть обновлений
```cpp
String UPDATOR_SSID = "DED_UPDATOR";
String UPDATOR_PSK = "9876543210";
```

## Сервопривод

### Настройки сервопривода
```cpp
bool SERVO = false;
int Servo_type = 1;
int Servo_rpm = 8000;
int Servo_percent = 30;

// Позиции сервопривода
int servo_valueArr[3][22] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
};
```

## Быстрое переключение передач

### Настройки быстрого переключения
```cpp
bool QUICKSHIFTER = false;

// Время коррекции для каждой точки
int quick_adjustmentArr[3][22] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
};
```

## Сохранение и загрузка

### NVS Preferences
```cpp
void loadConfiguration() {
    preferences.begin("ded_box", false);
    
    // Загрузка основных параметров
    active_cart = preferences.getInt("active_cart", 0);
    type = preferences.getInt("type", 2);
    max_rpm = preferences.getInt("max_rpm", 10000);
    filter = preferences.getInt("filter", 30);
    Init_angle = preferences.getInt("Init_angle", 43);
    
    // Загрузка флагов
    HIGH_OUTPUT = preferences.getBool("HIGH_OUTPUT", true);
    ANGLE_ADVANCE = preferences.getBool("ANGLE_ADVANCE", true);
    QUICKSHIFTER = preferences.getBool("QUICKSHIFTER", false);
    INVERT_SENSOR = preferences.getBool("INVERT_SENSOR", false);
    INVERT_THROTTLE = preferences.getBool("INVERT_THROTTLE", false);
    CARTS_SWHITCHING = preferences.getBool("CARTS_SWHITCHING", true);
    SERVO = preferences.getBool("SERVO", false);
    
    // Загрузка массивов
    preferences.getBytes("rpmsArr", rpmsArr, sizeof(rpmsArr));
    preferences.getBytes("anglesArr", anglesArr, sizeof(anglesArr));
    preferences.getBytes("angles_adjustmentArr", angles_adjustmentArr, sizeof(angles_adjustmentArr));
    preferences.getBytes("quick_adjustmentArr", quick_adjustmentArr, sizeof(quick_adjustmentArr));
    preferences.getBytes("servo_valueArr", servo_valueArr, sizeof(servo_valueArr));
    
    preferences.end();
}

void saveConfiguration() {
    preferences.begin("ded_box", false);
    
    // Сохранение основных параметров
    preferences.putInt("active_cart", active_cart);
    preferences.putInt("type", type);
    preferences.putInt("max_rpm", max_rpm);
    preferences.putInt("filter", filter);
    preferences.putInt("Init_angle", Init_angle);
    
    // Сохранение флагов
    preferences.putBool("HIGH_OUTPUT", HIGH_OUTPUT);
    preferences.putBool("ANGLE_ADVANCE", ANGLE_ADVANCE);
    preferences.putBool("QUICKSHIFTER", QUICKSHIFTER);
    preferences.putBool("INVERT_SENSOR", INVERT_SENSOR);
    preferences.putBool("INVERT_THROTTLE", INVERT_THROTTLE);
    preferences.putBool("CARTS_SWHITCHING", CARTS_SWHITCHING);
    preferences.putBool("SERVO", SERVO);
    
    // Сохранение массивов
    preferences.putBytes("rpmsArr", rpmsArr, sizeof(rpmsArr));
    preferences.putBytes("anglesArr", anglesArr, sizeof(anglesArr));
    preferences.putBytes("angles_adjustmentArr", angles_adjustmentArr, sizeof(angles_adjustmentArr));
    preferences.putBytes("quick_adjustmentArr", quick_adjustmentArr, sizeof(quick_adjustmentArr));
    preferences.putBytes("servo_valueArr", servo_valueArr, sizeof(servo_valueArr));
    
    preferences.end();
}
```

## Сброс настроек

### Полный сброс
```cpp
void resetToDefaults() {
    // Сброс основных параметров
    active_cart = 0;
    type = 2;
    max_rpm = 10000;
    filter = 30;
    Init_angle = 43;
    
    // Сброс флагов
    HIGH_OUTPUT = true;
    ANGLE_ADVANCE = true;
    QUICKSHIFTER = false;
    INVERT_SENSOR = false;
    INVERT_THROTTLE = false;
    CARTS_SWHITCHING = true;
    SERVO = false;
    
    // Загрузка стандартных карт
    loadDefaultMaps();
    
    // Сохранение настроек
    saveConfiguration();
}
```

### Сброс NVS
```cpp
void clearNVS() {
    Serial.println("Очистка NVS...");
    nvs_flash_erase();
    nvs_flash_init();
    Serial.println("NVS очищен");
    
    // Перезагрузка системы
    ESP.restart();
}
```

## Валидация настроек

### Проверка диапазонов
```cpp
bool validateConfiguration() {
    // Проверка основных параметров
    if (active_cart < 0 || active_cart > 2) return false;
    if (type < 0 || type > 2) return false;
    if (max_rpm < 1000 || max_rpm > 20000) return false;
    if (filter < 1 || filter > 100) return false;
    if (Init_angle < 0 || Init_angle > 90) return false;
    
    // Проверка карт
    for (int cart = 0; cart < 3; cart++) {
        for (int i = 0; i < 22; i++) {
            if (rpmsArr[cart][i] < 0 || rpmsArr[cart][i] > 20000) return false;
            if (anglesArr[cart][i] < 0 || anglesArr[cart][i] > 90) return false;
        }
    }
    
    return true;
}
```

---

Эта документация предоставляет полное описание всех конфигурационных параметров системы и методов их сохранения.
