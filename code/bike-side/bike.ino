// =======================================
// Bike Side - Receiver Code
// Smart Safety Helmet with H2V
// =======================================

#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ------------------------
// Pin Mapping (Bike Side)
// ------------------------
const int CE_PIN    = 9;     // nRF24L01 CE
const int CSN_PIN   = 10;    // nRF24L01 CSN
const int RELAY_PIN = 7;     // Relay control pin (ignition)

// ------------------------
// nRF24L01 Radio
// ------------------------
RF24 radio(CE_PIN, CSN_PIN);
const byte RADIO_ADDRESS[6] = "H2V01";

// ------------------------
// LCD (I2C)
// ------------------------
// Change 0x27 to 0x3F if LCD does not display
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ------------------------
// Data Packet (must match helmet side)
// ------------------------
struct HelmetStatus {
  uint8_t helmetWorn;        // 1 = helmet worn, 0 = not worn
  uint8_t accidentDetected;  // 1 = accident, 0 = normal
  uint16_t alcoholRaw;       // raw MQ-3 value (0–1023)
};

HelmetStatus status;

// ------------------------
// Alcohol Calculation
// ------------------------
// Approximate calibration constants for MQ-3
// Use Serial Monitor to tune for your sensor.
const int MQ3_BASELINE = 200;    // Clean air analog value (approx)
const int MQ3_MAX      = 800;    // Strong alcohol reading (approx)

// Legal BAC limit (India / TN) ~ 0.03%
// We'll compare estimated BAC against this.
const float BAC_LEGAL_LIMIT = 0.03f;   // 0.03%

// If no packets received for this duration -> assume link lost
const unsigned long LINK_TIMEOUT_MS = 1000;
unsigned long lastPacketTime = 0;

// =======================================
// Helper: Estimate BAC from raw MQ-3 value
// =======================================
float estimateBAC(uint16_t alcoholRaw) {
  // Constrain sensor reading to expected range
  int val = constrain((int)alcoholRaw, MQ3_BASELINE, MQ3_MAX);

  // Normalize between 0 and 1
  float norm = (float)(val - MQ3_BASELINE) / (float)(MQ3_MAX - MQ3_BASELINE);

  // Map to 0.00%–0.10% (example)
  // You can adjust 0.10f based on experiments.
  float bac = norm * 0.10f;

  if (bac < 0.0f) bac = 0.0f;
  return bac;
}

// =======================================
// Helper: Format BAC for LCD (xx.xx%)
// =======================================
void formatBAC(char *buffer, float bac) {
  // Convert float to "0.02%" style text
  int bacInt = (int)(bac * 10000); // e.g., 0.03 -> 300
  int whole  = bacInt / 100;       // 300 -> 3
  int frac   = bacInt % 100;       // 300 -> 0

  // Format as "0.03%" type string
  sprintf(buffer, "%d.%02d%%", whole, frac);
}

// =======================================
// LCD Helper
// =======================================
void showLCD(const char *line1, const char *line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

// =======================================
// SETUP
// =======================================
void setup() {
  Serial.begin(9600);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);   // Engine OFF by default

  // LCD Initialization
  Wire.begin();
  lcd.init();
  lcd.backlight();
  showLCD("H2V Bike Unit", "Waiting Helmet");

  // Radio Initialization
  radio.begin();
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_1MBPS);
  radio.openReadingPipe(1, RADIO_ADDRESS);
  radio.startListening();

  Serial.println("Bike RX: Ready");
}

// =======================================
// MAIN LOOP
// =======================================
void loop() {
  // Check for incoming packet from helmet
  if (radio.available()) {
    radio.read(&status, sizeof(status));
    lastPacketTime = millis();  // Update last link time

    // --- Decode fields ---
    bool helmetWorn      = (status.helmetWorn == 1);
    bool accidentDetected = (status.accidentDetected == 1);
    uint16_t alcoholRaw   = status.alcoholRaw;

    // Estimate BAC from MQ-3 reading
    float bac = estimateBAC(alcoholRaw);
    bool bacAboveLimit = (bac > BAC_LEGAL_LIMIT);

    // Prepare BAC string for LCD
    char bacStr[10];
    formatBAC(bacStr, bac);

    // Debug to Serial Monitor
    Serial.print("Helmet: "); Serial.print(helmetWorn);
    Serial.print(" | Accident: "); Serial.print(accidentDetected);
    Serial.print(" | MQ3 Raw: "); Serial.print(alcoholRaw);
    Serial.print(" | BAC est: "); Serial.println(bacStr);

    // ----------------------
    // Decision Priority:
    // 1. Accident -> Stop engine
    // 2. Helmet not worn -> Stop engine
    // 3. BAC above legal limit -> Stop engine
    // 4. All safe -> Engine on
    // ----------------------

    if (accidentDetected) {
      // Accident: engine must be stopped immediately
      digitalWrite(RELAY_PIN, LOW);
      showLCD("Accident Detected", "Engine Stopped");
    }
    else if (!helmetWorn) {
      // Helmet not worn: do not allow ignition
      digitalWrite(RELAY_PIN, LOW);
      showLCD("Helmet Not Worn", "Wear Helmet");
    }
    else if (bacAboveLimit) {
      // Alcohol above TN legal limit (0.03%)
      digitalWrite(RELAY_PIN, LOW);

      // First line: show BAC
      // Second line: show warning
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("BAC: ");
      lcd.print(bacStr);         // e.g. "0.04%"
      lcd.setCursor(0, 1);
      lcd.print("Above Limit!"); // TN Reg: 0.03%

    } else {
      // All safe: Helmet worn, no accident, BAC below limit
      digitalWrite(RELAY_PIN, HIGH);

      // Show BAC + engine allowed
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("BAC: ");
      lcd.print(bacStr);         // e.g. "0.01%"

      lcd.setCursor(0, 1);
      lcd.print("All Safe - GO"); // Engine ON permitted
    }
  }

  // ----------------------
  // Link Timeout Handling
  // If no packets from helmet for some time -> assume link lost
  // ----------------------
  if (millis() - lastPacketTime > LINK_TIMEOUT_MS) {
    digitalWrite(RELAY_PIN, LOW);  // Stop engine as fail-safe
    showLCD("No Helmet Link", "Ignition Locked");
  }

  delay(100);
}
