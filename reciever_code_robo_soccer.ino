
#include <WiFi.h>
#include <esp_now.h>

// TRANSMITTER MAC ADDRESS
uint8_t transmitterMAC[] = {
  0x30, 0x76, 0xf5, 0xe6, 0x4a, 0x44
};

// LEFT BTS7960
#define LEFT_RPWM 25
#define LEFT_LPWM 26
#define LEFT_REN  27
#define LEFT_LEN  14

// RIGHT BTS7960
#define RIGHT_RPWM 32
#define RIGHT_LPWM 33
#define RIGHT_REN  18
#define RIGHT_LEN  19

// PACKET - MUST MATCH TRANSMITTER
typedef struct __attribute__((packed)) {
  uint32_t magic;
  int16_t left;
  int16_t right;
  uint8_t mode;
} MotorPacket;

MotorPacket receivedPacket;

const uint32_t MAGIC = 0x524F424F;

volatile int receivedLeft = 0;
volatile int receivedRight = 0;
volatile uint8_t receivedMode = 0;
volatile bool newData = false;

unsigned long lastReceiveTime = 0;

const unsigned long FAILSAFE_TIMEOUT = 500;

// Set true only if a motor runs in the wrong direction
#define INVERT_LEFT_MOTOR false
#define INVERT_RIGHT_MOTOR false

void setMotor(int speed, int rpwm, int lpwm,
              bool inverted) {

  speed = constrain(speed, -255, 255);

  if (inverted) {
    speed = -speed;
  }

  if (speed > 0) {
    analogWrite(lpwm, 0);
    analogWrite(rpwm, speed);
  }
  else if (speed < 0) {
    analogWrite(rpwm, 0);
    analogWrite(lpwm, -speed);
  }
  else {
    analogWrite(rpwm, 0);
    analogWrite(lpwm, 0);
  }
}

void stopMotors() {
  setMotor(0, LEFT_RPWM, LEFT_LPWM,
           INVERT_LEFT_MOTOR);

  setMotor(0, RIGHT_RPWM, RIGHT_LPWM,
           INVERT_RIGHT_MOTOR);
}

void applyMotorSpeeds(int left, int right) {

  left = constrain(left, -255, 255);
  right = constrain(right, -255, 255);

  setMotor(left, LEFT_RPWM, LEFT_LPWM,
           INVERT_LEFT_MOTOR);

  setMotor(right, RIGHT_RPWM, RIGHT_LPWM,
           INVERT_RIGHT_MOTOR);
}

void onDataRecv(const esp_now_recv_info_t *info,
                const uint8_t *data,
                int len) {

  if (memcmp(info->src_addr, transmitterMAC, 6) != 0) {
    return;
  }

  if (len != sizeof(MotorPacket)) {
    return;
  }

  MotorPacket incoming;
  memcpy(&incoming, data, sizeof(incoming));

  if (incoming.magic != MAGIC) {
    return;
  }

  receivedLeft = constrain(incoming.left, -255, 255);
  receivedRight = constrain(incoming.right, -255, 255);
  receivedMode = incoming.mode;

  lastReceiveTime = millis();
  newData = true;
}

void setup() {

  Serial.begin(115200);

  pinMode(LEFT_RPWM, OUTPUT);
  pinMode(LEFT_LPWM, OUTPUT);
  pinMode(LEFT_REN, OUTPUT);
  pinMode(LEFT_LEN, OUTPUT);

  pinMode(RIGHT_RPWM, OUTPUT);
  pinMode(RIGHT_LPWM, OUTPUT);
  pinMode(RIGHT_REN, OUTPUT);
  pinMode(RIGHT_LEN, OUTPUT);

  // ENABLE BOTH BTS7960 DRIVERS
  digitalWrite(LEFT_REN, HIGH);
  digitalWrite(LEFT_LEN, HIGH);

  digitalWrite(RIGHT_REN, HIGH);
  digitalWrite(RIGHT_LEN, HIGH);

  analogWrite(LEFT_RPWM, 0);
  analogWrite(LEFT_LPWM, 0);
  analogWrite(RIGHT_RPWM, 0);
  analogWrite(RIGHT_LPWM, 0);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.print("Receiver MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW INIT FAILED");
    while (true) {
      stopMotors();
      delay(1000);
    }
  }

  esp_now_register_recv_cb(onDataRecv);

  Serial.println("ROBO SOCCER RECEIVER READY");
  stopMotors();
}

void loop() {

  // FAILSAFE: STOP IF TRANSMITTER DISCONNECTS
  if (millis() - lastReceiveTime > FAILSAFE_TIMEOUT) {
    stopMotors();
    return;
  }

  if (newData) {

    noInterrupts();
    int left = receivedLeft;
    int right = receivedRight;
    uint8_t mode = receivedMode;
    newData = false;
    interrupts();

    applyMotorSpeeds(left, right);

    Serial.print("LEFT: ");
    Serial.print(left);

    Serial.print(" | RIGHT: ");
    Serial.print(right);

    Serial.print(" | MODE: ");

    if (mode == 1) {
      Serial.println("SLOW (70%)");
    } else {
      Serial.println("NORMAL (100%)");
    }
  }
}
