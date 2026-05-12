#ifndef VARIABLES_H
#define VARIABLES_H

// Basic variables
int Modulator = 0;
float Throttle = 0;
int Servo_position = 0;
int Servo_type = 1;
int Servo_rpm = 8000;
int Servo_percent = 30;
int Servo_error = 0;
int RPM = 0;
int Termo = 0;
int OilTermo = 0;
int OilBar = 0;
float Advance_angle = 0;
int Init_angle = 43;
int Init_flash_time = 10;
int Flash = 0;

int type = 2;

// Configuration arrays
float rpmsArr[][22] = {
  { 0, 500, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000, 5500, 6000, 6500, 7000, 7500, 8000, 8500, 9000, 9500, 10000, 12000 },
  { 0, 500, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000, 5500, 6000, 6500, 7000, 7500, 8000, 8500, 9000, 9500, 10000, 12000 },
  { 0, 500, 1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000, 5500, 6000, 6500, 7000, 7500, 8000, 8500, 9000, 9500, 10000, 12000 }
};

float anglesArr[][22] = {
  { 21, 23, 24, 26, 29, 29, 31, 33, 35, 37, 39, 40, 41, 42, 43, 43, 42, 41, 40, 39, 28, 28 },
  { 21, 23, 24, 26, 29, 29, 31, 33, 35, 37, 39, 40, 41, 42, 43, 43, 42, 41, 40, 39, 28, 28 },
  { 21, 23, 24, 26, 29, 29, 31, 33, 35, 37, 39, 40, 41, 42, 43, 43, 42, 41, 40, 39, 28, 28 }
};

float angles_adjustmentArr[][22] = {
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

float servo_valueArr[][22] = {
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

int quick_adjustmentArr[][22] = {
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

// RPM limits
int max_rpm = 10000;
int max_rpm_angle_kiker = 4;

// Timing variables
int analog = 0;
int oldRpmMicro = 0;
int preOldRpmMicro = 0;
int oldIgnitionMicro = 0;
int oldIgnitionModulator = 0;
int diffMicro = 0;
unsigned long nextFlashMicro = 0;
unsigned long lastFlashMicro = 0;
int timePerDegree = 0;
int filter = 30;
int fix_micros = 60000000;
int startInit = 0;

// Sensor variables
int datchikThrottle = 0;
int datchikModulator = 0;

#endif // VARIABLES_H
