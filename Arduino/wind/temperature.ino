// Masthead air temperature from a DS18B20 on the 1-Wire bus, as fitted to the
// Yachta head.
//
// Wiring: DS18B20 data to the pin below, with a 4k7 pull-up to 3V3. Powered
// from 3V3 (not parasite mode - the library's parasite support needs the bus
// held high during conversion, which we do not do here).

#include <OneWire.h>
#include <DallasTemperature.h>

// 1-Wire data pin. Clear of the I2C pins the vane uses and the interrupt pin
// the anemometer uses.
#define ONE_WIRE_PIN D3

// How often a conversion is started. The original polls at 2 Hz and this
// matters more than it looks - see the self-heating note below.
#define TEMPERATURE_INTERVAL_MS 500

// The DS18B20 dissipates enough during back-to-back conversions to read high,
// and the original firmware subtracts a flat 6 degrees C to compensate. That
// constant is only valid for the polling rate it was calibrated at, which is
// why TEMPERATURE_INTERVAL_MS above matches the original's timer rather than
// being set to something slower and tidier.
#define DS18B20_SELF_HEATING_C 6.0f

// 11-bit resolution: 0.125 C, and a 375 ms conversion.
//
// The default 12 bits takes 750 ms, which will not fit inside a 500 ms poll
// interval - the read on the following tick would land before the conversion
// finished and return the previous value. Dropping a bit keeps the 2 Hz rate
// the self-heating constant above assumes, and 0.125 C is far finer than
// anything air temperature is being used for here.
#define DS18B20_RESOLUTION_BITS 11

static OneWire oneWire(ONE_WIRE_PIN);
static DallasTemperature sensors(&oneWire);

static unsigned long lastTemperatureTime = 0;
static bool conversionPending = false;
static bool deviceKnown = false;

// Walks the 1-Wire bus and caches the address of the first DS18B20 on it.
// getTempCByIndex() reads from that cache, so this - not requestTemperatures()
// - is what has to be repeated for a sensor that was not present at boot.
static bool scan_temperature_bus() {
  sensors.begin();
  // Non-blocking conversions: requestTemperatures() returns immediately and we
  // collect the result on the next tick. The library's default is to block for
  // the whole conversion, which on sensor_task would stall vane sampling and
  // ~APWND publishing for 375 ms out of every 500. Re-applied after every
  // begin() because begin() re-derives the resolution from what it finds.
  sensors.setWaitForConversion(false);
  sensors.setResolution(DS18B20_RESOLUTION_BITS);
  return sensors.getDeviceCount() > 0;
}

void setup_temperature() {
  deviceKnown = scan_temperature_bus();
  DEBUG_PRINT("DS18B20 devices found: ");
  DEBUG_PRINTLN(sensors.getDeviceCount());
  if (!deviceKnown) {
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
void check_temperature() {
  if (millis() - lastTemperatureTime < TEMPERATURE_INTERVAL_MS) {
    return;
  }
  lastTemperatureTime = millis();

  if (conversionPending) {
    conversionPending = false;
    float celsius = sensors.getTempCByIndex(0);
    if (celsius == DEVICE_DISCONNECTED_C) {
      wind.setTemperature(0.0, false);
      deviceKnown = false;  // gone - rescan below
    } else {
      wind.setTemperature(celsius - DS18B20_SELF_HEATING_C, true);
    }
  }

  if (!deviceKnown) {
    deviceKnown = scan_temperature_bus();
    if (!deviceKnown) {
      return;  // still nothing on the bus - try again next tick
    }
    DEBUG_PRINTLN("DS18B20 appeared on the 1-Wire bus");
  }

  sensors.requestTemperatures();
  conversionPending = true;
}
