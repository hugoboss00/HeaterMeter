// HeaterMeter Copyright 2016 Bryan Mayland <bmayland@capnbry.net>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <gom_server.h>
#include <pthread.h>
#define BOOST_SPIRIT_THREADSAFE
#include <boost/property_tree/ptree.hpp>

#include "econfig.h"
#include "digitalWriteFast.h"

#include "hmcore.h"


#include "bigchars.h"
#include "ledmanager.h"
#include "tone_4khz.h"
#include "systemif.h"
#include "serial.h"
#include "adc.h"

using namespace boost::property_tree;

static TempProbe probe0(PIN_PIT);
static TempProbe probe1(PIN_FOOD1);
static TempProbe probe2(PIN_FOOD2);
static TempProbe probe3(PIN_AMB);
GrillPid pid;
Serial CmdSerial;

static HMConfig hm_config;

// LCD and Menu system removed - HeaterMeter now runs headless

static char editString[PROBE_NAME_SIZE]; // Buffer for probe name editing



static void ledExecutor(unsigned char led, unsigned char on); // prototype
static LedManager ledmanager(&ledExecutor);

static unsigned char g_AlarmId; // ID of alarm going off
static unsigned char g_HomeDisplayMode;
static unsigned char g_LogPidInternals; // If non-zero then log PID interals
unsigned char g_LcdBacklight; // 0-100
unsigned char g_TestMode; // 0 = off, 1 = on (removed static for external access)
unsigned int g_PwmSliderValue; // PWM duty cycle value in microseconds (1000-2000) (removed static for external access)

#define config_store_byte(eeprom_field, src) { hm_config.econfig_write_byte((void *)offsetof(__eeprom_data, eeprom_field), src); }
#define config_store_word(eeprom_field, src) { hm_config.econfig_write_word((void *)offsetof(__eeprom_data, eeprom_field), src); }

#define EEPROM_MAGIC 0xf00e

static const struct __eeprom_data {
  unsigned int magic;
  int setPoint;
  unsigned char lidOpenOffset;
  unsigned int lidOpenDuration;
  float pidConstants[4]; // constants are stored Kb, Kp, Ki, Kd
  unsigned char pidMode;
  unsigned char lcdBacklight; // in PWM (max 100)
  char pidUnits;
  unsigned char fanMinSpeed;  // in percent
  unsigned char fanMaxSpeed;  // in percent
  unsigned char pidOutputFlags;
  unsigned char homeDisplayMode;
  unsigned char fanMaxStartupSpeed; // in percent
  unsigned char ledConf[LED_COUNT];
  unsigned char servoMinPos;  // in 10us
  unsigned char servoMaxPos;  // in 10us
  unsigned char fanActiveFloor; // in percent
  unsigned char servoActiveCeil; // in percent
} DEFAULT_CONFIG[]  = {
 {
  EEPROM_MAGIC,  // magic
  120,  // setpoint
  6,    // lid open offset %
  240,  // lid open duration
/*  { 0.0f, 4.0f, 0.02f, 5.0f },  // original PID constants */
  { 0.0f, 10.0f, 0.01f, 10.0f },  // PID constants
  PIDMODE_STARTUP,  // PID mode
  50,   // lcd backlight (%)
  'C',  // Units
  0,    // min fan speed
  100,  // max fan speed
  _BV(PIDFLAG_FAN_FEEDVOLT), // PID output flags bitmask
  0xff, // 2-line home
  100, // max startup fan speed
  { LEDSTIMULUS_FanMax, LEDSTIMULUS_LidOpen, LEDSTIMULUS_FanOn, LEDSTIMULUS_Off },
  150-50, // min servo pos = 1000us
  150+50,  // max servo pos = 2000us
  0, // fan active floor
  100 // servo active ceil
}
};

// EEPROM address of the start of the probe structs, the 2 bytes before are magic
#define EEPROM_PROBE_START  64

static const struct  __eeprom_probe DEFAULT_PROBE_CONFIG  = {
  "Probe  ", // Name if you change this change the hardcoded number-appender in eepromLoadProbeConfig()
  PROBETYPE_INTERNAL,  // probeType
  0,  // offset
  -40,  // alarm low
  -200, // alarm high
  0,  // unused1
  0,  // unused2
  {
    //2.4723753e-4,2.3402251e-4,1.3879768e-7  // Maverick ET-72/73
    //5.2668241e-4,2.0037400e-4,2.5703090e-8 // Maverick ET-732
	5.36924e-4,1.91396e-4,6.60399e-8 // hst:Maverick ET-732 (Honeywell R-T Curve 4)
    //8.98053228e-4,2.49263324e-4,2.04047542e-7 // Radio Shack 10k
    //1.14061e-3,2.32134e-4,9.63666e-8 // Vishay 10k NTCLE203E3103FB0
    //7.2237825e-4,2.1630182e-4,9.2641029e-8 // EPCOS100k
    //8.1129016e-4,2.1135575e-4,7.1761474e-8 // Semitec 104GT-2
    //7.3431401e-4,2.1574370e-4,9.5156860e-8 // ThermoWorks Pro-Series
    ,1.0e+4
  }
};

#ifdef PIEZO_HZ
// A simple beep-beep-beep-(pause) alarm
static const unsigned char tone_durs[] = { 10, 5, 10, 5, 10, 50 };  // in 10ms units
#define tone_cnt (sizeof(tone_durs)/sizeof(tone_durs[0]))
static unsigned char tone_idx;
static unsigned long tone_last;
#endif /* PIZEO_HZ */



void setLcdBacklight(unsigned char lcdBacklight)
{
  /* If the high bit is set, that means just set the output, do not store */
  if ((0x80 & lcdBacklight) == 0)
    g_LcdBacklight = lcdBacklight;
  lcdBacklight &= 0x7f;
  analogWrite(PIN_LCD_BACKLGHT, (unsigned int)(lcdBacklight) * 255 / 100);
}

// Note the storage loaders and savers expect the entire config storage is less than 256 bytes
static unsigned char getProbeConfigOffset(unsigned char probeIndex, unsigned char off)
{
  if (probeIndex >= TEMP_COUNT)
    return 0;
  // Point to the name in the first probe_config structure
  unsigned char retVal = EEPROM_PROBE_START + off;
  // Stride to the proper configuration structure
  retVal += probeIndex * sizeof( __eeprom_probe);
  
  return retVal;
}

static void storeProbeName(unsigned char probeIndex, const char *name)
{
  unsigned char ofs = getProbeConfigOffset(probeIndex, offsetof( __eeprom_probe, name));
  if (ofs != 0)
    hm_config.econfig_write_block(name, (void *)(uintptr_t)ofs, PROBE_NAME_SIZE);
}

void loadProbeName(unsigned char probeIndex)
{
  unsigned char ofs = getProbeConfigOffset(probeIndex, offsetof( __eeprom_probe, name));
  if (ofs != 0)
    hm_config.econfig_read_block(editString, (void *)(uintptr_t)ofs, PROBE_NAME_SIZE);
}

void storePidMode()
{
  unsigned char mode = pid.getPidMode();
  if (mode <= PIDMODE_AUTO_LAST)
    mode = PIDMODE_STARTUP;
  config_store_byte(pidMode, mode);
}

void storeSetPoint(int sp)
{
  // If the setpoint is >0 that's an actual setpoint.  
  // 0 or less is a manual fan speed
  if (sp > 0)
  {
    config_store_word(setPoint, sp);
    pid.setSetPoint(sp);
  }
  else
  {
    pid.setPidOutput(-sp);
  }

  storePidMode();
}

static void storePidUnits(char units)
{
  pid.setUnits(units);
  if (units == 'C' || units == 'F')
    config_store_byte(pidUnits, units);
}

static void storeProbeOffset(unsigned char probeIndex, int offset)
{
  unsigned char ofs = getProbeConfigOffset(probeIndex, offsetof( __eeprom_probe, tempOffset));
  if (ofs != 0)
  {
    pid.Probes[probeIndex]->Offset = offset;
    hm_config.econfig_write_byte((void *)(uintptr_t)ofs, offset);
  }  
}

static void storeProbeType(unsigned char probeIndex, unsigned char probeType)
{
  unsigned char ofs = getProbeConfigOffset(probeIndex, offsetof( __eeprom_probe, probeType));
  if (ofs != 0)
  {
    pid.setProbeType(probeIndex, probeType);
    hm_config.econfig_write_byte((void *)(uintptr_t)ofs, probeType);
  }
}


static void storeProbeTypeOrMap(unsigned char probeIndex, unsigned char probeType)
{
  /* If probeType is < 128 it is just a probe type */
  if (probeType < 128)
  {
    unsigned char oldProbeType = pid.Probes[probeIndex]->getProbeType();
    if (oldProbeType != probeType)
    {
      storeProbeType(probeIndex, probeType);
    }
  }  /* if probeType */
  
}

static void storeFanMinSpeed(unsigned char fanMinSpeed)
{
  pid.setFanMinSpeed(fanMinSpeed);
  config_store_byte(fanMinSpeed, pid.getFanMinSpeed());
}

static void storeFanMaxSpeed(unsigned char fanMaxSpeed)
{
  pid.setFanMaxSpeed(fanMaxSpeed);
  config_store_byte(fanMaxSpeed, pid.getFanMaxSpeed());
}

static void storeFanMaxStartupSpeed(unsigned char fanMaxStartupSpeed)
{
  pid.setFanMaxStartupSpeed(fanMaxStartupSpeed);
  config_store_byte(fanMaxStartupSpeed, pid.getFanMaxStartupSpeed());
}

static void storeFanActiveFloor(unsigned char fanActiveFloor)
{
  pid.setFanActiveFloor(fanActiveFloor);
  config_store_byte(fanActiveFloor, pid.getFanActiveFloor());
}

static void storeServoActiveCeil(unsigned char servoActiveCeil)
{
  pid.setServoActiveCeil(servoActiveCeil);
  config_store_byte(servoActiveCeil, pid.getServoActiveCeil());
}

static void storeServoMinPos(unsigned char servoMinPos)
{
  pid.setServoMinPos(servoMinPos);
  config_store_byte(servoMinPos, servoMinPos);
}

static void storeServoMaxPos(unsigned char servoMaxPos)
{
  pid.setServoMaxPos(servoMaxPos);
  config_store_byte(servoMaxPos, servoMaxPos);
}

static void storePidOutputFlags(unsigned char pidOutputFlags)
{
  pid.setOutputFlags(pidOutputFlags);
  config_store_byte(pidOutputFlags, pidOutputFlags);
}

void storeLcdBacklight(unsigned char lcdBacklight)
{
  lcdBacklight = constrain(lcdBacklight, 0, 100);
  setLcdBacklight(lcdBacklight);
  config_store_byte(lcdBacklight, lcdBacklight);
}

static void storeLedConf(unsigned char led, unsigned char ledConf)
{
  ledmanager.setAssignment(led, ledConf);

  unsigned char *ofs = (unsigned char *)offsetof(__eeprom_data, ledConf);
  ofs += led;
  hm_config.econfig_write_byte(ofs, ledConf);
}

static void toneEnable(bool enable)
{
#ifdef PIEZO_HZ
  if (enable)
  {
    if (tone_idx != 0xff)
      return;
    tone_last = 0;
    tone_idx = tone_cnt - 1;
  }
  else
  {
    tone_idx = 0xff;
    tone4khz_end();
    setLcdBacklight(g_LcdBacklight);
  }
#endif /* PIEZO_HZ */
}

static void lcdPrintBigNum(float val)
{
#if 0
  // good up to 3276.8
  int16_t ival = val * 10;
  uint16_t uval;
  bool isNeg;
  if (ival < 0)
  {
    isNeg = true;
    uval = -ival;
  }
  else
  {
    isNeg = false;
    uval = ival;
  }

  int8_t x = 16;
  do
  {
    if (uval != 0 || x >= 9)
    {
      const char *numData = NUMS + ((uval % 10) * 6);

      x -= C_WIDTH;
      lcd.setCursor(x, 0);
      lcd.write_P(numData, C_WIDTH);
      numData += C_WIDTH;

      lcd.setCursor(x, 1);
      lcd.write_P(numData, C_WIDTH);

      uval /= 10;
    }  /* if val */
    --x;
    lcd.setCursor(x, 0);
    lcd.write(C_BLK);
    lcd.setCursor(x, 1);
    if (x == 12)
      lcd.write('.');
    else if (uval == 0 && x < 9 && isNeg)
    {
      lcd.write(C_CT);
      isNeg = false;
    }
    else
      lcd.write(C_BLK);
  } while (x != 0);
  
#endif
}

static bool isMenuHomeState(void)
{
  // Menu system removed - always return true for headless operation
  return true;
}

void updateDisplay(void)
{
  // LCD removed - HeaterMeter now runs headless
  // Display update is handled via web interface
}

void lcdprint(const char *p, const bool doClear)
{
  // LCD removed - HeaterMeter now runs headless
  // Output via printf instead
  if (doClear)
    printf("\n--- LCD Clear ---\n");
  printf("LCD: %s\n", p);
}

static void storePidParam(char which, float value)
{
  unsigned char k;
  switch (which)
  {
    case 'b': k = 0; break;
    case 'p': k = 1; break;
    case 'i': k = 2; break;
    case 'd': k = 3; break;
    default:
      return;
  }
  pid.setPidConstant(k, value);

  unsigned char ofs = offsetof(__eeprom_data, pidConstants[0]);
  hm_config.econfig_write_block(&pid.Pid[k], (void *)(ofs + k * sizeof(float)), sizeof(value));
}



static void reportProbeCoeff(unsigned char probeIdx)
{
  TempProbe *p = pid.Probes[probeIdx];
  printf("Probe %d Coefficients: A=%.6e B=%.6e C=%.6e R=%.1f Type=%d\n",
         probeIdx, p->Steinhart[0], p->Steinhart[1], p->Steinhart[2], 
         p->Steinhart[3], p->getProbeType());
}

static void storeProbeCoeff(unsigned char probeIndex, const char *vals)
{
  // vals is SteinA(float),SteinB(float),SteinC(float),RKnown(float),probeType+1(int)|probeMap(char+int)
  // If any value is blank, it won't be modified
  unsigned char ofs = getProbeConfigOffset(probeIndex, offsetof( __eeprom_probe, steinhart));
  if (ofs == 0)
    return;
    
  unsigned char idx = 0;
  while (*vals)
  {
    if (idx >= STEINHART_COUNT)
      break;
    if (*vals == ',')
    {
      ++idx;
      ++vals;
      ofs += sizeof(float);
    }
    else
    {
      float *fDest = &pid.Probes[probeIndex]->Steinhart[idx];
      *fDest = atof(vals);
      hm_config.econfig_write_block(fDest, (void *)(uintptr_t)ofs, sizeof(float));
      while (*vals && *vals != ',')
        ++vals;
    }
  }

  if (*vals)
    storeProbeTypeOrMap(probeIndex, atoi(vals));
  reportProbeCoeff(probeIndex);
}


static void reportProbeNames(void)
{
  CmdSerial.write(("HMPN"));
  for (unsigned char i=0; i<TEMP_COUNT; ++i)
  {
    loadProbeName(i);
    Serial_csv();
    CmdSerial.write(editString);
  }
  Serial_nl();
}

static void reportPidParams(void)
{
  printf("PID Parameters: B=%.3f P=%.3f I=%.3f D=%.3f\n",
         pid.Pid[0], pid.Pid[1], pid.Pid[2], pid.Pid[3]);
}

static void reportProbeOffsets(void)
{
  printf("Probe Offsets: ");
  for (unsigned char i=0; i<TEMP_COUNT; ++i)
  {
    printf("P%d=%d", i, pid.Probes[i]->Offset);
    if (i < TEMP_COUNT - 1) printf(" ");
  }
  printf("\n");
}

void storeAndReportProbeOffset(unsigned char probeIndex, int offset)
{
  storeProbeOffset(probeIndex, offset);
  reportProbeOffsets();
}

void storeAndReportProbeName(unsigned char probeIndex, const char *name)
{
  storeProbeName(probeIndex, name);
  pid.Probes[probeIndex]->setName(name);
  reportProbeNames();
}

static void reportVersion(void)
{
  printf("Version: UCID=HeaterMeter %s\n", hm_version);
}

static void reportLidParameters(void)
{
  printf("Lid Parameters: Offset=%d%% Duration=%ds\n", 
         pid.LidOpenOffset, pid.getLidOpenDuration());
}

void reportLcdParameters(void)
{
  printf("LCD Parameters: Backlight=%d%% DisplayMode=%d LEDs:", 
         g_LcdBacklight, g_HomeDisplayMode);
  for (unsigned char i=0; i<LED_COUNT; ++i)
  {
    printf(" LED%d=%d", i, ledmanager.getAssignment(i));
  }
  printf("\n");
}

void storeLcdParam(unsigned char idx, int val)
{
  switch (idx)
  {
    case 0:
      storeLcdBacklight(val);
      break;
    case 1:
      g_HomeDisplayMode = val;
      config_store_byte(homeDisplayMode, g_HomeDisplayMode);
      // LCD removed - no need to clear display
      break;
    case 2:
    case 3:
    case 4:
    case 5:
      storeLedConf(idx - 2, val);
      break;
  }
}

static void reportProbeCoeffs(void)
{
  for (unsigned char i=0; i<TEMP_COUNT; ++i)
    reportProbeCoeff(i);
}

static void reportAlarmLimits(void)
{
  printf("Alarm Limits: ");
  for (unsigned char i=0; i<TEMP_COUNT; ++i)
  {
    ProbeAlarm &a = pid.Probes[i]->Alarms;
    printf("P%d Low=%d", i, a.getLow());
    if (a.getLowRinging()) printf("(RINGING)");
    printf(" High=%d", a.getHigh());
    if (a.getHighRinging()) printf("(RINGING)");
    if (i < TEMP_COUNT - 1) printf(" | ");
  }
  printf("\n");
}

static void reportFanParams(void)
{
  printf("Fan Parameters: MinSpeed=%d%% MaxSpeed=%d%% ServoMin=%d ServoMax=%d OutputFlags=0x%x MaxStartup=%d%% ActiveFloor=%d%% ServoCeil=%d%%\n",
         pid.getFanMinSpeed(), pid.getFanMaxSpeed(), pid.getServoMinPos(), pid.getServoMaxPos(),
         pid.getOutputFlags(), pid.getFanMaxStartupSpeed(), pid.getFanActiveFloor(), pid.getServoActiveCeil());
}

void storeAndReportMaxFanSpeed(unsigned char maxFanSpeed)
{
  storeFanMaxSpeed(maxFanSpeed);
  reportFanParams();
}

static void reportConfig(void)
{
  reportVersion();
  reportPidParams();
  reportFanParams();
  reportProbeNames();
  reportProbeCoeffs();
  reportProbeOffsets();
  reportLidParameters();
  reportLcdParameters();
  reportAlarmLimits();
}

typedef void (*csv_int_callback_t)(unsigned char idx, int val);

static void csvParseI(const char *vals, csv_int_callback_t c)
{
  unsigned char idx = 0;
  while (*vals)
  {
    if (*vals == ',')
    {
      ++idx;
      ++vals;
    }
    else
    {
      int val = atoi(vals);
      c(idx, val);
      while (*vals && *vals != ',')
        ++vals;
    }
  }
}

void storeLidParam(unsigned char idx, int val)
{
  if (val < 0)
    val = 0;

  switch (idx)
  {
    case 0:
      pid.LidOpenOffset = val;
      config_store_byte(lidOpenOffset, val);
      break;
    case 1:
      pid.setLidOpenDuration(val);
      config_store_word(lidOpenDuration, val);
      break;
    case 2:
      if (val)
        pid.resetLidOpenResumeCountdown();
      else
        pid.LidOpenResumeCountdown = 0;
      break;
  }
}

/* storeAlarmLimits: Expects pairs of data L,H,L,H,L,H,L,H one for each probe,
   the passed index is coincidently the ALARM_ID */
static void storeAlarmLimits(unsigned char idx, int val)
{
  unsigned char probeIndex = ALARM_ID_TO_PROBE(idx);
  ProbeAlarm &a = pid.Probes[probeIndex]->Alarms;
  unsigned char alarmIndex = ALARM_ID_TO_IDX(idx);
  a.setThreshold(alarmIndex, val);

  unsigned char ofs = getProbeConfigOffset(probeIndex, offsetof( __eeprom_probe, alarmLow));
  if (ofs != 0 && val != 0)
  {
    ofs += alarmIndex * sizeof(val);
    hm_config.econfig_write_block(&val, (void *)(uintptr_t)ofs, sizeof(val));
  }
}

void silenceRingingAlarm(void)
{
  /*
  unsigned char probeIndex = ALARM_ID_TO_PROBE(g_AlarmId);
  ProbeAlarm &a = pid.Probes[probeIndex]->Alarms;
  unsigned char alarmIndex = ALARM_ID_TO_IDX(g_AlarmId);
  storeAlarmLimits(g_AlarmId, disable ? -a.getThreshold(alarmIndex) : 0);
  */
  storeAlarmLimits(g_AlarmId, 0);
  reportAlarmLimits();
}

static void storeFanParams(unsigned char idx, int val)
{
  switch (idx)
  {
    case 0:
      storeFanMinSpeed(val);
      break;
    case 1:
      storeFanMaxSpeed(val);
      break;
    case 2:
      storeServoMinPos(val);
      break;
    case 3:
      storeServoMaxPos(val);
      break;
    case 4:
      storePidOutputFlags(val);
      break;
    case 5:
      storeFanMaxStartupSpeed(val);
      break;
    case 6:
      storeFanActiveFloor(val);
      break;
    case 7:
      storeServoActiveCeil(val);
      break;
  }
}

static void setTempParam(unsigned char idx, int val)
{
  switch (idx)
  {
    case 0:
      g_LogPidInternals = val;
      break;
#if defined(NOISEDUMP_PIN)
    case 1:
      extern volatile unsigned char g_NoisePin;
      g_NoisePin = val;
      break;
#endif
  }
}

void handleCommandUrl(const char *URL)
{
  unsigned char urlLen = strlen(URL);
  printf("handle url: %s\n",URL);
  
  if (strncmp(URL, ("set?sp="), 7) == 0) 
  {
    // store the units first, in case of 'O' disabling the PID output
    storePidUnits(URL[urlLen - 1]);
    // prevent sending "C" or "F" which would setpoint(0)
    if (*(URL+7) <= '9')
      storeSetPoint(atoi(URL + 7));
  }
  else if (strncmp(URL, ("set?lb="), 7) == 0)
  {
    csvParseI(URL + 7, storeLcdParam);
    reportLcdParameters();
  }
  else if (strncmp(URL, ("set?ld="), 7) == 0)
  {
    csvParseI(URL + 7, storeLidParam);
    reportLidParameters();
  }
  else if (strncmp(URL, ("set?po="), 7) == 0)
  {
    csvParseI(URL + 7, storeProbeOffset);
    reportProbeOffsets();
  }
  else if (strncmp(URL, ("set?pid"), 7) == 0 && urlLen > 9)
  {
    float f = atof(URL + 9);
    storePidParam(URL[7], f);
    reportPidParams();
  }
  else if (strncmp(URL, ("set?pn"), 6) == 0 && urlLen > 8)
  {
    // Store probe name will only store it if a valid probe number is passed
    storeAndReportProbeName(URL[6] - '0', URL + 8);
  }
  else if (strncmp(URL, ("set?pc"), 6) == 0 && urlLen > 8)
  {
    storeProbeCoeff(URL[6] - '0', URL + 8);
  }
  else if (strncmp(URL, ("set?al="), 7) == 0)
  {
    csvParseI(URL + 7, storeAlarmLimits);
    reportAlarmLimits();
  }
  else if (strncmp(URL, ("set?fn="), 7) == 0)
  {
    csvParseI(URL + 7, storeFanParams);
    reportFanParams();
  }
  else if (strncmp(URL, ("set?tt="), 7) == 0)
  {
    // Toast messages now go to console (menu system removed)
    printf("Toast: %s\n", URL+7);
  }
  else if (strncmp(URL, ("set?tp="), 7) == 0)
  {
    csvParseI(URL + 7, setTempParam);
  }
  else if (strncmp(URL, ("set?pwm="), 8) == 0)
  {
    g_PwmSliderValue = atoi(URL + 8);
    // Clamp value to valid range
    if (g_PwmSliderValue < 1000) g_PwmSliderValue = 1000;
    if (g_PwmSliderValue > 2000) g_PwmSliderValue = 2000;
    printf("PWM slider set to: %d us\n", g_PwmSliderValue);
  }
  else if (strncmp(URL, ("set?testmode="), 13) == 0)
  {
    g_TestMode = atoi(URL + 13) ? 1 : 0;
    printf("Test mode %s\n", g_TestMode ? "enabled" : "disabled");
  }
  else if (strncmp(URL, ("config"), 6) == 0)
  {
    reportConfig();
  }
  else if (strncmp(URL, ("reboot"), 5) == 0)
  {
  }
}


extern int hm_AdcPins[];
static void outputAdcStatus(void)
{
  printf("ADC Status: ");
  for (unsigned char i=0; i<NUM_ANALOG_INPUTS; ++i)
  {
    printf("ADC%d=%d ", i, adc.analogReadRange(hm_AdcPins[i]));
  }
  printf("\n");
}

static void tone_doWork(void)
{
#ifdef PIEZO_HZ
  if (tone_idx == 0xff)
    return;
  unsigned int elapsed = millis() - tone_last;
  unsigned int dur = tone_durs[tone_idx] * 10;
  if (elapsed > dur)
  {
    tone_last = millis();
    tone_idx = (tone_idx + 1) % tone_cnt;
    if (tone_idx % 2 == 0)
    {
      dur = tone_durs[tone_idx] * 10;
      tone4khz_begin(PIN_ALARM, dur);
      setLcdBacklight(0x80 | 0);
    }
    else
      setLcdBacklight(0x80 | g_LcdBacklight);
  }
#endif /* PIEZO_HZ */
}

static void checkAlarms(void)
{
  bool anyRinging = false;
  for (unsigned char i=0; i<TEMP_COUNT; ++i)
  {
    for (unsigned char j=ALARM_IDX_LOW; j<=ALARM_IDX_HIGH; ++j)
    {
      bool ringing = pid.Probes[i]->Alarms.Ringing[j];
      unsigned char alarmId = MAKE_ALARM_ID(i, j);
      if (ringing)
      {
        anyRinging = true;
        g_AlarmId = alarmId;
      }
      ledmanager.publish(LEDSTIMULUS_Alarm0L + alarmId, ringing);
    }
  }

  ledmanager.publish(LEDSTIMULUS_AlarmAny, anyRinging);
  if (anyRinging)
  {
    reportAlarmLimits();
    // Menu system removed - alarm state managed via web interface
  }
  // Menu system removed - no alarm state tracking needed
}

static void eepromLoadBaseConfig(unsigned char forceDefault)
{
  // The compiler likes to join eepromLoadBaseConfig and eepromLoadProbeConfig s
  // this union saves stack space by reusing the same memory area for both structs
  union {
    struct __eeprom_data base;
    struct __eeprom_probe probe;
  } config;

  hm_config.econfig_read_block(&config.base, 0, sizeof(__eeprom_data));
  forceDefault = forceDefault || config.base.magic != EEPROM_MAGIC;
  if (forceDefault != 0)
  {
    memcpy(&config.base, &DEFAULT_CONFIG[forceDefault - 1], sizeof(__eeprom_data));
    hm_config.econfig_write_block(&config.base, 0, sizeof(__eeprom_data));
  }
  
  pid.setSetPoint(config.base.setPoint);
  pid.LidOpenOffset = config.base.lidOpenOffset;
  pid.setLidOpenDuration(config.base.lidOpenDuration);
  memcpy(pid.Pid, config.base.pidConstants, sizeof(config.base.pidConstants));
  pid.setPidMode(config.base.pidMode);
  setLcdBacklight(config.base.lcdBacklight);
  pid.setUnits(config.base.pidUnits == 'C' ? 'C' : 'F');
  pid.setFanMinSpeed(config.base.fanMinSpeed);
  pid.setFanMaxSpeed(config.base.fanMaxSpeed);
  pid.setFanActiveFloor(config.base.fanActiveFloor);
  pid.setFanMaxStartupSpeed(config.base.fanMaxStartupSpeed);
  pid.setOutputFlags(config.base.pidOutputFlags);
  g_HomeDisplayMode = config.base.homeDisplayMode;
  pid.setServoMinPos(config.base.servoMinPos);
  pid.setServoMaxPos(config.base.servoMaxPos);
  pid.setServoActiveCeil(config.base.servoActiveCeil);

  for (unsigned char led = 0; led<LED_COUNT; ++led)
    ledmanager.setAssignment(led, config.base.ledConf[led]);
}

static void eepromLoadProbeConfig(unsigned char forceDefault)
{
  // The compiler likes to join eepromLoadBaseConfig and eepromLoadProbeConfig s
  // this union saves stack space by reusing the same memory area for both structs
  union {
    struct __eeprom_data base;
    struct __eeprom_probe probe;
  } config;

  // instead of this use below because we don't have eeprom_read_word linked yet
  //magic = eeprom_read_word((uint16_t *)(EEPROM_PROBE_START-sizeof(magic))); 
  hm_config.econfig_read_block(&config.base, (void *)(EEPROM_PROBE_START-sizeof(config.base.magic)), sizeof(config.base.magic));
  if (config.base.magic != EEPROM_MAGIC)
  {
    forceDefault = 1;
    hm_config.econfig_write_word((void *)(EEPROM_PROBE_START-sizeof(config.base.magic)), EEPROM_MAGIC);
  }
    
  struct  __eeprom_probe *p;
  p = (struct  __eeprom_probe *)(EEPROM_PROBE_START);
  for (unsigned char i=0; i<TEMP_COUNT; ++i)
  {
    if (forceDefault != 0)
    {
      memcpy(&config.probe, &DEFAULT_PROBE_CONFIG, sizeof( __eeprom_probe));
      // Hardcoded to change the last character of the string instead of [strlen(config.name)-1]
      config.probe.name[6] = '0' + i;
      hm_config.econfig_write_block(&config.probe, p, sizeof(__eeprom_probe));
    }
    else
      hm_config.econfig_read_block(&config.probe, p, sizeof(__eeprom_probe));

    pid.Probes[i]->loadConfig(&config.probe);
    ++p;
  }  /* for i<TEMP_COUNT */
}

void eepromLoadConfig(unsigned char forceDefault)
{
  eepromLoadBaseConfig(forceDefault);
  eepromLoadProbeConfig(forceDefault);
}

static void blinkLed(void)
{
  // This function only works the first time, when all the LEDs are assigned to
  // LedStimulus::Off, and OneShot turns them on for one blink
  ledmanager.publish(LEDSTIMULUS_Off, LEDACTION_OneShot);
  ledmanager.doWork();
}


/* Starts a debug log output message line, end with Debug_end() */
void Debug_begin(void)
{
    CmdSerial.write(("HMLG" CSV_DELIMITER));
}

void publishLeds(void)
{
  ledmanager.publish(LEDSTIMULUS_Off, LEDACTION_Off);
  ledmanager.publish(LEDSTIMULUS_LidOpen, pid.isLidOpen());
  ledmanager.publish(LEDSTIMULUS_FanOn, pid.isOutputActive());
  ledmanager.publish(LEDSTIMULUS_FanMax, pid.isOutputMaxed());
  ledmanager.publish(LEDSTIMULUS_PitTempReached, pid.isPitTempReached());
  ledmanager.publish(LEDSTIMULUS_Startup, pid.getPidMode() == PIDMODE_STARTUP);
  ledmanager.publish(LEDSTIMULUS_Recovery, pid.getPidMode() == PIDMODE_RECOVERY);
}

static void newTempsAvail(void)
{
  static unsigned char pidCycleCount;

  updateDisplay();
  ++pidCycleCount;
    

  pid.status();
  // We want to report the status before the alarm readout so
  // receivers can tell what the value was that caused the alarm
  checkAlarms();

  if (g_LogPidInternals)
    pid.pidStatus();

  if ((pidCycleCount % 0x04) == 1)
    outputAdcStatus();

  publishLeds();

}

static void lcdDefineChars(void)
{
  // LCD removed - HeaterMeter now runs headless
}

static void ledExecutor(unsigned char led, unsigned char on)
{
  switch (led)
  {
    case 0:
      digitalWrite(PIN_WIRELESS_LED, on?1:0);
      break;
    default:
      // LCD LEDs removed - could add GPIO-based LEDs here if needed
      printf("LED %d: %s\n", led-1, on ? "ON" : "OFF");
      break;
  }
}

void hmcoreSetup(void)
{
  pinModeFast(PIN_WIRELESS_LED, OUTPUT);
  blinkLed();
  
  reportVersion();



#ifdef PIEZO_HZ
  tone4khz_init();
#endif
  pid.Probes[TEMP_PIT] = &probe0;
  pid.Probes[TEMP_FOOD1] = &probe1;
  pid.Probes[TEMP_FOOD2] = &probe2;
  pid.Probes[TEMP_AMB] = &probe3;

  eepromLoadConfig(0);
  pid.init();
  lcdDefineChars();
  
  // Initialize PWM slider and test mode
  g_TestMode = 0;
  g_PwmSliderValue = 1500; // Default to middle value
  
  // Menu system removed - HeaterMeter runs headless
}


void getConfigData(ptree &pt)
{
	for (int i=0; i<4; i++)
	{
		pid.addProbeConfig(i, pt);
	}
	pt.put("ucid", hm_version);
	pt.put("sp", pid.getSetPoint());
	//LCD
	pt.put("lb", g_LcdBacklight);
	pt.put("lbn", g_HomeDisplayMode);

	pt.put("pidp", pid.getPidConstant(PIDP));
	pt.put("pidi", pid.getPidConstant(PIDI));
	pt.put("pidd", pid.getPidConstant(PIDD));
	pt.put("oflag", pid.getOutputFlags());
	pt.put("fflor", pid.getFanActiveFloor());
	pt.put("fmin", pid.getFanMinSpeed());
	pt.put("fmax", pid.getFanMaxSpeed());
	pt.put("fsmax", pid.getFanMaxStartupSpeed());

	pt.put("smin", pid.getServoMinPos());
	pt.put("smax", pid.getServoMaxPos());
	pt.put("sceil", pid.getServoActiveCeil());

	//LID
	pt.put("lo", pid.LidOpenOffset);
	pt.put("ld", pid.getLidOpenDuration());
	
	//LED
	pt.put("le0", ledmanager.getAssignment(0));
	pt.put("le1", ledmanager.getAssignment(1));
	pt.put("le2", ledmanager.getAssignment(2));
	pt.put("le3", ledmanager.getAssignment(3));

	//PWM and Test Mode
	pt.put("pwm", g_PwmSliderValue);
	pt.put("testmode", g_TestMode);

}

int getPidData(ptree &pt)
{
	if (g_LogPidInternals)
	{
		static ptree oldpt;
		pid.pidStatus(pt);
		if (pt != oldpt)
		{
			oldpt = pt;
			return 1;
		}
	}
	return 0;
}

/* return 1 if data has changed compared to last call, 0 otherwise */
int getProbeData(ptree &pt)
{
	static ptree oldtemppt;
    ptree fan, adc, adcs;
	ptree temps;
	unsigned long nowtime = time(NULL);
	int fanspeed = pid.getFanSpeed();
	if (fanspeed < pid.getFanMinSpeed())
		fanspeed = pid.getFanMinSpeed();
	if (fanspeed > pid.getFanMaxSpeed())
		fanspeed = pid.getFanMaxSpeed();
	
	
	pt.put("time", nowtime);
	pt.put("set", pid.getSetPoint());
	pt.put("lid", pid.LidOpenResumeCountdown);
	fan.put("c", pid.getFanSpeed());
	fan.put("a", pid.getFanSpeed());
	fan.put("f",fanspeed);
	pt.add_child("fan",fan);
	for (int i=0; i<6; i++)
	{
		// Create an unnamed node containing the value
		ptree adc;
		adc.put("", "0");

		// Add this node to the list.
		adcs.push_back(std::make_pair("", adc));
	}		
	pt.add_child("adc",adcs);
	
	for (int i=0; i<4; i++)
	{
		pid.addProbeValues(i, temps);
	}
	pt.add_child("temps",temps);
	
	if (temps != oldtemppt)
	{
		oldtemppt = temps;
		return 1;
	}
	return 1;
}

void getHistory(stringstream &csv, int timespan)
{
	pid.getHistoryCsv(csv, timespan);
}

void hmcoreLoop(void)
{ 

  if (pid.doWork())
  {
	newTempsAvail();
	pid.writeHistory();
  }
  // Menu system removed - no menu processing needed
  tone_doWork();
  ledmanager.doWork();
}


int main(int argc, char **argv)
{
	pthread_setname_np(pthread_self(), "gom_main");
	hmcoreSetup();
	start_server();
	while (1)
	{
		hmcoreLoop();
		// wait 5ms
		delayMicroseconds(5000);
	}
}
