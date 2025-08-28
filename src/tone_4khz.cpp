// HeaterMeter Copyright 2016 Bryan Mayland <bmayland@capnbry.net>
/*
  BeagleBone PWM-based buzzer control
*/
#include <stdint.h>
#include <unistd.h>
#include <chrono>
#include <thread>
#ifndef PIN_SIMULATION
#include <BBBiolib.h>
#endif
#include "pwm.h"
#include "tone_4khz.h"

static Pwm buzzer;
static bool buzzer_initialized = false;
static std::thread buzzer_timer_thread;
static bool timer_active = false;

void tone4khz_init(void)
{
#ifndef PIN_SIMULATION
  if (!buzzer_initialized) {
    // Initialize buzzer PWM at 4kHz frequency
    buzzer.init(PWM_PIN2B, 4000.0f);
    buzzer_initialized = true;
  }
#endif
}

void tone4khz_end(void)
{
  timer_active = false;
  if (buzzer_timer_thread.joinable()) {
    buzzer_timer_thread.join();
  }
#ifndef PIN_SIMULATION
  if (buzzer_initialized) {
    buzzer.setValue(0); // 0% duty cycle = off
  }
#endif
}

void tone4khz_begin(unsigned char pin, unsigned char dur)
{
  // Stop existing tone
  tone4khz_end();
  
#ifndef PIN_SIMULATION
  if (!buzzer_initialized) {
    tone4khz_init();
  }
  
  // Start PWM at 50% duty cycle for audible tone
  buzzer.setValue(500000000); // 50% duty cycle in nanoseconds (1 second = 1,000,000,000 ns)
  
  // Start timer thread to stop after duration
  if (dur > 0) {
    timer_active = true;
    buzzer_timer_thread = std::thread([dur]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(dur * 10));
      if (timer_active) {
        buzzer.setValue(0); // Turn off after duration
        timer_active = false;
      }
    });
  }
#endif
}
