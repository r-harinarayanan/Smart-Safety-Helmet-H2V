// ===============================
// Helmet Side - Transmitter Code
// Smart Safety Helmet with H2V
// ===============================

#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <Wire.h>

// ------------------------
// Pin Mapping (Helmet Side)
// ------------------------
const int FSR_PIN   = A1;  // FSR for helmet wear detection
const int MQ3_PIN   = A0;  // MQ-3 alcohol sensor
const int CE_PIN    = 9;   // nRF24L01 CE
const int CSN_PIN   = 10;  // nRF24L01 CSN

// ------------------------
// nRF24L01 Radio
// ------------------------
RF24 radio(CE_PIN, CSN_PIN);
const byte RADIO_ADDRESS[6] = "H2V01";   // 5-byte pipe name + '\0'

// ------------------------
// Thresholds (to be tuned)
// ------------------------
// These values are example starting points. Use Serial Monitor to tune.
const int FSR_THRESHOLD = 300;   // If FSR reading > this => helmet worn
const int MQ3_BASELINE  = 200;   // MQ-3 reading in clean air (approx)
const int MQ3_MAX       = 800;   // MQ-3 high reading when strong alcohol (approx)

// Rough accident detection threshold - you should tune using Serial Monitor
const long ACCEL_MAG_THRESHOLD = 20000;  

// ------------------------
// Data Packet Structure
// This must match exactly on BOTH helmet and bike side.
// ------------------------
struct HelmetStatus {
  uint8_t helmetWorn;        // 1 = helmet worn, 0 = not worn
  uint8_t accidentDetected;  // 1 = accident detected, 0 = normal
  uint16_t alcoholRaw;       // raw MQ-3 analog value (0–1023)
};

// Global packet instance
HelmetStatus status;

// MPU6050 I2C address
const uint8_t MPU_ADDR = 0x68;

// ===============================
// MPU6050 Initialization & Read
// ===============================
void initMPU6050() {
  Wire.begin();
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);     // Power management register
  Wire.write(0);        // Wake up MPU6050
  Wire.endTransmission(true);
}

void readAccelRaw(int16_t &AcX, int16_t &AcY, int16_t &AcZ) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);     // Start with ACCEL_XOUT_H
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 6, true);

  AcX = (Wire.read() << 8) | Wire.read();
  AcY = (Wire.read() << 8) | Wire.read();
  AcZ = (Wire.read() << 8) | Wire.read();
}

// Very rough accident detection using acceleration magnitude
bool detectAccident() {
  int16_t AcX, AcY, AcZ;
  readAccelRaw(AcX, AcY, AcZ);

  long magSq = (long)AcX * AcX + (long)AcY * AcY + (long)AcZ * AcZ;
  return (magSq > (long)ACCEL_MAG_THRESHOLD * ACCEL_MAG_THRESHOLD);
}

// ===============================
// SETUP
// ===============================
void setup() {
  Serial.begin(9600);

  pinMode(FSR_PIN, INPUT);
  pinMode(MQ3_PIN, INPUT);

  // Initialize MPU6050
  initMPU6050();

  // Initialize nRF24L01
  radio.begin();
  radio.setPALevel(RF24_PA_LOW);      // Low power to reduce noise
  radio.setDataRate(RF24_1MBPS);      // 1 Mbps is stable
  radio.openWritingPipe(RADIO_ADDRESS);
  radio.stopListening();              // Set as Transmitter

  Serial.println("Helmet TX: Ready");
}

// ===============================
// LOOP
// ===============================
void loop() {
  // Read sensors
  int fsrValue = analogRead(FSR_PIN);
  int mq3Value = analogRead(MQ3_PIN);
  bool helmetWornFlag = (fsrValue > FSR_THRESHOLD);
  bool accidentFlag   = detectAccident();

  // Fill status packet
  status.helmetWorn       = helmetWornFlag ? 1 : 0;
  status.accidentDetected = accidentFlag ? 1 : 0;
  status.alcoholRaw       = (uint16_t)mq3Value;

  // Debug output on Serial Monitor
  Serial.print("FSR: "); Serial.print(fsrValue);
  Serial.print(" | MQ3: "); Serial.print(mq3Value);
  Serial.print(" | Helmet: "); Serial.print(status.helmetWorn);
  Serial.print(" | Accident: "); Serial.println(status.accidentDetected);

  // Transmit packet to bike
  bool ok = radio.write(&status, sizeof(status));
  if (!ok) {
    Serial.println("Radio TX failed");
  }

  delay(200);  // Send about 5 packets per second (200 ms interval)
}
