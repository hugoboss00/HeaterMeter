// HeaterMeter Copyright 2016 Bryan Mayland <bmayland@capnbry.net>
#include "ledmanager.h"
#include "systemif.h"

#define LED_BLINK_MILLIS 500U

void LedManager::publish(unsigned char stimulus, unsigned char action)
{
  for (unsigned char i=0; i<LED_COUNT; ++i)
  {
    led_status_t &a = _leds[i];
    if ((a.stimulus & LEDSTIMULUS_MASK) == stimulus)
    {
      a.triggered = action;
      if (a.on != LEDACTION_OneShot && action != LEDACTION_OneShot)
      {
        unsigned char invert = a.stimulus >> 7;
        unsigned char invertedState = invert ^ action;

        if (invertedState != a.on)
        {
          a.on = invertedState;
          _executor(i, invertedState);
        }
      }
    } /* stimulus match */
  } /* LED_COUNT */
}

void LedManager::doWork(void)
{
  unsigned int elapsed = millis() - _blinkMillis;
  if (_hasRunOnce && (elapsed < LED_BLINK_MILLIS))
    return;
  _blinkMillis = millis();
  _hasRunOnce = true;

  ++_blinkCount;
  for (unsigned char i=0; i<LED_COUNT; ++i)
  {
    led_status_t &a = _leds[i];
    bool stateChanged = false;

    if (_blinkCount & 1)
    {
      if (a.triggered == LEDACTION_OneShot)
      {
        a.on = LEDACTION_OneShot;
        a.triggered = LEDACTION_Off;
        stateChanged = true;
      }
    }
    else
    {
      if (a.on == LEDACTION_OneShot)
      {
        a.on = LEDACTION_Off;
        stateChanged = true;
      }
    } /* !_blinkState */

    if (stateChanged)
      _executor(i, a.on);
  } /* LED_COUNT */
}

void LedManager::setAssignment(unsigned char led, unsigned char stimulus)
{
  _leds[led].stimulus = stimulus;
  if (_leds[led].on == LEDACTION_OnSteady)
    _executor(led, LEDACTION_Off);
}
