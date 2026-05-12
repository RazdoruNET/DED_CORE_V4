#ifdef __cplusplus
extern "C" {
#endif

uint8_t temprature_sens_read();

#ifdef __cplusplus
}
#endif

uint8_t temprature_sens_read();

const int sensor_pin = 16;
const int quickshifter_pin = 17;
const int out_pin = 19;
const int select_cart_up_pin = 32;
const int select_cart_down_pin = 33;
const int throttle_pin = 35;

boolean preferences_clear = false;
boolean initSPIFFS = false;

int select_cart_in_up_proces = 0;
int select_cart_in_down_proces = 0;

int quickshifter_proces = 0;

int QS_start = 0;
int QS_time = 135;

int select_cart = 0;
int select_qs = 0;

String APSSID = "";
String APPSK = "";

String APSSID1 = "DED_BOX_V4";
String APPSK1 = "162534Bnm123q";
                 
String serverName = "194.87.83.240";

int updateServerPort = 443;

boolean INVERT_SENSOR = false;
boolean INVERT_THROTTLE = false;
boolean ANGLE_ADVANCE = false;
boolean QUICKSHIFTER = false;
boolean UPDATE_MODE = false;
boolean UPDATE_RUN = false;
boolean UPDATE_CORE = true;
boolean SAVE_CONFIG = false;
boolean AFTER_REBOOT = false;
boolean CARTS_SWHITCHING = false;

boolean HIGH_OUTPUT = true;

boolean SERVO = false;
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
