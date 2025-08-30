// HeaterMeter Copyright 2016 Bryan Mayland <bmayland@capnbry.net>
#ifndef __HMCORE_H__
#define __HMCORE_H__

#include "strings.h"

//#define PIEZO_HZ 4000             // enable piezo buzzer at this frequency

#include "grillpid.h"


void hmcoreSetup(void);
void hmcoreLoop(void);


void eepromLoadConfig(unsigned char forceDefault);
void storePidMode();
void storeSetPoint(int sp);
void loadProbeName(unsigned char probeIndex);
void storeAndReportProbeName(unsigned char probeIndex, const char *name);
void storeAndReportProbeOffset(unsigned char probeIndex, int offset);
void storeProbeAlarmOn(unsigned char probeIndex, bool isHigh, bool value);
void storeProbeAlarmVal(unsigned char probeIndex, bool isHigh, int value);
void storeAndReportMaxFanSpeed(unsigned char maxFanSpeed);
void setLcdBacklight(unsigned char lcdBacklight);
void storeLcdBacklight(unsigned char lcdBacklight);
void reportLcdParameters(void);
void Debug_begin(void);
void publishLeds(void);
#define Debug_end Serial_nl
void silenceRingingAlarm(void);

#define LIDPARAM_OFFSET 0
#define LIDPARAM_DURATION 1
#define LIDPARAM_ACTIVE 2
void storeLidParam(unsigned char idx, int val);

extern GrillPid pid;
extern unsigned char g_LcdBacklight;

#endif /* __HMCORE_H__ */
