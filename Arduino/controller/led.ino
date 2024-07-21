
#define FLASH_INTERVAL 25
#define FLASH_COUNT 5
#define FLASH_PIN 2

uint32_t flash_millis = millis();
bool led_on = false;
int flash_count = 0;

void setup_led() {
  pinMode(FLASH_PIN, OUTPUT);
  Serial.println("Led all setup");
}

void flash_led() {
  flash_count += 1;
  if (flash_count >= FLASH_COUNT) {
    flash_count = 0;
    flash_millis = millis();
    led_on = true;
    digitalWrite(FLASH_PIN, HIGH);
  }
}

void check_led() {
  if (led_on && (millis() - flash_millis > FLASH_INTERVAL)) {
    led_on = false;
    digitalWrite(FLASH_PIN, LOW);
  }
}

