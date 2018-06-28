// HeaterMeter Copyright 2016 Bryan Mayland <bmayland@capnbry.net>
#ifndef __HMCORE_H__
#define __HMCORE_H__

#include "strings.h"

#define HEATERMETER_SERIAL 38400 // enable serial interface
//#define HEATERMETER_RFM12  RF12_915MHZ  // enable RFM12B receiving (433MHZ|868MHZ|915MHZ)
//#define PIEZO_HZ 4000             // enable piezo buzzer at this frequency
#define SHIFTREGLCD_NATIVE        // Use the native shift register instead of SPI (HM PCB <v3.2)

#include "ShiftRegLCD.h"
#include "grillpid.h"
#include "hmmenus.h"


void hmcoreSetup(void);
void hmcoreLoop(void);

void updateDisplay(void);
void lcdprint(const char *p, const bool doClear);

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
extern ShiftRegLCDNative lcd;
extern unsigned char g_LcdBacklight;

#endif /* __HMCORE_H__ */
