// include the library code:
#include <Arduino.h>
#include <LiquidCrystalIO.h>
#include <SPI.h>

// Pin which may enable piezo speaker when HIGH, leave undefined for quiet
#define SOUND_PIN 8
// Pins used for the two tube inputs
#define TUBE1_PIN 2
#define TUBE2_PIN 3
// Use IAEA thresholds for radiation safety. WARNING: May not be able to measure that high rate, false sense of security
// #define IAEA_THRESHOLDS
// Hardware test, check for nested interrupts or code running during interrupt, normally comment this out
#define TEST_INT

// When using the I2C version, these two extra includes are always needed. Doing this reduces the memory slightly for
// users that are not using I2C.
#include <IoAbstractionWire.h>
#include <Wire.h>

// For most standard I2C backpacks one of the two helper functions below will create you a liquid crystal instance
// that's ready configured for I2C. Important Note: this method assumes a PCF8574 running at 100Khz. If otherwise
// use a custom configuration as you see in many other examples.

// If your backpack is wired RS,RW,EN then use this version
LiquidCrystalI2C_RS_EN(lcd, 0x27, false)

// If your backpack is wired EN,RW,RS then use this version instead of the above.
//LiquidCrystalI2C_EN_RS(lcd, 0x20, false)

#define LOGtime (1000)                    // Logging time in milliseconds (1 second)
#define Period (60)                       // the period of 1 minute for calculating the CPM values
#define CONVERSION (151.0*2)              // Conversion factor from counts to uSv/h; see comments
#define COINCIDENCE (20)                  // Interrupt processing time for co-incidence detection

volatile unsigned long Coincidence = 0;   // Count of coincidences
unsigned long COUNTS[Period];             // array for storing the measured amounts of impulses in consecutive 1 second periods
int Slot = 0;                             // pointer to round robin location in COUNTS

unsigned long AVGCPM = 0;                 // variable containing the floating average number of counts over a fixed moving window period
volatile unsigned long Counts1 = 0;       // variable containing the number of GM Tube events within the LOGtime
volatile unsigned long Counts2 = 0;       // tube 2

bool Starting = true;                     // Flag tracking initial sampling period
unsigned long Start;                      // Millis value when logging started; must be 32-bit

#ifdef TEST_INT
volatile int INT = 0;                     // Flag for tracking whether we're within interrupt. Shouldn't end up set, but for debugging
bool WasInt = false;                      // Debugging flag to see if we've run during interrupt handling, which would be bad.
#endif

volatile unsigned long DeadTime = 99999;  //
volatile unsigned long LastTick1 = 0;
volatile unsigned long LastTick2 = 0;     // tube 2

void setup() {
  // most backpacks have the backlight on pin 3.
  //  lcd.configureBacklightPin(3);
  //  lcd.backlight();

  // for i2c variants, this must be called first.
  Wire.begin();

  // set up the LCD's number of columns and rows, must be called.
  lcd.begin(16, 2);

  // Larger micro-mu, might be useful on some displays
  // const uint8_t plus_minus[8] = {0x04,0x04,0x1F,0x04,0x04,0x00,0x1F,0x00};
  const uint8_t plus_minus[8] = {0x00, 0x04, 0x0E, 0x04, 0x00, 0x0E, 0x00, 0x00};
  lcd.createChar(0, (uint8_t *)plus_minus);
  const uint8_t micro_mu[8] = {0x00, 0x00, 0x11, 0x11, 0x11, 0x19, 0x16, 0x10};
  lcd.createChar(1, (uint8_t *)micro_mu);
  // Sv/h is needlessly long, but with 5 bit character width we can't fit two symbols

  for (int idx = 0; idx < Period; idx++) {             // put all data in the Array COUNTS to 0 (Array positions run from 0 to Period-1);
    COUNTS[idx] = 0;                                   // positions covering number of seconds as defined in Period
  }

#ifdef SOUND_PIN
  pinMode(SOUND_PIN, OUTPUT);                          // Enable 5v on pin that could be connected to piezo
  digitalWrite(SOUND_PIN, HIGH);                       // Enable 5v on pin that could be connected to piezo
#endif

  taskManager.scheduleFixedRate(LOGtime, [] {
    unsigned long counts1;
    unsigned long counts2;
    noInterrupts();
    counts1 = Counts1;
    counts2 = Counts2;
    Counts1 = 0;
    Counts2 = 0;
    interrupts();

    // SURVEY METER: If current second indicates large change, reset to new CPM
    if (!Starting && labs(((long)counts1 + (long)counts2) * Period - (long)AVGCPM) > 1000) {
      Starting = true;
      for (int idx = 0; idx < Period; idx++) {
        COUNTS[idx] = counts1 + counts2;
      }
      Slot = 0;
    }

    // Update AVGCPM
    AVGCPM = 0;
    COUNTS[Slot] = counts1 + counts2;
    for (int idx = 0; idx < Period; idx++) {           // add all data in the Array COUNTS together
      AVGCPM += COUNTS[idx];                           // and calculate the rolling average CPM over Period seconds
    }
    Slot++;
    // This assumes we only sum over new samples, right now we don't.
    if (0 && Starting) {
      AVGCPM = AVGCPM * 60 / Slot;
    }

    float STDCPM = 0;
    // We need the average CPM for standard deviation, so run our loop again.
    for (int idx = 0; idx < Period; idx++) {
      STDCPM += sq(COUNTS[idx] - (float)AVGCPM/Period);// calculate the standard deviation of the samples
    }
    STDCPM = sqrt(STDCPM);                            // / sqrt(Period), but we're doing stddev over samples per second already

    if (Slot >= Period) {
      Starting = false;
      Slot = 0;
    }

    // https://sites.google.com/site/diygeigercounter/technical/gm-tubes-supported Fairly good explanation of conversion factors;
    // note they're technically specific to isotope AND specific tube, so you would need to have known reference sample, eg. another calibrated meter.
    // https://www.pascalchour.fr/ressources/cgm/cgm_en.html includes J305 for 60Co, though 137Cs is more likely encountered in nuclear disaster.
    // This would give us 123.147 for 60Co, 80.661 for 137Cs based on above reported SBM-20 comparison. This gives too high readings for radon background.
    // The CAJOE-1.3 original sketch has conversion factor around 151, which seems closer to SBS-20 value, but I'm leaving it as default for compatibility.
    float Sievert = (AVGCPM / CONVERSION);

    // lcd.clear(); // Causes
    lcd.setCursor(0, 0);
    // lcd.print("CPM=");                              // Matches original sketch, but isn't it kinda obvious? Save space for stddev
    lcd.print(AVGCPM);
    lcd.write((uint8_t)0);                             // The +/- symbol
    lcd.print(STDCPM);

#ifdef TEST_INT
    if (INT || WasInt) {
      WasInt = true;
      lcd.print("WI ");                                // Indicator for non-atomic interrupt problem; shouldn't happen
    }
#endif
    lcd.print("     ");                                // Clear any possible leftover on screen

    // Status is always 7 characters long so we can print over previous status, one extra for ending zero
    // http://www-ns.iaea.org/downloads/iec/health-hazard-perspec-charts2013.pdf 1 month living limits; check for contamination of ingestibles!
    char Status[8] = "Safety";
#ifdef IAEA_THRESHOLDS
    if (Sievert > 100) {
      strcpy((char*)&Status, "Danger");
    } else if (Sievert > 25) {
      strcpy((char*)&Status, "Warning");
    }
#else
    if (Sievert > 10) {
      strcpy((char*)&Status, "Danger!");
    } else if (Sievert > 0.5) {
      strcpy((char*)&Status, "Unsafe");
    }
#endif
    lcd.setCursor(16 - strlen(Status), 0);
    lcd.print(Status);

    // Second LCD line, show calculated sieverts and minimum time between ticks (max deadtime)
    lcd.setCursor(0, 1);
    lcd.print(Sievert);
    lcd.print(" \x01Sv/h");

    unsigned long coincidence;
    noInterrupts();
    coincidence = Coincidence;
    interrupts();
    lcd.print(" ");
    lcd.print(coincidence);
    lcd.print(" ");
    lcd.print((unsigned long)((3600000ULL * coincidence) / (millis() - Start)));
    lcd.print("    ");

    Serial.println(AVGCPM);                            // Serial printout for Arduino Serial Plotter or analysis
  });

  Serial.begin(9600);

  attachInterrupt(digitalPinToInterrupt(TUBE1_PIN), IMPULSE1, FALLING);
  attachInterrupt(digitalPinToInterrupt(TUBE2_PIN), IMPULSE2, FALLING);
  Start = millis();
}

void loop() {
  taskManager.runLoop();
}

void IMPULSE1()
{
#ifdef TEST_INT
  // https://microchipdeveloper.com/8avr:int Interrupts within interrupts shouldn't happen on AVR, but then again what hardware are you running?
  if (1 == INT) {
    WasInt = true;
  }
  INT = 1;
#endif

  Counts1++;
  unsigned long Micros = micros();
  // When micros rolls over, it will be maxint so won't be smaller.
  if ((Micros - LastTick1) < DeadTime) {
    DeadTime = Micros - LastTick1;
  }
  LastTick1 = Micros;
  if(LastTick1 - LastTick2 < COINCIDENCE) {
    Coincidence++;
  }

#ifdef TEST_INT
  INT = 0;
#endif
}

void IMPULSE2()
{
  Counts2++;
  unsigned long Micros = micros();
  // When micros rolls over, it will be maxint so won't be smaller.
  if ((Micros - LastTick2) < DeadTime) {
    DeadTime = Micros - LastTick2;
  }
  LastTick2 = Micros;
  if(LastTick2 - LastTick1 < COINCIDENCE) {
    Coincidence++;
  }
}
