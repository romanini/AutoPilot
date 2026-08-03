// Cup anemometer: a reed switch that closes twice per revolution as the magnet
// in the cup wheel passes it. Wind speed is derived from how long a revolution
// takes, so all this file does is time the gaps between pulses.
//
// Wiring: reed switch between the pin below and GND, with the pin pulled up
// internally. The original Yachta build also fits a 10k / 100n RC snubber at
// the pin for spike suppression; PULSE_DEBOUNCE_US below covers the same
// ground in software, so the RC is belt-and-braces rather than required.
//
// This is the part of the port that changes shape the most. The ESP8266
// original ran a hardware timer at 100 us purely to *count* ticks between
// pulses, because it wanted a free-running counter it could read from an
// interrupt. On the ESP32, micros() is exactly that counter, so the timer, its
// ISR, the tick counters and the marker/state machine that drove them all
// disappear: the pulse interrupt subtracts two timestamps and is done.
//
// One consequence worth knowing: the original recorded an interval only on
// every *other* pulse (its marker1 toggled), so it sampled once per revolution
// even though it was measuring a half-revolution gap. This version records
// every pulse. Same quantity, twice as many samples, no downside.

// Reed switch input. Any GPIO with interrupt capability works; D2 is chosen to
// stay clear of the I2C pins the vane needs and the 1-Wire pin the DS18B20 uses.
#define WIND_SPEED_PIN D2

// Ignore pulses closer together than this. At the sensor's stated 73 knot
// ceiling the cups turn about 42 times a second, i.e. 84 pulses a second, i.e.
// one pulse every 12 ms - so 2 ms is a wide margin below any real interval
// while still swallowing reed-switch chatter.
#define PULSE_DEBOUNCE_US 2000

// Interval clamp, in microseconds. Mirrors ANEMOMETER_PERIOD_LIMIT_MS on the
// calculation side (Wind.h): intervals at or beyond this are recorded at the
// limit, and Wind::calculate() then declines to convert a period that has hit
// it into a speed.
#define PULSE_PERIOD_LIMIT_US ((uint32_t)(ANEMOMETER_PERIOD_LIMIT_MS * 1000.0f))

// No pulse for this long means the anemometer has stopped and the speed is
// zero, rather than frozen at whatever it was doing when it slowed down. Three
// seconds matches the original's zero-wind detector, and corresponds to
// roughly 0.3 m/s - below which this style of cup anemometer has stalled
// anyway.
#define WIND_ZERO_TIMEOUT_US 3000000UL

// How many pulse intervals to average over. The original exposes this on its
// settings page as 1..10 and notes "for high speed use 1, default use 2".
// Averaging periods and then inverting is not the same as averaging
// frequencies, so this is kept small on purpose - at 2 the bias is negligible,
// and most of the smoothing this port needs comes from publishing at 5 Hz
// rather than from a long window here.
#define PULSE_AVERAGE_COUNT 2

// Shared with the interrupt handler. portMUX + portENTER_CRITICAL is the ESP32
// equivalent of the original's noInterrupts()/ATOMIC() blocks, and is what
// makes a multi-word read on the task side consistent against an interrupt
// that could land on the other core mid-copy.
static portMUX_TYPE anemometerMux = portMUX_INITIALIZER_UNLOCKED;

static volatile uint32_t lastPulseMicros = 0;
static volatile uint32_t pulseIntervals[PULSE_AVERAGE_COUNT];
static volatile uint8_t pulseIndex = 0;
static volatile uint8_t pulseFilled = 0;
static volatile bool pulsePrimed = false;

// Pulse interrupt. Deliberately integer-only: the ESP32's FPU registers are
// context-switched lazily, and touching a float from an ISR is a known way to
// corrupt whatever task happened to be using the FPU at the time. The
// microsecond intervals stored here are converted to milliseconds in
// read_anemometer() below, on the task side.
void IRAM_ATTR anemometer_isr() {
  uint32_t now = micros();

  portENTER_CRITICAL_ISR(&anemometerMux);
  if (!pulsePrimed) {
    // First pulse since boot: there is no previous timestamp to subtract, so
    // record the time and wait for the next one. Without this the first
    // "interval" would be the whole uptime.
    lastPulseMicros = now;
    pulsePrimed = true;
    portEXIT_CRITICAL_ISR(&anemometerMux);
    return;
  }

  uint32_t interval = now - lastPulseMicros;  // unsigned: wraps correctly at 71 minutes
  if (interval < PULSE_DEBOUNCE_US) {
    portEXIT_CRITICAL_ISR(&anemometerMux);
    return;  // contact bounce, not a real revolution
  }

  lastPulseMicros = now;
  if (interval > PULSE_PERIOD_LIMIT_US) {
    interval = PULSE_PERIOD_LIMIT_US;
  }
  pulseIntervals[pulseIndex] = interval;
  pulseIndex = (pulseIndex + 1) % PULSE_AVERAGE_COUNT;
  if (pulseFilled < PULSE_AVERAGE_COUNT) {
    pulseFilled++;
  }
  portEXIT_CRITICAL_ISR(&anemometerMux);
}

void setup_anemometer() {
  pinMode(WIND_SPEED_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(WIND_SPEED_PIN), anemometer_isr, FALLING);
  DEBUG_PRINT("Anemometer interrupt attached to pin ");
  DEBUG_PRINTLN(WIND_SPEED_PIN);
}

// Averaged interval between pulses, in milliseconds. `validOut` is false while
// the anemometer is stopped or has not turned twice yet, which Wind::calculate()
// reads as a wind speed of zero.
void read_anemometer(float* periodMsOut, bool* validOut) {
  uint32_t intervals[PULSE_AVERAGE_COUNT] = {0};
  uint8_t filled;
  bool stopped;

  portENTER_CRITICAL(&anemometerMux);
  filled = pulseFilled;
  stopped = !pulsePrimed || (uint32_t)(micros() - lastPulseMicros) > WIND_ZERO_TIMEOUT_US;
  if (stopped) {
    // Latch the stop rather than just reporting it: drop the recorded
    // intervals so the state cannot come back without a genuine new pulse.
    // Without this, micros() rolling over during a long calm (it wraps every
    // ~71 minutes) briefly makes lastPulseMicros look recent again, and a
    // reading taken in that window would resurrect whatever speed the wind was
    // doing when it died. Restarting costs one extra pulse, which at three
    // seconds of stillness is nothing.
    pulseFilled = 0;
    pulsePrimed = false;
  } else {
    memcpy(intervals, (const void*)pulseIntervals, sizeof(intervals));
  }
  portEXIT_CRITICAL(&anemometerMux);

  if (stopped || filled == 0) {
    *periodMsOut = 0.0;
    *validOut = false;
    return;  // stopped, or has not turned since boot
  }

  uint64_t sum = 0;
  for (uint8_t i = 0; i < filled; i++) {
    sum += intervals[i];
  }
  *periodMsOut = ((float)sum / (float)filled) / 1000.0;
  *validOut = true;
}
