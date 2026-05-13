# 01. Математические основы

Этот раздел охватывает математические формулы и расчеты, используемые в ядре системы зажигания DED_CORE_V4.

## 1.1 Расчет оборотов двигателя (RPM)

**Формула:**
```
RPM = 60000000 / timePerDegree
```

**Объяснение переменных:**
- `timePerDegree`: Время между импульсами датчика ВМТ в микросекундах
- `RPM`: Обороты двигателя в минуту
- `60000000`: Коэффициент преобразования (60 секунд × 1,000,000 микросекунд)

**Алгоритм фильтрации шума:**
Система реализует процентный фильтр для подавления шума:

```cpp
// Расчет процентного изменения от предыдущего RPM
int changePercent = abs((currentRPM - previousRPM) * 100 / previousRPM);

// Фильтрация, если изменение превышает порог
if (changePercent <= filter) {
    RPM = currentRPM;
}
```

**Примеры расчетов:**

| timePerDegree (мкс) | RPM |
|---------------------|-----|
| 20000               | 3000 |
| 10000               | 6000 |
| 6667                | 9000 |
| 5000                | 12000 |

**Детектирование остановки двигателя:**
Если импульс датчика не получен в течение 500мс, двигатель считается остановленным:
```cpp
if (micros() - lastRpmMicro > 500000) {
    RPM = 0;
}
```

---

## 1.2 Расчет угла опережения зажигания

**Базовый угол из карты:**
```
Advance_angle = anglesArr[active_cart][rpmIndex]
```

**Линейная интерполяция между точками карты:**
```
Advance_angle = map(RPM, rpmsArr[i], rpmsArr[i+1], 
                    anglesArr[i], anglesArr[i+1])
```

Где `map()` выполняет линейную интерполяцию:
```
map(x, in_min, in_max, out_min, out_max) = 
    (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min
```

**Корректировка по дросселю (если ANGLE_ADVANCE=true):**
```
Advance_angle = Advance_angle + ((angles_adjustmentArr[i] / 100) * Throttle)
```

- `angles_adjustmentArr[i]`: Коэффициент корректировки из карты (процент)
- `Throttle`: Положение дросселя (0-100%)

**Ограничение на максимальных оборотах:**
```
if (RPM >= max_rpm) {
    Advance_angle = Advance_angle - max_rpm_angle_kiker
}
```

- `max_rpm`: Порог максимальных оборотов двигателя
- `max_rpm_angle_kiker`: Уменьшение угла на высоких оборотах (градусы)

---

## 1.3 Расчет времени искры

**Разница углов:**
```
diffAngle = Init_angle - Advance_angle
```

- `Init_angle`: Базовый угол зажигания (градусы до ВМТ)
- `diffAngle`: Градусы ожидания перед искрой

**Время до искры:**
```
diffMicros = diffAngle * (timePerDegree / 360)
```

- `diffMicros`: Микросекунды ожидания перед искрой
- `timePerDegree / 360`: Микросекунды на градус поворота коленвала

**Время искры:**
```
nextFlashMicro = oldRpmMicro + diffMicros
```

- `oldRpmMicro`: Метка времени последнего импульса датчика ВМТ
- `nextFlashMicro`: Абсолютная метка времени для запуска искры

---

## 1.4 Расчет положения дросселя

**Чтение ADC:**
```
datchikThrottle = analogRead(throttle_pin)
```

**Нормализация (0-100%):**
```
Throttle = datchikThrottle * 100 / 4095
```

- `4095`: Максимальное значение ADC (12-битный ADC)

**Инверсия (если INVERT_THROTTLE=true):**
```
Throttle = 100 - Throttle
```

**Плавная фильтрация:**
Значение дросселя изменяется шагами ±5% для предотвращения дребезга:
```cpp
if (Throttle > previousThrottle + 5) {
    Throttle = previousThrottle + 5;
} else if (Throttle < previousThrottle - 5) {
    Throttle = previousThrottle - 5;
}
```

---

**[← Назад к README](../README.md)** | **[Далее: Временные диаграммы →](02-timing-diagrams.md)**
