
#include <WiFi.h>
#include <esp_now.h>

// RECEIVER ESP32 MAC
uint8_t receiverMAC[] = {
  0x8c, 0x94, 0xdf, 0x71, 0x53, 0x70
};
 
// JOYSTICK PINS
#define THROTTLE_PIN 34
#define STEERING_PIN 35

// BUTTON PINS
#define SW1 13
#define SW2 12

// CALIBRATION
#define THROTTLE_CENTER 2816
#define STEERING_CENTER 1895

// DEADZONE
#define DEADZONE 150

// PACKET - MUST MATCH RECEIVER
typedef struct __attribute__((packed)) {
  uint32_t magic;
  int16_t left;
  int16_t right;
  uint8_t mode;
} MotorPacket;

MotorPacket packet;
const uint32_t MAGIC = 0x524F424F;

bool slowMode = false;

// BUTTON STATE
bool lastSW1 = HIGH;
bool lastSW2 = HIGH;

bool pendingSW1 = false;
bool pendingSW2 = false;

unsigned long firstPressTime = 0;
unsigned long lastDebounce1 = 0;
unsigned long lastDebounce2 = 0;

const unsigned long DOUBLE_PRESS_WINDOW = 100;
const unsigned long DEBOUNCE_TIME = 40;

void onDataSent(const wifi_tx_info_t *info,
                esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_SUCCESS) {
    Serial.println("ESP-NOW SEND FAILED");
  }
}

int readAxis(int pin, int center) {
  int raw = analogRead(pin);
  int value;

  if (raw > center) {
    value = map(raw, center, 4095, 0, 255);
  } else {
    value = map(raw, 0, center, -255, 0);
  }

  if (abs(raw - center) < DEADZONE) {
    value = 0;
  }

  return constrain(value, -255, 255);
}

// BUTTON CONTROL
void handleButtons() {
  unsigned long now = millis();

  bool sw1 = digitalRead(SW1);
  bool sw2 = digitalRead(SW2);

  bool press1 = (lastSW1 == HIGH && sw1 == LOW);
  bool press2 = (lastSW2 == HIGH && sw2 == LOW);

  lastSW1 = sw1;
  lastSW2 = sw2;

  if (press1 && now - lastDebounce1 >= DEBOUNCE_TIME) {
    lastDebounce1 = now;

    if (pendingSW2 && now - firstPressTime <= DOUBLE_PRESS_WINDOW) {
      slowMode = false;
      pendingSW1 = false;
      pendingSW2 = false;
      Serial.println("MODE = NORMAL (100%)");
    }
    else {
      pendingSW1 = true;
      pendingSW2 = false;
      firstPressTime = now;
    }
  }

  if (press2 && now - lastDebounce2 >= DEBOUNCE_TIME) {
    lastDebounce2 = now;

    if (pendingSW1 && now - firstPressTime <= DOUBLE_PRESS_WINDOW) {
      slowMode = false;
      pendingSW1 = false;
      pendingSW2 = false;
      Serial.println("MODE = NORMAL (100%)");
    }
    else {
      pendingSW2 = true;
      pendingSW1 = false;
      firstPressTime = now;
    }
  }

  // If second button is not pressed within 500 ms,
  // select SLOW mode.
  if ((pendingSW1 || pendingSW2) &&
      now - firstPressTime > DOUBLE_PRESS_WINDOW) {

    slowMode = true;
    pendingSW1 = false;
    pendingSW2 = false;

    Serial.println("MODE = SLOW (70%)");
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(SW1, INPUT_PULLUP);
  pinMode(SW2, INPUT_PULLUP);

  analogReadResolution(12);
  analogSetPinAttenuation(THROTTLE_PIN, ADC_11db);
  analogSetPinAttenuation(STEERING_PIN, ADC_11db);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.print("Transmitter MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW INIT FAILED");
    while (true) delay(1000);
  }

  esp_now_register_send_cb(onDataSent);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, receiverMAC, 6);
  peer.channel = 0;
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("PEER ADD FAILED");
    while (true) delay(1000);
  }

  Serial.println("TRANSMITTER READY");
}

void loop() {
  handleButtons();

  int throttle = readAxis(
    THROTTLE_PIN, THROTTLE_CENTER
  );

  int steering = readAxis(
    STEERING_PIN, STEERING_CENTER
  );

  // TEMPORARY SAFETY:
  // Disable steering while throttle is active.
  if (abs(throttle) > 35) {
    steering = 0;
  }

  int left = constrain(throttle + steering, -255, 255);
  int right = constrain(throttle - steering, -255, 255);

  if (slowMode) {
    left = left * 70 / 100;
    right = right * 70 / 100;
  }

  packet.magic = MAGIC;
  packet.left = left;
  packet.right = right;
  packet.mode = slowMode ? 1 : 0;

  esp_now_send(
    receiverMAC,
    (uint8_t *)&packet,
    sizeof(packet)
  );

  Serial.print("Throttle: ");
  Serial.print(throttle);
  Serial.print(" | Steering: ");
  Serial.print(steering);
  Serial.print(" | Left: ");
  Serial.print(left);
  Serial.print(" | Right: ");
  Serial.print(right);
  Serial.print(" | Mode: ");
  Serial.println(slowMode ? "SLOW 70%" : "NORMAL 100%");

  delay(30);
}
