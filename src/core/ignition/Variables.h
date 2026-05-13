#ifndef VARIABLES_H
#define VARIABLES_H

// Include variable declarations from PROPS.h
#include "config/PROPS.h"

// Basic variables
int Modulator = 0;
int Servo_error = 0;
int OilTermo = 0;
int OilBar = 0;
int Flash = 0;

// Configuration arrays - declarations only, definitions in PROPS_DEF.h
extern float rpmsArr[][22];

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
int fix_micros = 60000000;
int startInit = 0;

// Sensor variables
int datchikThrottle = 0;
int datchikModulator = 0;

#endif // VARIABLES_H
