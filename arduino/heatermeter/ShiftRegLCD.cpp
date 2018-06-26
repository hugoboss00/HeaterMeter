// ShiftRegLCD  - Shiftregister-based LCD library for Arduino
//
// Connects an LCD using 2 or 3 pins from the Arduino, via an 8-bit ShiftRegister (SR from now on).
//
// Acknowledgements:
//
// Based very much on the "official" LiquidCrystal library for the Arduino:
//     ttp://arduino.cc/en/Reference/Libraries
// and also the improved version (with examples CustomChar1 and SerialDisplay) from LadyAda:
//     http://web.alfredstate.edu/weimandn/arduino/LiquidCrystal_library/LiquidCrystal_index.html
// and also inspired by this schematics from an unknown author (thanks to mircho on the arduino playground forum!):
//     http://www.scienceprog.com/interfacing-lcd-to-atmega-using-two-wires/
//
// Modified to work serially with the shiftOut() function, an 8-bit shiftregister (SR) and an LCD in 4-bit mode.
//
// Shiftregister connection description (NEW as of 2007.07.27)
//
// Bit  #0 - N/C - not connected, used to hold a zero
// Bits #1 - N/C
// Bit  #2 - connects to RS (Register Select) on the LCD
// Bits #3 - #6 from SR connects to LCD data inputs D4 - D7.
// Bit  #7 - is used to enabling the enable-puls (via a diode-resistor AND "gate")
//
// 2 or 3 Pins required from the Arduino for Data, Clock, and Enable (optional). If not using Enable,
// the Data pin is used for the enable signal by defining the same pin for Enable as for Data.
// Data and Clock outputs/pins goes to the shiftregister.
// LCD RW-pin hardwired to LOW (only writing to LCD). Busy Flag (BF, data bit D7) is not read.
//
// Any shift register should do. I used 74LS164, for the reason that's what I had at hand.
//
//       Project homepage: http://code.google.com/p/arduinoshiftreglcd/
//
// History
// 2009.05.23  raron, but; based mostly (as in almost verbatim) on the "official" LiquidCrystal library.
// 2009.07.23  raron (yes exactly 2 months later). Incorporated some proper initialization routines
//               inspired (lets say copy-paste-tweaked) from LiquidCrystal library improvements from LadyAda
//               Also a little re-read of the datasheet for the HD44780 LCD controller.
// 2009.07.25  raron - Fixed comments. I really messed up the comments before posting this, so I had to fix it.
//               Also renamed a function, but no improvements or functional changes.
// 2009.07.27  Thanks to an excellent suggestion from mircho at the Arduiono playgrond forum,
//               the number of wires now required is only two!
// 2009.07.28  Mircho / raron - a new modification to the schematics, and a more streamlined interface
// 2009.07.30  raron - minor corrections to the comments. Fixed keyword highlights. Fixed timing to datasheet safe.

#include "ShiftRegLCD.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "systemif.h"


/*
use https://github.com/hsm5xw/Linux-Device-Driver-for-Character-LCD-Kernel-Level/ as driver
*/


void ShiftRegLCDBase::init(uint8_t lines, uint8_t font)
{
}

// ********** high level commands, for the user! **********
void ShiftRegLCDBase::clear()
{
}

void ShiftRegLCDBase::home()
{
}

void ShiftRegLCDBase::setCursor(uint8_t col, uint8_t row)
{
}

// Turn the display on/off (quickly)
void ShiftRegLCDBase::noDisplay() {
}
void ShiftRegLCDBase::display() {
}

// Turns the underline cursor on/off
void ShiftRegLCDBase::noCursor() {
}
void ShiftRegLCDBase::cursor() {
}

// Turn on and off the blinking cursor
void ShiftRegLCDBase::noBlink() {
}
void ShiftRegLCDBase::blink() {
}

// These commands scroll the display without changing the RAM
void ShiftRegLCDBase::scrollDisplayLeft(void) {
}
void ShiftRegLCDBase::scrollDisplayRight(void) {
}

// This is for text that flows Left to Right
void ShiftRegLCDBase::shiftLeft(void) {
}

// This is for text that flows Right to Left
void ShiftRegLCDBase::shiftRight(void) {
}

// This will 'right justify' text from the cursor
void ShiftRegLCDBase::shiftIncrement(void) {
}

// This will 'left justify' text from the cursor
void ShiftRegLCDBase::shiftDecrement(void) {
}

// Allows us to fill the first 8 CGRAM locations with custom characters
void ShiftRegLCDBase::createChar(uint8_t location, const char charmap[]) {
#if 0
  location &= 0x7; // we only have 8 locations 0-7
  command(LCD_SETCGRAMADDR | location << 3);
  write(charmap, 8);
  command(LCD_SETDDRAMADDR); // Reset display to display text (from pos. 0)
#endif
  }


void ShiftRegLCDBase::digitalWrite(uint8_t pin, uint8_t val)
{
}


void ShiftRegLCDBase::write(const char *p, uint8_t len)
{
}

void ShiftRegLCDNative::print(const char *str)
{
}

void ShiftRegLCDNative::print(const char c)
{
}

void ShiftRegLCDNative::ctor(uint8_t srdata, uint8_t srclock, uint8_t enable, uint8_t lines, uint8_t font)
{
  init(lines, font);
};

void ShiftRegLCDNative::send(uint8_t value, uint8_t mode) const
{
}

void ShiftRegLCDNative::send4bits(uint8_t value) const
{
}

void ShiftRegLCDNative::updateAuxPins(void) const
{
}

