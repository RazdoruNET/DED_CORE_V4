#ifdef __cplusplus
extern "C" {
#endif

uint8_t temprature_sens_read();

#ifdef __cplusplus
}
#endif

uint8_t temprature_sens_read();

// Pin declarations - definitions in PINS_DEF.h
extern const int sensor_pin;
extern const int quickshifter_pin;
extern const int out_pin;
extern const int select_cart_up_pin;
extern const int select_cart_down_pin;
extern const int throttle_pin;

// Declare as extern to prevent multiple definitions
extern boolean preferences_clear;
extern boolean initSPIFFS;

extern int select_cart_in_up_proces;
extern int select_cart_in_down_proces;

extern int quickshifter_proces;

extern int QS_start;
extern int QS_time;

extern boolean TROT_SENSER;
extern boolean INIT_SYSTEM;
extern boolean SOUND;

extern int fps;
extern int fps_time;
extern int fps_sys;
extern int fps_time_sys;

extern int active_cart;

extern boolean LOOP_FIRST_STEP;

extern int start_micros;
extern int alarm_micros;
extern bool off_time_alarm;

extern int select_cart;
extern int select_qs;
extern int ANGLE_ADVANCE;
extern int QUICKSHIFTER;
extern int CARTS_SWHITCHING;
extern int max_rpm;
extern int max_rpm_angle_kiker;
extern int Advance_angle;
extern int RPM;
extern int Termo;
extern int Throttle;
extern int Init_angle;
extern int filter;
extern boolean HIGH_OUTPUT;
extern int Init_flash_time;
extern boolean INVERT_SENSOR;
extern boolean INVERT_THROTTLE;
extern boolean SERVO;
extern int Servo_position;
extern int Servo_type;
extern int Servo_rpm;
extern int Servo_percent;
extern int type;

extern String APSSID1;
extern String APPSK1;
extern String APSSID;
extern String APPSK;

// Array declarations
extern float rpmsArr[3][22];
extern float anglesArr[3][22];
extern int angles_adjustmentArr[22][3];
extern int quick_adjustmentArr[22][3];
extern int servo_valueArr[22][3];

// Additional variables
extern String serverName;
extern int updateServerPort;
extern boolean UPDATE_MODE;
extern boolean UPDATE_RUN;
extern boolean UPDATE_CORE;
extern boolean SAVE_CONFIG;
extern boolean AFTER_REBOOT;
