// PROPS_DEF.h - Variable definitions
// This file should be included only once in the main compilation unit

boolean preferences_clear = false;

int select_cart_in_up_proces = 0;
int select_cart_in_down_proces = 0;

int quickshifter_proces = 0;

int QS_start = 0;
int QS_time = 135;

boolean TROT_SENSER = true;
boolean INIT_SYSTEM = true;
boolean SOUND = false;

int fps = 0;
int fps_time = 0;
int fps_sys = 0;
int fps_time_sys = 0;

int active_cart = 0;

boolean LOOP_FIRST_STEP = true;

int start_micros = 0;
int alarm_micros = 120000000;
bool off_time_alarm = false;

int select_cart = 0;
int select_qs = 0;
int ANGLE_ADVANCE = 0;
int QUICKSHIFTER = 0;
int CARTS_SWHITCHING = 0;
int max_rpm = 0;
int max_rpm_angle_kiker = 0;
int Advance_angle = 0;
int RPM = 0;
int Termo = 0;
int Throttle = 0;
int Init_angle = 0;
int filter = 0;
boolean HIGH_OUTPUT = false;
int Init_flash_time = 0;
boolean INVERT_SENSOR = false;
boolean INVERT_THROTTLE = false;
boolean SERVO = false;
int Servo_position = 0;
int Servo_type = 0;
int Servo_rpm = 0;
int Servo_percent = 0;
int type = 0;

String APSSID1 = "DED_BOX_V4";
String APPSK1 = "162534Bnm123q";

String APSSID = "";
String APPSK = "";
bool WIFI_AUTO_UPDATE_ENABLED = false;
bool DIAGNOSTICS_WIFI_ENABLED = false;
String WIFI_UPDATE_SSID = "";
String WIFI_UPDATE_PASSWORD = "";
unsigned long WIFI_LAST_CHECK_TIME = 0;
unsigned long WIFI_LAST_ATTEMPT_TIME = 0;
String WIFI_UPDATE_STATUS = "idle";
unsigned long WIFI_LAST_CHECK_EPOCH = 0;
unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
unsigned long WIFI_RETRY_INTERVAL_MS = 600000;
unsigned long WIFI_CHECK_INTERVAL_MS = 1800000;
bool WIFI_SCAN_REQUESTED = false;

String serverName = "194.87.83.240";
int updateServerPort = 443;

boolean UPDATE_MODE = false;
boolean UPDATE_RUN = false;
boolean UPDATE_CORE = true;
boolean SAVE_CONFIG = false;
boolean AFTER_REBOOT = false;

// OTA Configuration
int OTA_MODE = 0; // 0=AUTO, 1=MANUAL, 2=OFF
String OTA_PASSWORD = "ded_ota_secure";
String OTA_JWT_TOKEN = "";
unsigned long OTA_CHECK_INTERVAL = 3600000; // 1 hour

// Array definitions
// По умолчанию — стоковая семиточечная карта на среднестатистический 250 см³
// (см. core/ignition/carts/Angles_Stock250.h): заводится и едет почти на любом
// одностволе 110…600 см³. Дальние точки — только «хвост» таблицы, прошивка их не читает,
// пока выбран тип на семь точек.
float rpmsArr[3][22] = {
  { 1200, 1800, 2500, 3500, 5000, 7000, 9500, 12000, 12000, 12000, 12000, 12000, 12000, 12000, 12000, 12000, 12000, 12000, 12000, 12000, 12000, 12000 },
  { 1200, 1800, 2500, 3500, 5000, 7000, 9500, 12000, 12000, 12000, 12000, 12000, 12000, 12000, 12000, 12000, 12000, 12000, 12000, 12000, 12000, 12000 },
  { 1200, 1800, 2500, 3500, 5000, 7000, 9500, 12000, 12000, 12000, 12000, 12000, 12000, 12000, 12000, 12000, 12000, 12000, 12000, 12000, 12000, 12000 }
};

float anglesArr[3][22] = {
  { 10, 15, 22, 27, 30, 32, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31 },
  { 10, 15, 22, 27, 30, 32, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31 },
  { 10, 15, 22, 27, 30, 32, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31 }
};

int angles_adjustmentArr[3][22];
int quick_adjustmentArr[3][22];
int servo_valueArr[3][22];
