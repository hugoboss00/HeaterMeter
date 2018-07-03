// HeaterMeter Copyright 2016 Bryan Mayland <bmayland@capnbry.net>
// GrillPid uses TIMER1 COMPB vector, as well as modifies the waveform
// generation mode of TIMER1. Blower output pin needs to be a hardware PWM pin.
// Fan output is 489Hz phase-correct PWM
// Servo output is 50Hz pulse duration
#include <math.h>
#include <BBBiolib.h>
#include <string.h>
#include <stdbool.h>
#include "atomic.h"
#include "digitalWriteFast.h"
#include "systemif.h"
#include "strings.h"
#include "serial.h"
#include "grillpid.h"
#include "adc.h"
#include "pwm.h"


extern GrillPid pid;
extern Serial CmdSerial;

Adc adc;
static Pwm servo;
int hm_AdcPins[] = {PIN_PIT,PIN_FOOD1,PIN_FOOD2,PIN_AMB,APIN_FFEEDBACK,PIN_BUTTONS};



// For this calculation to work, ccpm()/8 must return a round number
#define uSecToTicks(x) ((unsigned int)(clockCyclesPerMicrosecond() / 8) * x)

// LERP percentage o into the unsigned range [A,B]. B - A must be < 655
#define mappct(o, a, b)  (((b - a) * (unsigned int)o / 100) + a)

#define DIFFMAX(x,y,d) ((x - y + d) <= (d*2U))


static void calcExpMovingAverage(const float smooth, float *currAverage, float newValue)
{
  newValue = newValue - *currAverage;
  *currAverage = *currAverage + (smooth * newValue);
}

void ProbeAlarm::updateStatus(int value)
{
  // Low: Arming point >= Thresh + 1.0f, Trigger point < Thresh
  // A low alarm set for 100 enables at 101.0 and goes off at 99.9999...
  if (getLowEnabled())
  {
    if (value >= (getLow() + 1))
      Armed[ALARM_IDX_LOW] = true;
    else if (value < getLow() && Armed[ALARM_IDX_LOW])
      Ringing[ALARM_IDX_LOW] = true;
  }

  // High: Arming point < Thresh - 1.0f, Trigger point >= Thresh
  // A high alarm set for 100 enables at 98.9999... and goes off at 100.0
  if (getHighEnabled())
  {
    if (value < (getHigh() - 1))
      Armed[ALARM_IDX_HIGH] = true;
    else if (value >= getHigh() && Armed[ALARM_IDX_HIGH])
      Ringing[ALARM_IDX_HIGH] = true;
  }

  if (pid.isLidOpen())
    Ringing[ALARM_IDX_LOW] = Ringing[ALARM_IDX_HIGH] = false;
}

void ProbeAlarm::setHigh(int value)
{
  setThreshold(ALARM_IDX_HIGH, value);
}

void ProbeAlarm::setLow(int value)
{
  setThreshold(ALARM_IDX_LOW, value);
}

void ProbeAlarm::setThreshold(unsigned char idx, int value)
{
  Armed[idx] = false;
  Ringing[idx] = false;
  /* 0 just means silence */
  if (value == 0)
    return;
  Thresholds[idx] = value;
}

TempProbe::TempProbe(const unsigned char pin) :
  _pin(pin), _tempStatus(TSTATUS_NONE)
{
}

void TempProbe::loadConfig(struct __eeprom_probe *config)
{
  _probeType = config->probeType;
  Offset = config->tempOffset;
  memcpy(Steinhart, config->steinhart, sizeof(Steinhart));
  memcpy(_name, config->name, PROBE_NAME_SIZE);
  Alarms.setLow(config->alarmLow);
  Alarms.setHigh(config->alarmHigh);
}

void TempProbe::setProbeType(unsigned char probeType)
{
  _probeType = probeType;
  _tempStatus = TSTATUS_NONE;
  _hasTempAvg = false;
}

void TempProbe::calcTemp(unsigned int adcval)
{
  const float ADCmax = 1023 * pow(2, TEMP_OVERSAMPLE_BITS);

  // Units 'A' = ADC value
  if (pid.getUnits() == 'A')
  {
    Temperature = adcval;
    _tempStatus = TSTATUS_OK;
    return;
  }

  if (_probeType == PROBETYPE_TC_ANALOG)
  {
    float mvScale = Steinhart[3];
    // Commented out because there's no "divide by zero" exception so
    // just allow undefined results to save prog space
    //if (mvScale == 0.0f)
    //  mvScale = 1.0f;
    // If scale is <100 it is assumed to be mV/C with a 3.3V reference
    if (mvScale < 100.0f)
      mvScale = 3300.0f / mvScale;
    setTemperatureC(adcval / ADCmax * mvScale);
    return;
  }

  // Ignore probes within 1 LSB of max. TC don't need this as their min/max
  // values are rejected as outside limits in setTemperatureC()
  if (adcval > 1022 * pow(2, TEMP_OVERSAMPLE_BITS) || adcval == 0)
  {
    _tempStatus = TSTATUS_NONE;
    return;
  }

  /* if PROBETYPE_INTERNAL */
  float R, T;
  R = Steinhart[3] / ((ADCmax / (float)adcval) - 1.0f);

  // Units 'R' = resistance, unless this is the pit probe (which should spit out Celsius)
  if (pid.getUnits() == 'R' && this != pid.Probes[TEMP_CTRL])
  {
    Temperature = R;
    _tempStatus = TSTATUS_OK;
    return;
  };

  // Compute degrees K
  R = log(R);
  T = 1.0f / ((Steinhart[2] * R * R + Steinhart[1]) * R + Steinhart[0]);

  setTemperatureC(T - 273.15f);
}

void TempProbe::processPeriod(void)
{
  // Called once per measurement period after temperature has been calculated
  if (hasTemperature())
  {
    if (!_hasTempAvg)
    {
      TemperatureAvg = Temperature;
      _hasTempAvg = true;
    }
    else
      calcExpMovingAverage(TEMPPROBE_AVG_SMOOTH, &TemperatureAvg, Temperature);

    if (!pid.isDisabled())
    {
      Alarms.updateStatus(Temperature);
      return;
    }
  }

  // !hasTemperature() || pid.getDisabled()
  Alarms.silenceAll();
}

void TempProbe::setTemperatureC(float T)
{
  // Sanity - anything less than -20C (-4F) or greater than 500C (932F) is rejected
  if (T <= -20.0f)
    _tempStatus = TSTATUS_LOW;
  else if (T >= 500.0f)
    _tempStatus = TSTATUS_HIGH;
  else
  {
    if (pid.getUnits() == 'F')
      Temperature = (T * (9.0f / 5.0f)) + 32.0f;
    else
      Temperature = T;
    Temperature += Offset;
    _tempStatus = TSTATUS_OK;
  }
}

void TempProbe::status(void) const
{
  if (hasTemperature())
    CmdSerial.write(Temperature);
  else
    CmdSerial.write('U');
  Serial_csv();
}

void GrillPid::init(void)
{
  iolib_init();

#if defined(GRILLPID_SERVO_ENABLED)
  servo.init(BBBIO_PWMSS0, FREQ_SERVO);
#endif
  // Initialize ADC 
  adc.init(hm_AdcPins, NUM_ANALOG_INPUTS);

  updateControlProbe();
}

void __attribute__ ((noinline)) GrillPid::updateControlProbe(void)
{
  // Set control to the first non-Disabled probe. If all probes are disabled, return TEMP_PIT
  Probes[TEMP_CTRL] = Probes[TEMP_PIT];
  for (unsigned char i=0; i<TEMP_COUNT; ++i)
    if (Probes[i]->getProbeType() != PROBETYPE_DISABLED)
    {
      Probes[TEMP_CTRL] = Probes[i];
      break;
    }
}

void GrillPid::setProbeType(unsigned char idx, unsigned char probeType)
{
  Probes[idx]->setProbeType(probeType);
  updateControlProbe();
}

void GrillPid::setOutputFlags(unsigned char value)
{
  _outputFlags = value;

  unsigned char newTop;
  // 50Hz = 192.31 samples
  if (bit_is_set(value, PIDFLAG_LINECANCEL_50))
     newTop = 192;
  // 60Hz = 160.25 samples
  else if (bit_is_set(value, PIDFLAG_LINECANCEL_60))
    newTop = 160;
  else
    newTop = 255;

  adc.setTop(newTop);
}

void GrillPid::servoRangeChanged(void)
{
#if defined(SERVO_MIN_THRESH)
  _servoHoldoff = SERVO_MAX_HOLDOFF;
#endif
}

void GrillPid::setServoMinPos(unsigned char value)
{
  _servoMinPos = value;
  servoRangeChanged();
}

void GrillPid::setServoMaxPos(unsigned char value)
{
  _servoMaxPos = value;
  servoRangeChanged();
}

unsigned int GrillPid::countOfType(unsigned char probeType) const
{
  unsigned char retVal = 0;
  for (unsigned char i=0; i<TEMP_COUNT; ++i)
    if (Probes[i]->getProbeType() == probeType)
      ++retVal;
  return retVal;  
}

/* Calucluate the desired output percentage using the proportional–integral-derivative (PID) controller algorithm */
inline void GrillPid::calcPidOutput(void)
{
  unsigned char lastOutput = _pidOutput;
  _pidOutput = 0;

  // If the pit probe is registering 0 degrees, don't jack the fan up to MAX
  if (!Probes[TEMP_CTRL]->hasTemperature())
    return;

  // If we're in lid open mode, fan should be off
  if (isLidOpen())
    return;

  float currentTemp = Probes[TEMP_CTRL]->Temperature;
  float error;
  error = _setPoint - currentTemp;

  // PPPPP = fan speed percent per degree of error
  _pidCurrent[PIDP] = Pid[PIDP] * error;

  // IIIII = fan speed percent per degree of accumulated error
  // anti-windup: Make sure we only adjust the I term while inside the proportional control range
  if ((error < 0 && lastOutput > 0) || (error > 0 && lastOutput < getPidIMax()))
  {
    _pidCurrent[PIDI] += Pid[PIDI] * error;
    // I term can never be negative, because if curr = set, then P and D are 0, so I must be output
    if (_pidCurrent[PIDI] < 0.0f) _pidCurrent[PIDI] = 0.0f;
  }

  // DDDDD = fan speed percent per degree of change over TEMPPROBE_AVG_SMOOTH period
  _pidCurrent[PIDD] = Pid[PIDD] * (Probes[TEMP_CTRL]->TemperatureAvg - currentTemp);
  // BBBBB = fan speed percent (always 0)
  //_pidCurrent[PIDB] = Pid[PIDB];

  int control = _pidCurrent[PIDP] + _pidCurrent[PIDI] + _pidCurrent[PIDD];
  _pidOutput = constrain(control, 0, 100);
}

void GrillPid::adjustFeedbackVoltage(void)
{
  if (_lastBlowerOutput != 0 && bit_is_set(_outputFlags, PIDFLAG_FAN_FEEDVOLT))
  {
    // _lastBlowerOutput is the voltage we want on the feedback pin
    // adjust _feedvoltLastOutput until the ffeedback == _lastBlowerOutput
    unsigned char ffeedback = adc.analogReadOver(APIN_FFEEDBACK, 8);
    int error = ((int)_lastBlowerOutput - (int)ffeedback);
    int newOutput = (int)_feedvoltLastOutput + (error / 2);
    _feedvoltLastOutput = constrain(newOutput, 1, 255);

#if defined(GRILLPID_FEEDVOLT_DEBUG)
    CmdSerial.write("HMLG,");
    CmdSerial.write("SMPS: ffeed="); CmdSerial.write(ffeedback, DEC);
    CmdSerial.write(" out="); CmdSerial.write(newOutput, DEC);
    CmdSerial.write(" fdesired="); CmdSerial.write(_lastBlowerOutput, DEC);
    Serial_nl();
#endif
  }
  else
    _feedvoltLastOutput = _lastBlowerOutput;

  analogWrite(PIN_BLOWER, _feedvoltLastOutput);
}

inline unsigned char FeedvoltToAdc(float v)
{
  // Calculates what an 8 bit ADC value would be for the given voltage
  const unsigned long R1 = 22000;
  const unsigned long R2 = 68000;
  // Scale the voltage by the voltage divder
  // v * R1 / (R1 + R2) = pV
  // Scale to ADC assuming 3.3V reference
  // (pV / 3.3) * 256 = ADC
  return ((v * R1 * 256) / ((R1 + R2) * 3.3f));
}

inline void GrillPid::commitFanOutput(void)
{
  /* Long PWM period is 10 sec */
  const unsigned int LONG_PWM_PERIOD = 10000;
  const unsigned int PERIOD_SCALE = (LONG_PWM_PERIOD / TEMP_MEASURE_PERIOD);

  if (_pidOutput < _fanActiveFloor)
    _fanSpeed = 0;
  else
  {
    // _fanActiveFloor should be constrained to 0-99 to prevent a divide by 0
    unsigned char range = 100 - _fanActiveFloor;
    unsigned char max = getFanCurrentMaxSpeed();
    _fanSpeed = (unsigned int)(_pidOutput - _fanActiveFloor) * max / range;
  }

  /* For anything above _minFanSpeed, do a nomal PWM write.
     For below _minFanSpeed we use a "long pulse PWM", where
     the pulse is 10 seconds in length.  For each percent we are
     emulating, run the fan for one interval. */
  if (_fanSpeed >= _fanMinSpeed)
    _longPwmTmr = 0;
  else
  {
    // Simple PWM, ON for first [FanSpeed] intervals then OFF
    // for the remainder of the period
    if (((PERIOD_SCALE * _fanSpeed / _fanMinSpeed) > _longPwmTmr))
      _fanSpeed = _fanMinSpeed;
    else
      _fanSpeed = 0;

    if (++_longPwmTmr > (PERIOD_SCALE - 1))
      _longPwmTmr = 0;
  }  /* long PWM */

  if (bit_is_set(_outputFlags, PIDFLAG_INVERT_FAN))
    _fanSpeed = _fanMaxSpeed - _fanSpeed;

  // 0 is always 0
  if (_fanSpeed == 0)
    _lastBlowerOutput = 0;
  else
  {
    bool needBoost = _lastBlowerOutput == 0;
    if (bit_is_set(_outputFlags, PIDFLAG_FAN_FEEDVOLT))
      _lastBlowerOutput = mappct(_fanSpeed, FeedvoltToAdc(5.0f), FeedvoltToAdc(12.1f));
    else
      _lastBlowerOutput = mappct(_fanSpeed, 0, 255);
    // If going from 0% to non-0%, turn the blower fully on for one period
    // to get it moving (boost mode)
    if (needBoost)
    {
      analogWrite(PIN_BLOWER, 255);
      // give the FFEEDBACK control a high starting point so when it reads
      // for the first time and sees full voltage it doesn't turn off
      _feedvoltLastOutput = 128;
      return;
    }
  }
  adjustFeedbackVoltage();
}
#if 0
unsigned int GrillPid::getServoStepNext(unsigned int curr)
{
#if defined(GRILLPID_SERVO_ENABLED)
  const unsigned int SERVO_STEP = uSecToTicks(15U);
  const unsigned int SERVO_HOLD_SECS = 2U;

  // Hold the servo for SERVO_HOLD_SECS seconds then turn off on the next period
  // when the output is off (allow the damper to close before turning off)
  if (_servoStepTicks >= (SERVO_HOLD_SECS * 1000000UL / SERVO_REFRESH)
    && _pidMode == PIDMODE_OFF)
    return 0;

  // If at or close to target, snap to target
  // curr is 0 on first interrupt
  if (DIFFMAX(_servoTarget, curr, SERVO_STEP) || curr == 0)
  {
    ++_servoStepTicks;
    return _servoTarget;
  }
  // Else slew toward target
  else if (_servoTarget > curr)
    return curr + SERVO_STEP;
  else
    return curr - SERVO_STEP;
#else
	return 0;
#endif
}
#endif
inline void GrillPid::commitServoOutput(void)
{
#if defined(GRILLPID_SERVO_ENABLED)
  unsigned char output;
  // Servo is open 0% at 0 PID output and 100% at _servoActiveCeil PID output
  if (_pidOutput >= _servoActiveCeil)
    output = 100;
  else
    output = (unsigned int)_pidOutput * 100U / _servoActiveCeil;

  if (bit_is_set(_outputFlags, PIDFLAG_INVERT_SERVO))
    output = 100 - output;

  // Get the output speed in 10x usec by LERPing between min and max
  output = mappct(output, _servoMinPos, _servoMaxPos);
#if 0
  unsigned int targetTicks = uSecToTicks(10U * output);
#if defined(SERVO_MIN_THRESH)
  if (_servoHoldoff < 0xff)
    ++_servoHoldoff;
  // never pulse the servo if change isn't needed
  if (_servoTarget == targetTicks)
    return;

  // and only trigger the servo if a large movement is needed or holdoff expired
  bool isBigMove = !DIFFMAX(_servoTarget, targetTicks, uSecToTicks(SERVO_MIN_THRESH));
  if (isBigMove || _servoHoldoff > SERVO_MAX_HOLDOFF)
#endif
  {
    ATOMIC_BLOCK(ATOMIC_FORCEON)
    {
      _servoStepTicks = 0;
      _servoTarget = targetTicks;
    }
    _servoHoldoff = 0;
  }
#endif
#endif
}

inline void GrillPid::commitPidOutput(void)
{
  calcExpMovingAverage(PIDOUTPUT_AVG_SMOOTH, &PidOutputAvg, _pidOutput);
  commitFanOutput();
  commitServoOutput();
}

bool GrillPid::isAnyFoodProbeActive(void) const
{
  unsigned char i;
  for (i=TEMP_FOOD1; i<TEMP_COUNT; i++)
    if (Probes[i]->hasTemperature())
      return true;
      
  return false;
}
  
void GrillPid::resetLidOpenResumeCountdown(void)
{
  setPidMode(PIDMODE_RECOVERY);
  LidOpenResumeCountdown = _lidOpenDuration;
}

void GrillPid::setSetPoint(int value)
{
  setPidMode(PIDMODE_STARTUP);
  _setPoint = value;
}

void GrillPid::setPidOutput(int value)
{
  setPidMode(PIDMODE_MANUAL);
  _pidOutput = constrain(value, 0, 100);
}

void GrillPid::setPidMode(unsigned char mode)
{
  _pidMode = mode;
  LidOpenResumeCountdown = 0;
  _pidOutput = 0;
}

void GrillPid::setPidConstant(unsigned char idx, float value)
{
  Pid[idx] = value;
  if (idx == PIDI && value == 0)
    _pidCurrent[PIDI] = 0;
}

void GrillPid::setLidOpenDuration(unsigned int value)
{
  _lidOpenDuration = (value > LIDOPEN_MIN_AUTORESUME) ? value : LIDOPEN_MIN_AUTORESUME;
}

void GrillPid::status(void) const
{
#if defined(GRILLPID_SERIAL_ENABLED)
  if (isDisabled())
    CmdSerial.write('U');
  else if (isManualOutputMode())
    CmdSerial.write('-');
  else
    CmdSerial.write(getSetPoint(), DEC);
  Serial_csv();

  // Always output the control probe in the first slot, usually TEMP_PIT
  Probes[TEMP_CTRL]->status();
  // The rest of the probes go in order, and one may be a duplicate of TEMP_CTRL
  for (unsigned char i = TEMP_FOOD1; i<TEMP_COUNT; ++i)
    Probes[i]->status();

  CmdSerial.write(getPidOutput(), DEC);
  Serial_csv();
  CmdSerial.write((int)PidOutputAvg, DEC);
  Serial_csv();
  CmdSerial.write(LidOpenResumeCountdown, DEC);
  Serial_csv();
  CmdSerial.write(getFanSpeed(), DEC);
#endif
}


bool GrillPid::doWork(void)
{
  unsigned int elapsed = millis() - _lastWorkMillis;
  if (elapsed < (TEMP_MEASURE_PERIOD / TEMP_OUTADJUST_CNT))
    return false;
  _lastWorkMillis = millis();

  if (_periodCounter < (TEMP_OUTADJUST_CNT-1))
  {
    ++_periodCounter;
    adjustFeedbackVoltage();
    return false;
  }
  _periodCounter = 0;

#if defined(GRILLPID_CALC_TEMP) 
  for (unsigned char i=0; i<TEMP_COUNT; i++)
  {
    if (Probes[i]->getProbeType() == PROBETYPE_INTERNAL ||
        Probes[i]->getProbeType() == PROBETYPE_TC_ANALOG)
      Probes[i]->calcTemp(adc.analogReadOver(Probes[i]->getPin(), 10+TEMP_OVERSAMPLE_BITS));
    Probes[i]->processPeriod();
  }

  if (_pidMode <= PIDMODE_AUTO_LAST)
  {
    // Always calculate the output
    // calcPidOutput() will bail if it isn't supposed to be in control
    calcPidOutput();
    
    int tempDiff = _setPoint - (int)Probes[TEMP_CTRL]->Temperature;
    if ((tempDiff <= 0) &&
      (_lidOpenDuration - LidOpenResumeCountdown > LIDOPEN_MIN_AUTORESUME))
    {
      // When we first achieve temperature, reduce any I sum we accumulated during startup
      // If we actually neded that sum to achieve temperature we'll rebuild it, and it
      // prevents bouncing around above the temperature when you first start up
      if (_pidMode == PIDMODE_STARTUP)
      {
        _pidCurrent[PIDI] *= 0.50f;
      }
      _pidMode = PIDMODE_NORMAL;
      LidOpenResumeCountdown = 0;
    }
    else if (LidOpenResumeCountdown != 0)
    {
      LidOpenResumeCountdown = LidOpenResumeCountdown - (TEMP_MEASURE_PERIOD / 1000);
    }
    // If the pit temperature has been reached
    // and if the pit temperature is [lidOpenOffset]% less that the setpoint
    // and if the fan has been running less than 90% (more than 90% would indicate probable out of fuel)
    // Note that the code assumes we're not currently counting down
    else if (isPitTempReached() && 
      ((tempDiff*100/_setPoint) >= (int)LidOpenOffset) &&
      ((int)PidOutputAvg < 90))
    {
      resetLidOpenResumeCountdown();
    }
  }   /* if !manualFanMode */
#endif

  commitPidOutput();
  adc.adcDump();
  return true;
}

void GrillPid::pidStatus(void) const
{
#if defined(GRILLPID_SERIAL_ENABLED)
  TempProbe const* const pit = Probes[TEMP_CTRL];
  if (pit->hasTemperature())
  {
    CmdSerial.write("HMPS" CSV_DELIMITER);
    for (unsigned char i=PIDB; i<=PIDD; ++i)
    {
      CmdSerial.write(_pidCurrent[i], DEC);
      Serial_csv();
    }

    CmdSerial.write(pit->Temperature - pit->TemperatureAvg, DEC);
    Serial_nl();
  }
#endif
}

void GrillPid::setUnits(char units)
{
  switch (units)
  {
    case 'A':
    case 'C':
    case 'F':
    case 'R':
      _units = units;
      break;
    case 'O': // Off
      setPidMode(PIDMODE_OFF);
      break;
  }
}

float GrillPid::getPidConstant(unsigned char index)
{
	float retval = 0.0;
	if (index < 4)
	{
		retval = Pid[index];
	}
	return retval;
}

void GrillPid::addProbeValues(int i, ptree &pt)
{
	ptree probe,alarm;
	if (i >= TEMP_COUNT)
		return;
	
	const char *ring = "null";
	if (Probes[i]->Alarms.getLowRinging() || Probes[i]->Alarms.getHighRinging())
	{
		ring = "H";
		if (Probes[i]->Alarms.getLowRinging())
			ring = "L";
	}
	
	
	int temp = Probes[i]->Temperature;
	probe.put("n", Probes[i]->getName());
	probe.put("c", temp);
	probe.put("dph", "1.3");
	alarm.put("l",Probes[i]->Alarms.getLow());
	alarm.put("h",Probes[i]->Alarms.getHigh());
	alarm.put("r",ring);

	probe.add_child("a",alarm);
	pt.push_back(std::make_pair("", probe));
	
}

void GrillPid::addProbeConfig(int i, ptree &pt)
{
	char strbuf[80];
	if (i >= TEMP_COUNT)
		return;
	
	const char *ring = "null";
	if (Probes[i]->Alarms.getLowRinging() || Probes[i]->Alarms.getHighRinging())
	{
		ring = "H";
		if (Probes[i]->Alarms.getLowRinging())
			ring = "L";
	}
	
	
	int temp = Probes[i]->Temperature;
	sprintf(strbuf, "pn%d", i );
	pt.put(strbuf, Probes[i]->getName());
	sprintf(strbuf, "pcurr%d", i );
	pt.put(strbuf, temp);
	sprintf(strbuf, "pt%d", i );
	pt.put(strbuf,  Probes[i]->getProbeType());
	sprintf(strbuf, "pcp%d", i );
	pt.put(strbuf, 0);
	sprintf(strbuf, "pca%d", i );
	pt.put(strbuf, Probes[i]->Steinhart[0]);
	sprintf(strbuf, "pcb%d", i );
	pt.put(strbuf, Probes[i]->Steinhart[1]);
	sprintf(strbuf, "pcc%d", i );
	pt.put(strbuf, Probes[i]->Steinhart[2]);
	sprintf(strbuf, "pcr%d", i );
	pt.put(strbuf, 0);
	sprintf(strbuf, "po%d", i );
	pt.put(strbuf, (int)Probes[i]->Offset);
	sprintf(strbuf, "prfn%d", i );
	pt.put(strbuf, 0);
	sprintf(strbuf, "prfa%d", i );
	pt.put(strbuf, 0);


}


// write history entry
// time, set, pit, food1, food2, food3, fan, servo
void GrillPid::writeHistory(void)
{
	tHISTORY_ENTRY entry;
	unsigned int elapsed = millis() - _lastHistoryMillis;
	if (elapsed < HISTORY_PERIOD)
		return;
	_lastHistoryMillis = millis();

	unsigned char fanspeed = pid.getFanSpeed();
	if (fanspeed < pid.getFanMinSpeed())
		fanspeed = pid.getFanMinSpeed();
	if (fanspeed > pid.getFanMaxSpeed())
		fanspeed = pid.getFanMaxSpeed();
	
	unsigned char servo_output;
	// Servo is open 0% at 0 PID output and 100% at _servoActiveCeil PID output
	if (_pidOutput >= _servoActiveCeil)
		servo_output = 100;
	else
		servo_output = (unsigned int)_pidOutput * 100U / _servoActiveCeil;
	
	entry.time = time(NULL);
	entry.set  = pid.getSetPoint();
	entry.pit  = Probes[TEMP_PIT]->Temperature;
	entry.food1 = Probes[TEMP_FOOD1]->Temperature;
	entry.food2 = Probes[TEMP_FOOD2]->Temperature;
	entry.food3 = Probes[TEMP_AMB]->Temperature;
	entry.fan   = fanspeed;
	entry.servo = servo_output;
	_history_array.push_back(entry);
}

void GrillPid::getHistoryCsv(stringstream &csv)
{
	for(std::vector<tHISTORY_ENTRY>::iterator it = _history_array.begin(); it != _history_array.end(); ++it)
	{
		csv << it->time << "," << (int)it->set << "," << (int)it->pit << "," << (int)it->food1 << "," << (int)it->food2 << "," << (int)it->food3 << "," << (int)it->fan << "," << (int)it->servo << endl;
	}
}

