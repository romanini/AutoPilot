#define CHECK_INTERVAL 5000

// Battery 4.2 max R1 = 10k, R2 = 22k ratio = 0.6875
const int batteryVoltagePin = A1;
const float batteryVoltageDividerRatio = .6875;  // Voltage divider ratio (R2/(R1+R2))
const float batteryCorrectionFactor = 0.9880095;

// v-in (12v) R1 = 10k, R2 = 2.2k ratio = 0.1803
const int inputVoltagePin = A0;
const float inputVoltageDividerRatio = .1803;    // Voltage divider ratio (R2/(R1+R2))
const float inputCorrectionFactor = 0.9816733;

uint32_t last_check_time_mills = millis();

void check_voltage() {
  if (millis() - last_check_time_mills > CHECK_INTERVAL) {
    last_check_time_mills = millis();
    int sensorValue = analogRead(batteryVoltagePin);                        // Read the analog input
    // DEBUG_PRINT("Sensor Value: ");
    // DEBUG_PRINTLN(sensorValue);
    float voltage = (sensorValue * (3.3 / 1023.0) / batteryVoltageDividerRatio) * batteryCorrectionFactor;  // Convert to actual voltage
    // DEBUG_PRINT("Battery Voltage: ");
    // DEBUG_PRINTLN(voltage);
    autoPilot.setBatteryVoltage(voltage);

    sensorValue = analogRead(inputVoltagePin);                          // Read the analog input
    // DEBUG_PRINT("Sensor Value: ");
    // DEBUG_PRINTLN(sensorValue);
    voltage = (sensorValue * (3.3 / 1023.0) / inputVoltageDividerRatio) * inputCorrectionFactor;  // Convert to actual voltage
    // DEBUG_PRINT("Input Voltage: ");
    // DEBUG_PRINTLN(voltage);
    autoPilot.setInputVoltage(voltage);
  }
}
