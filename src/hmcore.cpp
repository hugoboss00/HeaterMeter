// HeaterMeter Copyright 2016 Bryan Mayland <bmayland@capnbry.net>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <gom_server.h>

#include "econfig.h"
#include "digitalWriteFast.h"

#include "hmcore.h"

#ifdef HEATERMETER_RFM12
#include "rfmanager.h"
#endif

#include "bigchars.h"
#include "ledmanager.h"
#include "tone_4khz.h"
#include "systemif.h"
#include "serial.h"
#include "adc.h"

static TempProbe probe0(PIN_PIT);
static TempProbe probe1(PIN_FOOD1);
static TempProbe probe2(PIN_FOOD2);
static TempProbe probe3(PIN_AMB);
GrillPid pid;
Serial CmdSerial;

static HMConfig hm_config;

#ifdef SHIFTREGLCD_NATIVE
ShiftRegLCDNative lcd(LCD_DATA, PIN_LCD_CLK, TWO_WIRE, 2);
#else
ShiftRegLCD lcd(PIN_LCD_CLK, 2);
#endif /* SHIFTREGLCD_NATIVE */

#ifdef HEATERMETER_SERIAL
static char g_SerialBuff[64]; 
#endif /* HEATERMETER_SERIAL */

#ifdef HEATERMETER_RFM12
static void rfSourceNotify(RFSource &r, unsigned char event); // prototype
static RFManager rfmanager(&rfSourceNotify);
static unsigned char rfMap[TEMP_COUNT];
#endif /* HEATERMETER_RFM12 */

static void ledExecutor(unsigned char led, unsigned char on); // prototype
static LedManager ledmanager(&ledExecutor);

static unsigned char g_AlarmId; // ID of alarm going off
static unsigned char g_HomeDisplayMode;
static unsigned char g_LogPidInternals; // If non-zero then log PID interals
unsigned char g_LcdBacklight; // 0-100

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
#ifdef HEATERMETER_RFM12
  unsigned char rfMap[TEMP_COUNT];
#endif
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
  { 0.0f, 10.0f, 0.005f, 10.0f },  // PID constants
  PIDMODE_STARTUP,  // PID mode
  50,   // lcd backlight (%)
#ifdef HEATERMETER_RFM12
  { RFSOURCEID_ANY, RFSOURCEID_ANY, RFSOURCEID_ANY, RFSOURCEID_ANY },  // rfMap
#endif
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

#ifdef HEATERMETER_RFM12
static void reportRfMap(void)
{
  CmdSerial.write(("HMRM"));
  for (unsigned char i=0; i<TEMP_COUNT; ++i)
  {
    Serial_csv();
    if (pid.Probes[i]->getProbeType() == PROBETYPE_RF12)
      SerialX.print(rfMap[i], DEC);
  }
  Serial_nl();
}

static void checkInitRfManager(void)
{
  if (pid.countOfType(PROBETYPE_RF12) != 0)
    rfmanager.init(HEATERMETER_RFM12);
}

static void storeRfMap(unsigned char probeIndex, unsigned char source)
{
  rfMap[probeIndex] = source;

  unsigned char *ofs = (unsigned char *)offsetof(__eeprom_data, rfMap);
  ofs += probeIndex;
  hm_config.econfig_write_byte(ofs, source);

  reportRfMap();
  checkInitRfManager();
}
#endif /* HEATERMETER_RFM12 */

static void storeProbeTypeOrMap(unsigned char probeIndex, unsigned char probeType)
{
  /* If probeType is < 128 it is just a probe type */
  if (probeType < 128)
  {
    unsigned char oldProbeType = pid.Probes[probeIndex]->getProbeType();
    if (oldProbeType != probeType)
    {
      storeProbeType(probeIndex, probeType);
#ifdef HEATERMETER_RFM12
      if (oldProbeType == PROBETYPE_RF12)
        storeRfMap(probeIndex, RFSOURCEID_ANY);
#endif /* HEATERMETER_RFM12 */
    }
  }  /* if probeType */
  
#ifdef HEATERMETER_RFM12
  /* If probeType > 128 then it is an wireless probe and the value is 128+source ID */
  else
  {
    unsigned char newSrc = probeType - 128;
    /* Force the storage of TempProbe::setProbeType() if the src changes
       because we need to clear Temperature and any accumulated ADC readings */
    if (pid.Probes[probeIndex]->getProbeType() != PROBETYPE_RF12 ||
      rfMap[probeIndex] != newSrc)
      storeProbeType(probeIndex, PROBETYPE_RF12);
    storeRfMap(probeIndex, newSrc);
  }  /* if RF map */
#endif /* HEATERMETER_RFM12 */
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
  state_t state = Menus.getState();
  return (state >= ST_HOME_FOOD1 && state <= ST_HOME_ALARM);
}

void updateDisplay(void)
{
  // Updates to the temperature can come at any time, only update 
  // if we're in a state that displays them
  state_t state = Menus.getState();
  if (!isMenuHomeState())
    return;

  char buffer[17];
  unsigned char probeIdxLow, probeIdxHigh;

  // Fixed pit area
  lcd.setCursor(0, 0);
  if (state == ST_HOME_ALARM)
  {
    toneEnable(true);
    if (ALARM_ID_TO_IDX(g_AlarmId) == ALARM_IDX_LOW)
      lcdprint(("** ALARM LOW  **"), false);
    else
      lcdprint(("** ALARM HIGH **"), false);

    probeIdxLow = probeIdxHigh = ALARM_ID_TO_PROBE(g_AlarmId);
  }  /* if ST_HOME_ALARM */
#if 0
  else if (state == ST_PITCAL)
  {
    int pit = (int)(pid.Probes[TEMP_PIT]->Temperature * 10.0f);
    snprintf(buffer, sizeof(buffer), ("%4uADC   %4d" DEGREE "%c"),
      adc.analogReadOver(PIN_PIT, 10),
      pit,
      pid.getUnits());
    lcd.print(buffer);

    snprintf(buffer, sizeof(buffer), ("Ref=%3u    Nz=%2u"),
      analogGetBandgapScale(),
      adc.analogReadRange(PIN_PIT)
      );
    lcd.setCursor(0, 1);
    lcd.print(buffer);
    return;
  } /* if ST_PITCAL */
#endif
  else
  {
    toneEnable(false);

    /* Big Number probes overwrite the whole display if it has a temperature */
    if (g_HomeDisplayMode >= TEMP_PIT && g_HomeDisplayMode <= TEMP_AMB)
    {
      TempProbe *probe = pid.Probes[g_HomeDisplayMode];
      if (probe->hasTemperature())
      {
        lcdPrintBigNum(probe->Temperature);
        return;
      }
    }

    /* Default Pit / Fan Speed first line */
    int pitTemp;
    if (pid.Probes[TEMP_CTRL]->hasTemperature())
      pitTemp = pid.Probes[TEMP_CTRL]->Temperature;
    else
      pitTemp = 0;
    if (!pid.isManualOutputMode() && !pid.Probes[TEMP_CTRL]->hasTemperature())
      memcpy(buffer, LCD_LINE1_UNPLUGGED, sizeof(LCD_LINE1_UNPLUGGED));
    else if (pid.isDisabled())
      snprintf(buffer, sizeof(buffer), ("Pit:%3d" DEGREE "%c  [Off]"),
        pitTemp, pid.getUnits());
    else if (pid.LidOpenResumeCountdown > 0)
      snprintf(buffer, sizeof(buffer), ("Pit:%3d" DEGREE "%c Lid%3u"),
        pitTemp, pid.getUnits(), pid.LidOpenResumeCountdown);
    else
    {
      char c1,c2;
      if (pid.isManualOutputMode())
      {
        c1 = '^';  // LCD_ARROWUP
        c2 = '^';  // LCD_ARROWDN
      }
      else
      {
        c1 = '[';
        c2 = ']';
      }
      snprintf(buffer, sizeof(buffer), ("Pit:%3d" DEGREE "%c %c%3u%%%c"),
        pitTemp, pid.getUnits(), c1, pid.getPidOutput(), c2);
    }

    lcd.print(buffer);
    // Display mode 0xff is 2-line, which only has space for 1 non-pit value
    if (g_HomeDisplayMode == 0xff)
      probeIdxLow = probeIdxHigh = state - ST_HOME_FOOD1 + TEMP_FOOD1;
    else
    {
      // Display mode 0xfe is 4 line home, display 3 other temps there
      probeIdxLow = TEMP_FOOD1;
      probeIdxHigh = TEMP_AMB;
    }
  } /* if !ST_HOME_ALARM */

  // Rotating probe display
  for (unsigned char probeIndex=probeIdxLow; probeIndex<=probeIdxHigh; ++probeIndex)
  {
    if (probeIndex < TEMP_COUNT && pid.Probes[probeIndex]->hasTemperature())
    {
      loadProbeName(probeIndex);
      snprintf(buffer, sizeof(buffer), ("%-12s%3d" DEGREE), editString,
        (int)pid.Probes[probeIndex]->Temperature);
    }
    else
    {
      // If probeIndex is outside the range (in the case of ST_HOME_NOPROBES)
      // just fill the bottom line with spaces
      memset(buffer, ' ', sizeof(buffer));
      buffer[sizeof(buffer) - 1] = '\0';
    }

    lcd.setCursor(0, probeIndex - probeIdxLow + 1);
    lcd.print(buffer);
  }
}

void lcdprint(const char *p, const bool doClear)
{
  if (doClear)
    lcd.clear();
  lcd.print(p);
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

static void outputCsv(void)
{
#ifdef HEATERMETER_SERIAL
  CmdSerial.write(("HMSU" CSV_DELIMITER));
  pid.status();
  Serial_nl();
#endif /* HEATERMETER_SERIAL */
}

#if defined(HEATERMETER_SERIAL)
static void printSciFloat(float f)
{
  // This function could use a rework, it is pretty expensive
  // in terms of space and speed. 
  char exponent = 0;
  bool neg = f < 0.0f;
  if (neg)
    f *= -1.0f;
  while (f < 1.0f && f != 0.0f)
  {
    --exponent;
    f *= 10.0f;
  }
  while (f >= 10.0f)
  {
    ++exponent;
    f /= 10.0f;
  }
  if (neg)
    f *= -1.0f;
  CmdSerial.write(f);
  CmdSerial.write('e');
  CmdSerial.write(exponent, DEC);
}

static void reportProbeCoeff(unsigned char probeIdx)
{
  CmdSerial.write(("HMPC" CSV_DELIMITER));
  CmdSerial.write( probeIdx, DEC);
  Serial_csv();
  
  TempProbe *p = pid.Probes[probeIdx];
  for (unsigned char i=0; i<STEINHART_COUNT; ++i)
  {
    printSciFloat(p->Steinhart[i]);
    Serial_csv();
  }
  CmdSerial.write(p->getProbeType(), DEC);
  Serial_nl();
}

static void storeProbeCoeff(unsigned char probeIndex, char *vals)
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
  CmdSerial.write(("HMPD"));
  for (unsigned char i=0; i<4; ++i)
  {
    Serial_csv();
    //printSciFloat(pid.Pid[i]);
    CmdSerial.write(pid.Pid[i]);
  }
  Serial_nl();
}

static void reportProbeOffsets(void)
{
  CmdSerial.write(("HMPO"));
  for (unsigned char i=0; i<TEMP_COUNT; ++i)
  {
    Serial_csv();
    CmdSerial.write( pid.Probes[i]->Offset, DEC);
  }
  Serial_nl();
}

void storeAndReportProbeOffset(unsigned char probeIndex, int offset)
{
  storeProbeOffset(probeIndex, offset);
  reportProbeOffsets();
}

void storeAndReportProbeName(unsigned char probeIndex, const char *name)
{
  storeProbeName(probeIndex, name);
  reportProbeNames();
}

static void reportVersion(void)
{
  CmdSerial.write(("UCID" CSV_DELIMITER "HeaterMeter" CSV_DELIMITER HM_VERSION));
  Serial_nl();
}

static void reportLidParameters(void)
{
  CmdSerial.write(("HMLD" CSV_DELIMITER));
  CmdSerial.write(pid.LidOpenOffset, DEC);
  Serial_csv();
  CmdSerial.write(pid.getLidOpenDuration(), DEC);
  Serial_nl();
}

void reportLcdParameters(void)
{
  CmdSerial.write(("HMLB" CSV_DELIMITER));
  CmdSerial.write(g_LcdBacklight, DEC);
  Serial_csv();
  CmdSerial.write(g_HomeDisplayMode, DEC);
  for (unsigned char i=0; i<LED_COUNT; ++i)
  {
    Serial_csv();
    CmdSerial.write( ledmanager.getAssignment(i), DEC);
  }
  Serial_nl();
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
      // If we're in home, clear in case we're switching from 4 to 2
      if (isMenuHomeState())
        lcd.clear();
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
#ifdef HEATERMETER_SERIAL
  CmdSerial.write(("HMAL"));
  for (unsigned char i=0; i<TEMP_COUNT; ++i)
  {
    ProbeAlarm &a = pid.Probes[i]->Alarms;
    Serial_csv();
    CmdSerial.write(a.getLow(), DEC);
    if (a.getLowRinging()) CmdSerial.write('L');
    Serial_csv();
    CmdSerial.write(a.getHigh(), DEC);
    if (a.getHighRinging()) CmdSerial.write('H');
  }
  Serial_nl();
#endif
}

static void reportFanParams(void)
{
  CmdSerial.write(("HMFN" CSV_DELIMITER));
  CmdSerial.write(pid.getFanMinSpeed(), DEC);
  Serial_csv();
  CmdSerial.write(pid.getFanMaxSpeed(), DEC);
  Serial_csv();
  CmdSerial.write(pid.getServoMinPos(), DEC);
  Serial_csv();
  CmdSerial.write(pid.getServoMaxPos(), DEC);
  Serial_csv();
  CmdSerial.write(pid.getOutputFlags(), DEC);
  Serial_csv();
  CmdSerial.write(pid.getFanMaxStartupSpeed(), DEC);
  Serial_csv();
  CmdSerial.write(pid.getFanActiveFloor(), DEC);
  Serial_csv();
  CmdSerial.write(pid.getServoActiveCeil(), DEC);
  Serial_nl();
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
#ifdef HEATERMETER_RFM12
  reportRfMap();  
#endif /* HEATERMETER_RFM12 */
}

typedef void (*csv_int_callback_t)(unsigned char idx, int val);

static void csvParseI(char *vals, csv_int_callback_t c)
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

static void handleCommandUrl(char *URL)
{
  unsigned char urlLen = strlen(URL);
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
    Menus.displayToast(URL+7);
  }
  else if (strncmp(URL, ("set?tp="), 7) == 0)
  {
    csvParseI(URL + 7, setTempParam);
  }
  else if (strncmp(URL, ("config"), 6) == 0)
  {
    reportConfig();
  }
  else if (strncmp(URL, ("reboot"), 5) == 0)
  {
  }
}
#endif /* defined(HEATERMETER_SERIAL) */

static void outputRfStatus(void)
{
#if defined(HEATERMETER_SERIAL) && defined(HEATERMETER_RFM12)
  rfmanager.status();
#endif /* defined(HEATERMETER_SERIAL) && defined(HEATERMETER_RFM12) */
}

#ifdef HEATERMETER_RFM12
static void rfSourceNotify(RFSource &r, unsigned char event)
{
  for (unsigned char i=0; i<TEMP_COUNT; ++i)
    if ((pid.Probes[i]->getProbeType() == PROBETYPE_RF12) &&
    ((rfMap[i] == RFSOURCEID_ANY) || (rfMap[i] == r.getId()))
    )
    {
      if (event == RFEVENT_Remove)
        pid.Probes[i]->calcTemp(0);
      else if (r.isNative())
        pid.Probes[i]->setTemperatureC(r.Value / 10.0f);
      else
      {
        unsigned int val = r.Value;
        unsigned char adcBits = rfmanager.getAdcBits();
        // If the remote is lower resolution then shift it up to our resolution
        if (adcBits < pid.getAdcBits())
          val <<= (pid.getAdcBits() - adcBits);
        pid.Probes[i]->calcTemp(val);
      }
    } /* if probe is this source */

  if (event & (RFEVENT_Add | RFEVENT_Remove))
    outputRfStatus();
}
#endif /* HEATERMETER_RFM12 */

extern int hm_AdcPins[];
static void outputAdcStatus(void)
{
#if defined(HEATERMETER_SERIAL)
  CmdSerial.write("HMAR");
  for (unsigned char i=0; i<NUM_ANALOG_INPUTS; ++i)
  {
    Serial_csv();
    CmdSerial.write(adc.analogReadRange(hm_AdcPins[i]), DEC);
  }
  Serial_nl();
#endif
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
    Menus.setState(ST_HOME_ALARM);
  }
  else if (Menus.getState() == ST_HOME_ALARM)
    // No alarms ringing, return to HOME
    Menus.setState(ST_HOME_FOOD1);
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
#ifdef HEATERMETER_RFM12
  memcpy(rfMap, config.base.rfMap, sizeof(rfMap));
#endif
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

#ifdef HEATERMETER_SERIAL
static void serial_doWork(void)
{
  unsigned char len = strlen(g_SerialBuff);
  char c = CmdSerial.read();
  while (c != 0)
  {
    // support CR, LF, or CRLF line endings
		if (c == '\n' || c == '\r')  
		{
		  if (len != 0 && g_SerialBuff[0] == '/')
			handleCommandUrl(&g_SerialBuff[1]);
		  len = 0;
		}
		else {
		  g_SerialBuff[len++] = c;
		  // if the buffer fills without getting a newline, just reset
		  if (len >= sizeof(g_SerialBuff))
			len = 0;
		}
		g_SerialBuff[len] = '\0';
		c = CmdSerial.read();
  }  /* while CmdSerial */
}
#endif  /* HEATERMETER_SERIAL */

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
    
  if ((pidCycleCount % 0x20) == 0)
    outputRfStatus();

  outputCsv();
  // We want to report the status before the alarm readout so
  // receivers can tell what the value was that caused the alarm
  checkAlarms();

  if (g_LogPidInternals)
    pid.pidStatus();

  if ((pidCycleCount % 0x04) == 1)
    outputAdcStatus();

  publishLeds();

#ifdef HEATERMETER_RFM12
  rfmanager.sendUpdate(pid.getPidOutput());
#endif
}

static void lcdDefineChars(void)
{
  for (unsigned char i=0; i<8; ++i)
    lcd.createChar(i, BIG_CHAR_PARTS + (i * 8));
}

static void ledExecutor(unsigned char led, unsigned char on)
{
  switch (led)
  {
    case 0:
      digitalWrite(PIN_WIRELESS_LED, on?1:0);
      break;
    default:
      lcd.lcd_digitalWrite(led - 1, on);
      break;
  }
}

void hmcoreSetup(void)
{
  pinModeFast(PIN_WIRELESS_LED, OUTPUT);
  blinkLed();
  
#ifdef HEATERMETER_SERIAL
  CmdSerial.begin(HEATERMETER_SERIAL);
  // don't use SerialX because we don't want any preamble
  CmdSerial.write('\n');
  reportVersion();
#endif  /* HEATERMETER_SERIAL */



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
#ifdef HEATERMETER_RFM12
  checkInitRfManager();
#endif

  Menus.setState(ST_HOME_NOPROBES);
}

void hmcoreLoop(void)
{ 
#ifdef HEATERMETER_SERIAL 
  serial_doWork();
#endif /* HEATERMETER_SERIAL */

#ifdef HEATERMETER_RFM12
  if (rfmanager.doWork())
    ledmanager.publish(LEDSTIMULUS_RfReceive, LEDACTION_OneShot);
#endif /* HEATERMETER_RFM12 */

  if (pid.doWork())
    newTempsAvail();
  Menus.doWork();
  tone_doWork();
  ledmanager.doWork();
}


int main(int argc, char **argv)
{
	hmcoreSetup();
	start_server();
	while (1)
	{
		hmcoreLoop();
		delayMicroseconds(1000);
	}
}
