#define CHECK_INTERVAL 1000

// TODO (next hardware rev): these ratios are for the boards currently in
// service. The rev 2.2 schematic in circuit/Display/ fits different dividers -
// 10k/1.8k on v-in (0.1525) and 10k/12k on the battery (0.5455). Swap both
// ratios over when the new boards go in, or the battery will read ~21% low and
// v-in ~15% low, then re-measure and reset the correction factors.

// Battery 4.2 max R1 = 10k, R2 = 22k ratio = 0.6875
const int batteryVoltagePin = A0;
const float batteryVoltageDividerRatio = .6875;  // Voltage divider ratio (R2/(R1+R2))
const float batteryCorrectionFactor = 1.0; //0.9880095;

// v-in (12v) R1 = 10k, R2 = 2.2k ratio = 0.1803
const int inputVoltagePin = A1;
const float inputVoltageDividerRatio = .1803;    // Voltage divider ratio (R2/(R1+R2))
const float inputCorrectionFactor = 1.01666667; //0.9816733;

uint32_t last_check_time_mills = millis();

void check_voltage() {
  if (millis() - last_check_time_mills > CHECK_INTERVAL) {
    last_check_time_mills = millis();

    // analogReadMilliVolts() uses the ESP32's factory-burned eFuse calibration,
    // giving a much more accurate reading than (analogRead() * 3.3 / 4095).
    // After deploying this change, re-measure both channels with a multimeter
    // and update the correction factors above (expect them to be close to 1.0).
    float adcMilliVolts = analogReadMilliVolts(batteryVoltagePin);
    float voltage = (adcMilliVolts / 1000.0 / batteryVoltageDividerRatio) * batteryCorrectionFactor;
    autoPilot.setBatteryVoltage(voltage);

    adcMilliVolts = analogReadMilliVolts(inputVoltagePin);
    voltage = (adcMilliVolts / 1000.0 / inputVoltageDividerRatio) * inputCorrectionFactor;
    autoPilot.setInputVoltage(voltage);
  }
}
