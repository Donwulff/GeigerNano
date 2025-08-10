// include the library code:
#include <Arduino.h>
#include <LiquidCrystalIO.h>
#include <SPI.h>
#include <TaskManagerIO.h>

// Pin which may enable piezo speaker when HIGH, leave undefined for quiet
// Optional piezo speaker; comment out to disable audible clicks
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
LiquidCrystalI2C_RS_EN(lcd, 0x27, false);

// If your backpack is wired EN,RW,RS then use this version instead of the above.
//LiquidCrystalI2C_EN_RS(lcd, 0x20, false)

// Timing constants
constexpr unsigned long LOG_INTERVAL_MS = 1000UL;  // Logging interval in milliseconds (1 second)
constexpr unsigned int WINDOW_SECONDS = 60;        // Duration of the sliding window used for CPM calculations
constexpr float COUNTS_TO_SIEVERT = 151.0f * 2;    // Conversion factor from counts to uSv/h; see comments
constexpr unsigned long COINCIDENCE_WINDOW_MICROS = 20; // Interrupt processing time for coincidence detection

// Measurement tracking
volatile unsigned long coincidenceCount = 0;   // Count of coincidences
unsigned long countsPerSecond[WINDOW_SECONDS]; // Array of counts for each second in the sliding window
int currentSlot = 0;                           // Pointer to round robin location in countsPerSecond

unsigned long averageCpm = 0;                  // Counts per minute over the sliding window
volatile unsigned long countsTube1 = 0;        // Number of GM Tube events within the LOG interval for tube 1
volatile unsigned long countsTube2 = 0;        // Tube 2 counts

unsigned long windowSumCounts = 0;             // Running sum of samples in window
unsigned long windowSumSquares = 0;            // Running sum of squared samples
unsigned int sampleCount = 0;                  // Number of valid samples in window
unsigned long loggingStartTime;                // Millis value when logging started; must be 32-bit

#ifdef TEST_INT
volatile bool inInterrupt = false;        // Flag for tracking whether we're within interrupt.
volatile bool wasInInterrupt = false;     // Debugging flag to see if we've run during interrupt handling.
#endif

// Track the shortest interval between counts. Initialise to the maximum so
// the first measurement is always recorded.
volatile unsigned long shortestPulseInterval = 0xFFFFFFFFUL; // Shortest observed time between pulses
volatile unsigned long lastTickTube1 = 0;
volatile unsigned long lastTickTube2 = 0;     // Tube 2 last pulse time

// Handle a pulse by updating counts, dead time and coincidence detection
static inline void processImpulse(volatile unsigned long& pulseCount,
                                  volatile unsigned long& lastPulseTime,
                                  const volatile unsigned long& otherLastPulseTime) {
  pulseCount++;
  unsigned long currentTime = micros();
  unsigned long interval = currentTime - lastPulseTime;
  if (interval < shortestPulseInterval) {
    shortestPulseInterval = interval;
  }
  lastPulseTime = currentTime;
  if (currentTime - otherLastPulseTime < COINCIDENCE_WINDOW_MICROS) {
    coincidenceCount++;
  }
}

// Forward declaration for the periodic statistics and display update
void logAndDisplay();

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

  for (int idx = 0; idx < WINDOW_SECONDS; idx++) {             // Reset the sliding window
    countsPerSecond[idx] = 0;                                   // Positions covering number of seconds as defined in WINDOW_SECONDS
  }
  windowSumCounts = 0;
  windowSumSquares = 0;
  sampleCount = 0;

#ifdef SOUND_PIN
  pinMode(SOUND_PIN, OUTPUT);                          // Enable 5v on pin that could be connected to piezo
  digitalWrite(SOUND_PIN, HIGH);                       // Enable 5v on pin that could be connected to piezo
#endif

  taskManager.scheduleFixedRate(LOG_INTERVAL_MS, logAndDisplay);

  Serial.begin(9600);

  attachInterrupt(digitalPinToInterrupt(TUBE1_PIN), handleTube1Impulse, FALLING);
  attachInterrupt(digitalPinToInterrupt(TUBE2_PIN), handleTube2Impulse, FALLING);
  loggingStartTime = millis();
}

// Periodic task to update statistics and refresh the display
void logAndDisplay() {
  unsigned long counts1;
  unsigned long counts2;
  noInterrupts();
  counts1 = countsTube1;
  counts2 = countsTube2;
  countsTube1 = 0;
  countsTube2 = 0;
  interrupts();

  unsigned long newCount = counts1 + counts2;

  // SURVEY METER: If current second indicates large change, reset to new CPM
  if (sampleCount >= WINDOW_SECONDS && labs((long)newCount * WINDOW_SECONDS - (long)averageCpm) > 1000) {
    for (int idx = 0; idx < WINDOW_SECONDS; idx++) {
      countsPerSecond[idx] = newCount;
    }
    currentSlot = 0;
    windowSumCounts = newCount * WINDOW_SECONDS;
    windowSumSquares = newCount * newCount * WINDOW_SECONDS;
    sampleCount = WINDOW_SECONDS;
  }

  unsigned long oldCount = countsPerSecond[currentSlot];
  countsPerSecond[currentSlot] = newCount;
  windowSumCounts += newCount - oldCount;
  windowSumSquares += newCount * newCount - oldCount * oldCount;
  currentSlot = (currentSlot + 1) % WINDOW_SECONDS;
  if (sampleCount < WINDOW_SECONDS) {
    sampleCount++;
  }

  float meanPerSec = sampleCount ? (float)windowSumCounts / sampleCount : 0;
  averageCpm = (unsigned long)(meanPerSec * 60.0);

  float variancePerSec = 0;
  if (sampleCount > 1) {
    variancePerSec = ((float)windowSumSquares - ((float)windowSumCounts * windowSumCounts) / sampleCount) / (sampleCount - 1);
  }
  float stdCpm = (sampleCount > 1) ? 60.0 * sqrt(variancePerSec / sampleCount) : 0;

  // https://sites.google.com/site/diygeigercounter/technical/gm-tubes-supported Fairly good explanation of conversion factors;
  // note they're technically specific to isotope AND specific tube, so you would need to have known reference sample, eg. another calibrated meter.
  // https://www.pascalchour.fr/ressources/cgm/cgm_en.html includes J305 for 60Co, though 137Cs is more likely encountered in nuclear disaster.
  // This would give us 123.147 for 60Co, 80.661 for 137Cs based on above reported SBM-20 comparison. This gives too high readings for radon background.
  // The CAJOE-1.3 original sketch has conversion factor around 151, which seems closer to SBS-20 value, but I'm leaving it as default for compatibility.
  float sievert = averageCpm / COUNTS_TO_SIEVERT;

  // lcd.clear(); // Causes
  lcd.setCursor(0, 0);
  // lcd.print("CPM=");                              // Matches original sketch, but isn't it kinda obvious? Save space for stddev
  lcd.print(averageCpm);
  lcd.write((uint8_t)0);                             // The +/- symbol
  lcd.print(stdCpm);

#ifdef TEST_INT
  if (inInterrupt || wasInInterrupt) {
    wasInInterrupt = true;
    lcd.print("WI ");                                // Indicator for non-atomic interrupt problem; shouldn't happen
  }
#endif
  lcd.print("     ");                                // Clear any possible leftover on screen

  // Status fits within 7 characters to overwrite previous values
  // http://www-ns.iaea.org/downloads/iec/health-hazard-perspec-charts2013.pdf 1 month living limits; check for contamination of ingestibles!
  const char* status = "Safety";
#ifdef IAEA_THRESHOLDS
  if (sievert > 100) {
    status = "Danger";
  } else if (sievert > 25) {
    status = "Warning";
  }
#else
  if (sievert > 10) {
    status = "Danger!";
  } else if (sievert > 0.5) {
    status = "Unsafe";
  }
#endif
  lcd.setCursor(16 - strlen(status), 0);
  lcd.print(status);

  // Second LCD line, show calculated sieverts and minimum time between ticks (max deadtime)
  lcd.setCursor(0, 1);
  lcd.print(sievert);
  lcd.print(" \x01Sv/h");

  unsigned long coincidence;
  noInterrupts();
  coincidence = coincidenceCount;
  interrupts();
  lcd.print(" ");
  lcd.print(coincidence);
  lcd.print(" ");
  lcd.print((unsigned long)((3600000ULL * coincidence) / (millis() - loggingStartTime)));
  lcd.print("    ");

  Serial.println(averageCpm);                            // Serial printout for Arduino Serial Plotter or analysis
}

void loop() {
  taskManager.runLoop();
}

void handleTube1Impulse()
{
#ifdef TEST_INT
  // https://microchipdeveloper.com/8avr:int Interrupts within interrupts shouldn't happen on AVR, but then again what hardware are you running?
  if (inInterrupt) {
    wasInInterrupt = true;
  }
  inInterrupt = true;
#endif
  processImpulse(countsTube1, lastTickTube1, lastTickTube2);

#ifdef TEST_INT
  inInterrupt = false;
#endif
}

void handleTube2Impulse()
{
  processImpulse(countsTube2, lastTickTube2, lastTickTube1);
}
