# OTA Руководство - DED_CORE_V4

## Обзор

DED_CORE_V4 поддерживает гибридную систему OTA (Over-The-Air) обновлений, которая объединяет автоматические обновления с сервера через HTTP и локальные обновления через ArduinoOTA.

## Архитектура

### Компоненты системы

```
src/core/ota/
├── ARDUINO_OTA.h/cpp    - Менеджер ArduinoOTA для локальных обновлений
├── OTA_MANAGER.h/cpp    - Менеджер HTTP OTA для обновлений с сервера
└── HYBRID_OTA.h/cpp     - Гибридный менеджер, объединяющий оба подхода
```

### Режимы работы

- **AUTO** (0) - Автоматические обновления с сервера через HTTP
- **MANUAL** (1) - Ручные обновления через ArduinoOTA
- **OFF** (2) - Обновления отключены

## Конфигурация

### Переменные в PROPS_DEF.h

```cpp
int OTA_MODE = 0;                    // Режим работы (0=AUTO, 1=MANUAL, 2=OFF)
String OTA_PASSWORD = "ded_ota_secure"; // Пароль для ArduinoOTA
String OTA_JWT_TOKEN = "";           // JWT токен для аутентификации на сервере
unsigned long OTA_CHECK_INTERVAL = 3600000; // Интервал проверки (1 час)
```

### Серверные настройки

```cpp
String serverName = "194.87.83.240"; // Адрес сервера обновлений
int updateServerPort = 443;           // Порт сервера
```

## HTTP API Эндпоинты

### Проверка обновлений
```
GET /ota/check
```

**Ответ:**
```json
{
  "updateAvailable": true,
  "currentVersion": "0.0.1",
  "mode": "AUTO",
  "newVersion": "0.0.2",
  "size": 1234567,
  "md5": "abc123...",
  "changelog": "Bug fixes and improvements"
}
```

### Статус системы
```
GET /ota/status
```

**Ответ:**
```json
{
  "currentVersion": "0.0.1",
  "mode": "AUTO",
  "initialized": true,
  "updateMode": false
}
```

### Запуск обновления
```
POST /ota/update
```

**Ответ:**
```
Update initiated, rebooting...
```

### Изменение режима
```
POST /ota/mode?mode=1
```

**Параметры:**
- `mode` - 0=AUTO, 1=MANUAL, 2=OFF

**Ответ:**
```json
{
  "mode": "MANUAL"
}
```

### Изменение пароля ArduinoOTA
```
POST /ota/password?password=newpassword
```

**Ответ:**
```
OK
```

## Использование ArduinoOTA

### Подключение к устройству

1. Убедитесь, что устройство в режиме MANUAL
2. Устройство появится в сети как `DED_CORE_V4.local`
3. Используйте PlatformIO для обновления:

```bash
pio run --target upload --upload-port DED_CORE_V4.local
```

### Настройка в Arduino IDE

1. Добавьте устройство в Network Ports
2. Выберите порт `DED_CORE_V4.local`
3. Загрузите прошивку через Sketch > Upload

## Безопасность

### Защита ArduinoOTA
- Парольная защита через `OTA_PASSWORD`
- Рекомендуется использовать сложные пароли
- Отключайте в production если не используется

### Защита HTTP OTA
- JWT аутентификация через заголовок `Authorization: Bearer <token>`
- Проверка MD5 хеша прошивки
- Валидация размера файла
- Таймауты для предотвращения зависаний

## Серверная часть

### Требования к API сервера

Сервер должен предоставлять следующие эндпоинты:

#### Проверка обновлений
```
GET /api/firmware/check?current={version}&device={device}
```

**Ответ:**
```json
{
  "updateAvailable": true,
  "version": "0.0.2",
  "downloadUrl": "http://server:8000/api/firmware/download/{id}",
  "size": 1234567,
  "md5": "abc123...",
  "changelog": "Bug fixes"
}
```

#### Скачивание прошивки
```
GET /api/firmware/download/{id}
```

**Ответ:** Бинарный файл прошивки

## Best Practices

### Для разработки
- Используйте режим MANUAL для быстрой отладки
- Отключайте автоматические проверки
- Используйте короткие интервалы проверки

### Для production
- Используйте режим AUTO для автоматических обновлений
- Установите длинные интервалы проверки (1-24 часа)
- Используйте HTTPS для загрузки
- Валидируйте цифровые подписи прошивок
- Тестируйте обновления на тестовых устройствах

### Мониторинг
- Логируйте все попытки обновления
- Отслеживайте успешность обновлений
- Мониторьте версию прошивки на устройствах
- Уведомляйте о доступных обновлениях

## Troubleshooting

### Обновление не начинается
- Проверьте режим OTA (должен быть AUTO)
- Убедитесь, что сервер доступен
- Проверьте JWT токен
- Посмотрите логи в Serial Monitor

### ArduinoOTA не работает
- Убедитесь, что режим MANUAL
- Проверьте пароль
- Убедитесь, что устройство в той же сети
- Проверьте firewall настройки

### Ошибка компиляции
- Убедитесь, что ArduinoJson v5 установлен
- Проверьте все include файлы
- Убедитесь, что все файлы OTA добавлены в проект

### Недостаточно памяти
- Увеличьте partition scheme
- Оптимизируйте код
- Удалите неиспользуемые библиотеки

## Интеграция с существующим кодом

### Добавление в setup()
```cpp
// В DED_CORE_V4.ino
HybridOTAManager otaManager;

void setup() {
    // ... существующий код ...
    
    OtaMode mode = static_cast<OtaMode>(OTA_MODE);
    otaManager.setMode(mode);
    otaManager.setJwtToken(OTA_JWT_TOKEN);
    otaManager.setArduinoOTAPassword(OTA_PASSWORD);
    otaManager.begin();
}
```

### Добавление в loop()
```cpp
// В ServerTask.h
void TaskServerCode(void * parameter) {
    // ... существующий код ...
    
    for(;;){ 
        dnsServer.processNextRequest();
        handleServer();
        otaManager.handle();  // Добавить эту строку
        vTaskDelay(2);
    }
}
```

## Будущие улучшения

- [ ] Поддержка HTTPS для HTTP OTA
- [ ] Цифровые подписи прошивок
- [ ] A/B partition system для rollback
- [ ] Автоматический rollback при ошибке
- [ ] Прогресс обновления через WebSocket
- [ ] Планирование обновлений по расписанию
- [ ] Canary deployments
- [ ] Метрики и аналитика обновлений

## Версия

**Версия OTA системы:** 1.0  
**Дата:** Май 2026  
**Совместимость:** DED_CORE_V4 0.0.1+
