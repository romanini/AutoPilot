// Masthead air temperature from a DS18B20 on the 1-Wire bus, as fitted to the
// Yachta head.
//
// Wiring: DS18B20 data to the pin below, with a 4k7 pull-up to 3V3 (R4 on the
// board - see circuit/Sensor-Wind/). Powered from 3V3, not parasite mode.
//
// WHY THERE IS NO 1-WIRE LIBRARY HERE
//
// This used to be OneWire 2.3.8 + DallasTemperature 3.9.0, and it never read a
// single temperature on this board. OneWire's ESP32 back end takes the number
// it was constructed with and uses it as a raw GPIO bit index
// (`PIN_TO_BITMASK(pin)` is `(pin)`, then `GPIO.in >> pin`,
// `GPIO.out_w1tc = 1 << pin`). On the Nano ESP32 under the default Arduino pin
// numbering that number is an *Arduino* pin index, not a GPIO: D4 is 4, but
// the pad is GPIO7. So `begin()`'s `pinMode(D4, INPUT)` configured the right
// pad while every bus operation drove and sampled GPIO4 - which is A3, wired
// to nothing. The DS18B20 was never addressed at all, and the symptom was an
// empty bus rather than anything that pointed at the pin.
//
// The board's own convention of writing D4 rather than 7 is what makes the
// rest of this sketch immune to the Pin Numbering setting, but it only
// protects code that goes through the Arduino API. Any library reaching past
// that API to the GPIO registers is broken here no matter which name is used,
// so the fix is not to feed OneWire a different number - it is to not depend
// on a library that bypasses the remap. The bus is bit-banged below through
// digitalRead/digitalWrite, which the core translates correctly.
//
// That is affordable because this bus has exactly one device on it. There is
// no ROM search, no device table and no addressing: every transaction is SKIP
// ROM, so the whole driver is the ten primitives below plus three DS18B20
// commands.

// 1-Wire data pin. Clear of the I2C pins the vane uses and the interrupt pin
// the anemometer uses. This is where the wind sensor board routes DQ - see
// circuit/Sensor-Wind/.
#define ONE_WIRE_PIN D4

// How often a conversion is started, matching the original's 2 Hz. Air
// temperature does not need 2 Hz, but the rate is not free to change: it sets
// how much of the time the sensor is dissipating, and so feeds into
// TEMPERATURE_CORRECTION_C below (weakly - see the arithmetic there) and into
// the resolution choice after it (hard - a conversion must fit inside it).
#define TEMPERATURE_INTERVAL_MS 500

// Subtracted from every reading, and measured on this board rather than
// inherited: against an infrared thermometer on the sensor package, 19.9 was
// reported where the sensor read 20.9, and the correction in force at the time
// was 4.6 - so the die was at 24.5 and the right correction is 3.6.
//
// Aim the thermometer at the FLAT face, not the rounded back. The die is
// bonded against the flat, so that surface sits closest to what the sensor is
// actually measuring; a first attempt at this calibration read the back and
// came out 1.0 C too cold, which is most of the error being corrected here.
//
// The Yachta original calls its 6.0 self-heating compensation, but that cannot
// be what it mostly is. The DS18B20 draws about 1 mA while converting, so at
// 3.3 V for 375 ms out of every 500 it averages ~2.5 mW, and a TO-92 in still
// air is roughly 200 C/W junction-to-ambient - about 0.5 C of rise. The
// remaining ~4 C is the board: the ESP32-S3 module runs warm centimetres away
// on the same small PCB, so the sensor sits in air that is genuinely above
// ambient rather than merely reading itself wrong.
//
// That distinction is worth keeping straight, because it says which changes
// move this number. Halving the poll rate would shift it by ~0.25 C, which is
// inside the noise - so unlike what the original's comment implies, this is
// only weakly tied to TEMPERATURE_INTERVAL_MS. What *does* move it is anything
// that changes how heat leaves the board:
//
//   - It is a single-point fit at ~21 C, so an offset with no slope. Board
//     heating is roughly a fixed rise above ambient for a fixed power draw,
//     which is why an offset is the right shape at all, but nothing here has
//     been checked near 0 C or 35 C.
//   - It was measured on the bench, case open, USB powered. A sealed head
//     traps more heat and pushes the true correction up; apparent wind over
//     the masthead strips heat away and pushes it down. Re-measure once the
//     head is closed and in free air.
#define TEMPERATURE_CORRECTION_C 3.6f

// 11-bit resolution: 0.125 C, and a 375 ms conversion.
//
// The default 12 bits takes 750 ms, which will not fit inside a 500 ms poll
// interval - the read on the following tick would land before the conversion
// finished and return the previous value. Dropping a bit keeps the 2 Hz rate,
// and 0.125 C is far finer than anything air temperature is being used for
// here - the correction above is uncertain by more than an order of magnitude
// more than the quantisation is.
#define DS18B20_RESOLUTION_BITS 11

// Configuration-register byte for the resolution above: bits 6:5 are R1:R0,
// and everything else reads back as 1. 9/10/11/12 bits are 0x1F/0x3F/0x5F/0x7F.
#define DS18B20_CONFIG_BYTE 0x5F

// How long without a good reading before temp_ok is cleared.
//
// This is the same trap the controller avoids for the rudder board with a
// receive timeout: without it, anything that stops the two-phase cycle
// completing - a conversion that never starts, a device that stops answering
// between ticks - leaves the last good temperature sitting on the wire with
// temp_ok still set, and a frozen reading is worse than no reading because
// nothing downstream can tell it apart from a live one.
#define TEMPERATURE_STALE_MS 3000

// How often a failed rescan is reported on the debug console. The rescan
// itself runs every TEMPERATURE_INTERVAL_MS, and a missing sensor is a
// standing condition rather than an event - printing one line per rescan would
// bury every other debug line at 2 Hz.
#define TEMPERATURE_REPORT_INTERVAL_MS 10000

#define DS18B20_READ_ROM         0x33
#define DS18B20_SKIP_ROM         0xCC
#define DS18B20_CONVERT_T        0x44
#define DS18B20_WRITE_SCRATCHPAD 0x4E
#define DS18B20_READ_SCRATCHPAD  0xBE
#define DS18B20_FAMILY_CODE      0x28

static unsigned long lastTemperatureTime = 0;
static unsigned long lastTemperatureReportTime = 0;
static bool conversionPending = false;
static bool deviceKnown = false;
static unsigned long lastGoodReadTime = 0;

// ---------------------------------------------------------------------------
// 1-Wire bit banging
//
// Timings are the DS18B20 datasheet's. The pin is held in open-drain mode for
// the whole exchange: a 1-Wire master must never drive the line high (the
// device answers by pulling it down, and two drivers would fight), and
// open-drain keeps the input buffer live so a level read back is the one
// actually on the pad rather than an echo of the output register.
//
// Interrupts are disabled only across the parts of a slot that have an upper
// time bound, never across a low period that has only a lower bound. The
// anemometer's reed interrupt shares this core, and delaying it by the ~10 us
// of a bit slot is harmless, where letting it stretch a 6 us write-1 pulse
// past 15 us would corrupt the bit. The 480 us reset pulse is deliberately
// left interruptible for the same reason in reverse: it has a minimum and no
// maximum, so an ISR landing in the middle of it costs nothing.
// ---------------------------------------------------------------------------

static inline void ow_low() {
  digitalWrite(ONE_WIRE_PIN, LOW);
}

static inline void ow_release() {
  digitalWrite(ONE_WIRE_PIN, HIGH);
}

// Returns true if a device answered with a presence pulse.
static bool ow_reset() {
  pinMode(ONE_WIRE_PIN, OUTPUT_OPEN_DRAIN);
  ow_release();
  delayMicroseconds(10);
  ow_low();
  delayMicroseconds(480);

  noInterrupts();
  ow_release();
  delayMicroseconds(70);
  bool presence = digitalRead(ONE_WIRE_PIN) == LOW;
  interrupts();

  delayMicroseconds(410);
  return presence;
}

static void ow_write_bit(bool bit) {
  noInterrupts();
  ow_low();
  delayMicroseconds(bit ? 6 : 60);
  ow_release();
  interrupts();
  delayMicroseconds(bit ? 64 : 10);
}

static bool ow_read_bit() {
  noInterrupts();
  ow_low();
  delayMicroseconds(6);
  ow_release();
  delayMicroseconds(9);
  bool bit = digitalRead(ONE_WIRE_PIN) == HIGH;
  interrupts();
  delayMicroseconds(55);
  return bit;
}

// 1-Wire is little-endian: least significant bit first.
static void ow_write_byte(uint8_t value) {
  for (int i = 0; i < 8; i++) {
    ow_write_bit(value & (1 << i));
  }
}

static uint8_t ow_read_byte() {
  uint8_t value = 0;
  for (int i = 0; i < 8; i++) {
    if (ow_read_bit()) {
      value |= (1 << i);
    }
  }
  return value;
}

// Dallas/Maxim CRC-8, polynomial x^8 + x^5 + x^4 + 1 (0x8C reflected). Computed
// rather than table-driven: it runs at most twice a second over nine bytes, so
// the 256-byte table would buy nothing worth the flash.
static uint8_t ow_crc8(const uint8_t* data, uint8_t length) {
  uint8_t crc = 0;
  for (uint8_t i = 0; i < length; i++) {
    uint8_t byte = data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      uint8_t mix = (crc ^ byte) & 0x01;
      crc >>= 1;
      if (mix) {
        crc ^= 0x8C;
      }
      byte >>= 1;
    }
  }
  return crc;
}

// ---------------------------------------------------------------------------
// DS18B20
// ---------------------------------------------------------------------------

// READ ROM is only legal with a single device on the bus - with two, both
// answer at once and the wired-AND of their ROMs comes back as garbage that
// fails the CRC. That is the right trade here: this head has one sensor by
// construction, and reading the ROM checks far more than a search would. A
// good CRC over eight bytes plus the 0x28 family code proves the part is a
// DS18B20 and that the bus timing is sound, which is exactly what has to be
// true before a temperature can be believed.
static bool ds18b20_read_rom(uint8_t rom[8]) {
  if (!ow_reset()) {
    return false;
  }
  ow_write_byte(DS18B20_READ_ROM);
  for (int i = 0; i < 8; i++) {
    rom[i] = ow_read_byte();
  }
  return ow_crc8(rom, 7) == rom[7] && rom[0] == DS18B20_FAMILY_CODE;
}

// TH/TL are the alarm thresholds. Nothing here uses the alarm, but the write
// is all-or-nothing - the config byte cannot be set without also sending them -
// so they are given the datasheet power-on defaults.
static bool ds18b20_set_resolution() {
  if (!ow_reset()) {
    return false;
  }
  ow_write_byte(DS18B20_SKIP_ROM);
  ow_write_byte(DS18B20_WRITE_SCRATCHPAD);
  ow_write_byte(0x4B);
  ow_write_byte(0x46);
  ow_write_byte(DS18B20_CONFIG_BYTE);
  return true;
}

static bool ds18b20_start_conversion() {
  if (!ow_reset()) {
    return false;
  }
  ow_write_byte(DS18B20_SKIP_ROM);
  ow_write_byte(DS18B20_CONVERT_T);
  // Deliberately not waited on. The conversion runs in the device while
  // sensor_task gets on with the vane, the anemometer and ~APWND; the result
  // is collected on the next tick, TEMPERATURE_INTERVAL_MS later. Blocking for
  // the 375 ms conversion would stall vane sampling and publishing for three
  // quarters of every poll interval.
  return true;
}

// Why a read failed, so the caller can say. A bare false would leave temp_ok =
// 0 with no way to tell a bus that has gone quiet from one that is answering
// with corrupt bytes, and those want opposite fixes - the first is wiring, the
// second is timing.
enum ReadResult {
  READ_OK,
  READ_NO_PRESENCE,  // nobody answered the reset
  READ_BAD_CRC,      // bytes came back, but mangled
  READ_IDLE_BUS,     // all-ones: the pull-up, not a device
};

static ReadResult lastReadResult = READ_OK;
static uint8_t lastScratchpad[9];

static bool ds18b20_read_celsius(float* celsius) {
  memset(lastScratchpad, 0, sizeof(lastScratchpad));

  if (!ow_reset()) {
    lastReadResult = READ_NO_PRESENCE;
    return false;
  }
  ow_write_byte(DS18B20_SKIP_ROM);
  ow_write_byte(DS18B20_READ_SCRATCHPAD);

  uint8_t scratchpad[9];
  for (int i = 0; i < 9; i++) {
    scratchpad[i] = ow_read_byte();
  }
  memcpy(lastScratchpad, scratchpad, sizeof(scratchpad));

  if (ow_crc8(scratchpad, 8) != scratchpad[8]) {
    lastReadResult = READ_BAD_CRC;
    return false;
  }

  // Bytes 0/1 are the temperature, signed, LSB first, in sixteenths of a
  // degree. An all-ones scratchpad is what a bus with nothing on it reads
  // back; it happens to carry a valid CRC of 0xFF, so it has to be rejected
  // explicitly rather than being caught by the check above. (0x0550 is the
  // power-on value, +85 C, which is a real reading the device gives when asked
  // for a temperature before any conversion has completed - it is left alone
  // here because the two-phase cadence never asks that early.)
  if (scratchpad[0] == 0xFF && scratchpad[1] == 0xFF) {
    lastReadResult = READ_IDLE_BUS;
    return false;
  }

  int16_t raw = (int16_t)((scratchpad[1] << 8) | scratchpad[0]);
  *celsius = raw / 16.0f;
  lastReadResult = READ_OK;
  return true;
}

// Everything known about why there is no temperature, in one place: the state
// machine's own view, the last scratchpad off the wire, and then a fresh
// electrical probe of the line.
static void report_temperature_failure() {
  DEBUG_PRINTLN("--- No DS18B20 temperature ---");
  DEBUG_PRINT("  state     : deviceKnown=");
  DEBUG_PRINT(deviceKnown ? "yes" : "no");
  DEBUG_PRINT(" conversionPending=");
  DEBUG_PRINTLN(conversionPending ? "yes" : "no");
  DEBUG_PRINT("  last read : ");
  switch (lastReadResult) {
    case READ_NO_PRESENCE: DEBUG_PRINT("no presence pulse"); break;
    case READ_BAD_CRC:     DEBUG_PRINT("scratchpad CRC bad"); break;
    case READ_IDLE_BUS:    DEBUG_PRINT("bus idle (all ones)"); break;
    default:               DEBUG_PRINT("no read attempted"); break;
  }
  DEBUG_PRINT("   scratchpad:");
  for (int i = 0; i < 9; i++) {
    DEBUG_PRINT(" ");
    if (lastScratchpad[i] < 0x10) DEBUG_PRINT("0");
    DEBUG_PRINT2(lastScratchpad[i], HEX);
  }
  DEBUG_PRINTLN("");
  report_empty_bus();
}

// Confirms a DS18B20 is on the bus and applies the resolution. This - not the
// conversion command - is what has to be repeated for a sensor that was not
// present at boot.
static bool scan_temperature_bus() {
  uint8_t rom[8];
  if (!ds18b20_read_rom(rom)) {
    return false;
  }
  return ds18b20_set_resolution();
}

// Says why the bus came up empty, at a rate a human can read. Without it the
// only symptom is temp_ok = 0, which is identical for a missing sensor, an
// open joint, a short and a dead part - and on a sealed masthead unit that
// difference is the whole diagnosis.
//
//   drive low    can the ESP32 pull the line down at all? A failure means the
//                net is shorted to 3V3, or ONE_WIRE_PIN is not the pad this
//                copper actually reaches. It rules the master out before
//                anything gets blamed on the sensor.
//   rise time    microseconds for the pull-up to recover the released line.
//                With R4 = 4k7 and a short board trace this is 1-3 us. Tens of
//                microseconds means far more capacitance than that - a long
//                run up the mast. "never" means no pull-up at all.
//   presence     a DS18B20 answers a reset 15-60 us after release and holds
//                for 60-240 us. Seeing one proves the part is alive and its
//                DQ joint is sound, which no voltmeter reading on the pads can.
static void report_empty_bus() {
  pinMode(ONE_WIRE_PIN, OUTPUT_OPEN_DRAIN);
  ow_low();
  delayMicroseconds(10);
  bool canDriveLow = digitalRead(ONE_WIRE_PIN) == LOW;

  noInterrupts();
  ow_release();
  unsigned long start = micros();
  while (digitalRead(ONE_WIRE_PIN) == LOW && (micros() - start) < 1000) {
  }
  unsigned long riseUs = micros() - start;
  interrupts();
  bool rose = digitalRead(ONE_WIRE_PIN) == HIGH;

  bool presence = ow_reset();
  uint8_t rom[8];
  bool romOk = ds18b20_read_rom(rom);
  pinMode(ONE_WIRE_PIN, INPUT);

  DEBUG_PRINT("  drive low : ");
  DEBUG_PRINTLN(canDriveLow ? "ok" : "FAILED - line will not go low");
  DEBUG_PRINT("  rise time : ");
  if (rose) {
    DEBUG_PRINT(riseUs);
    DEBUG_PRINTLN(" us");
  } else {
    DEBUG_PRINTLN("never rose - no pull-up on this net");
  }
  DEBUG_PRINT("  presence  : ");
  DEBUG_PRINTLN(presence ? "yes" : "none");
  DEBUG_PRINT("  verdict   : ");
  if (!canDriveLow) {
    DEBUG_PRINTLN("cannot drive D4 - net shorted to 3V3, or wrong pin");
  } else if (!rose) {
    DEBUG_PRINTLN("no pull-up - check R4 (4k7) to 3V3");
  } else if (!presence) {
    DEBUG_PRINTLN("nothing answers a reset - open DQ joint, no 3V3 at the sensor, or a dead part");
  } else if (!romOk) {
    DEBUG_PRINTLN("a device answers but its ROM will not read - bus timing, or not a DS18B20");
  } else {
    DEBUG_PRINTLN("the sensor reads fine now - transient, recovering on the next tick");
  }
}

void setup_temperature() {
  pinMode(ONE_WIRE_PIN, INPUT);
  deviceKnown = scan_temperature_bus();
  if (deviceKnown) {
    DEBUG_PRINTLN("DS18B20 found on the 1-Wire bus");
  } else {
    // Not fatal, unlike a missing vane. Wind speed and direction are what this
    // board is for; temperature is a nice-to-have that rides along. The wire
    // format carries a temp_ok flag precisely so a missing or unplugged sensor
    // reads as "no data" downstream rather than as a plausible 0 degrees, and
    // check_temperature() keeps rescanning, so one that is connected later (or
    // a flaky joint that comes back) starts working without a restart.
    DEBUG_PRINTLN("No DS18B20 on the 1-Wire bus - continuing without temperature");
  }
}

// Two-phase, one phase per call: collect the conversion started last tick,
// then start the next one. Gated on TEMPERATURE_INTERVAL_MS, so it is safe to
// call from sensor_task's much faster loop.
//
// The staleness check at the bottom is a backstop on the *result* rather than
// a branch off each failure, because per-path reporting has a hole in it: a
// read that fails clears deviceKnown and gets caught on the next tick, but a
// conversion whose reset finds nobody leaves deviceKnown true and
// conversionPending false, and that state is reachable by neither reporter -
// temp_ok would read 0 indefinitely with nothing on the console. Keying off
// "no good reading recently" cannot miss a path, because it does not care
// which one was taken.
void check_temperature() {
  if (millis() - lastTemperatureTime < TEMPERATURE_INTERVAL_MS) {
    return;
  }
  lastTemperatureTime = millis();

  if (conversionPending) {
    conversionPending = false;
    float celsius = 0.0;
    if (ds18b20_read_celsius(&celsius)) {
      wind.setTemperature(celsius - TEMPERATURE_CORRECTION_C, true);
      lastGoodReadTime = millis();
    } else {
      deviceKnown = false;  // rescanned below
    }
  }

  if (!deviceKnown) {
    if (scan_temperature_bus()) {
      deviceKnown = true;
      DEBUG_PRINTLN("DS18B20 appeared on the 1-Wire bus");
    }
  }

  if (deviceKnown) {
    conversionPending = ds18b20_start_conversion();
    if (!conversionPending) {
      // The reset ahead of CONVERT T found nobody, so the device has gone
      // since the scan. Recorded rather than retried here: the next tick
      // rescans, and the report below says what happened.
      lastReadResult = READ_NO_PRESENCE;
      deviceKnown = false;
    }
  }

  if (millis() - lastGoodReadTime >= TEMPERATURE_STALE_MS) {
    wind.setTemperature(0.0, false);
    if (millis() - lastTemperatureReportTime >= TEMPERATURE_REPORT_INTERVAL_MS) {
      lastTemperatureReportTime = millis();
      report_temperature_failure();
    }
  }
}
