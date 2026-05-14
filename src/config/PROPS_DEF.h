// PROPS_DEF.h - Variable definitions
// This file should be included only once in the main compilation unit

boolean preferences_clear = false;
boolean initSPIFFS = false;

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

String serverName = "194.87.83.240";
int updateServerPort = 443;

boolean UPDATE_MODE = false;
boolean UPDATE_RUN = false;
boolean UPDATE_CORE = true;
boolean SAVE_CONFIG = false;
boolean AFTER_REBOOT = false;

// OTA Configuration
int OTA_MODE = 0; // 0=AUTO, 1=MANUAL, 2=DISABLED
String OTA_PASSWORD = "ded_ota_secure";
String OTA_JWT_TOKEN = "";
unsigned long OTA_CHECK_INTERVAL = 3600000; // 1 hour

// Array definitions
float rpmsArr[3][22] = {
  { 0, 500, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000, 5500, 6000, 6500, 7000, 7500, 8000, 8500, 9000, 9500, 10000, 12000 },
  { 0, 500, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000, 5500, 6000, 6500, 7000, 7500, 8000, 8500, 9000, 9500, 10000, 12000 },
  { 0, 500, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000, 5500, 6000, 6500, 7000, 7500, 8000, 8500, 9000, 9500, 10000, 12000 }
};

float anglesArr[3][22] = {
  { 21, 23, 24, 26, 29, 29, 31, 33, 35, 37, 39, 40, 41, 42, 43, 43, 42, 41, 40, 39, 28, 28 },
  { 21, 23, 24, 26, 29, 29, 31, 33, 35, 37, 39, 40, 41, 42, 43, 43, 42, 41, 40, 39, 28, 28 },
  { 21, 23, 24, 26, 29, 29, 31, 33, 35, 37, 39, 40, 41, 42, 43, 43, 42, 41, 40, 39, 28, 28 }
};

int angles_adjustmentArr[22][3];
int quick_adjustmentArr[22][3];
int servo_valueArr[22][3];
