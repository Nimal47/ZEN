#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>

// --- RFID PINS ---
#define RFID_CS_PIN  21  
#define RFID_RST_PIN 22

MFRC522 mfrc522(RFID_CS_PIN, RFID_RST_PIN);

// --- STATE MACHINE ---
enum JailState {
  WAITING_FOR_PHONE,
  JAIL_ACTIVE,
  GRACE_PERIOD
};

// Helper function to cleanly ping the card
bool checkCardPresent() {
  byte bufferATQA[2];
  byte bufferSize = sizeof(bufferATQA);
  
  // Send the Wake Up command
  if (mfrc522.PICC_WakeupA(bufferATQA, &bufferSize) == MFRC522::STATUS_OK) {
    // If found, read and halt to prevent jamming
    if (mfrc522.PICC_ReadCardSerial()) {
      mfrc522.PICC_HaltA(); 
    }
    return true;
  }
  return false;
}

void rfidJailTestTask(void *parameter) {
  SPI.begin(18, 19, 23, RFID_CS_PIN); 
  mfrc522.PCD_Init();
  
  Serial.println("🛡️ Phone Jail Timer Ready (30-second polling).");
  Serial.println("Place your phone or card on the reader to start...");

  JailState state = WAITING_FOR_PHONE;
  TickType_t jailStartTime = 0;
  TickType_t lastScanTime = 0;
  TickType_t lastConfirmedTime = 0;
  TickType_t graceStartTime = 0;

  for (;;) {
    TickType_t currentTick = xTaskGetTickCount();

    switch (state) {
      
      // ---------------------------------------------------------
      // STATE 1: Waiting for phone to be placed
      // ---------------------------------------------------------
      case WAITING_FOR_PHONE:
        if (checkCardPresent()) {
          state = JAIL_ACTIVE;
          jailStartTime = currentTick;
          lastScanTime = currentTick;
          lastConfirmedTime = currentTick;
          Serial.println("\n🔒 Phone Locked! Checking presence every 30 seconds...");
        }
        vTaskDelay(pdMS_TO_TICKS(200)); // Fast poll to catch insertion quickly
        break;

      // ---------------------------------------------------------
      // STATE 2: Phone is in jail. Rest for 30s, then check.
      // ---------------------------------------------------------
      case JAIL_ACTIVE:
        // Wait until 30 seconds have passed since the last scan
        if (currentTick - lastScanTime >= pdMS_TO_TICKS(30000)) {
          lastScanTime = currentTick; // Reset the 30s timer
          
          if (checkCardPresent()) {
            lastConfirmedTime = currentTick; // Update our "safe" time
            int elapsed = (currentTick - jailStartTime) / 1000;
            Serial.printf("⏱️ Check passed. Focusing for %d seconds...\n", elapsed);
          } else {
            // Failed the 30s check! Enter the grace period.
            state = GRACE_PERIOD;
            graceStartTime = currentTick;
            Serial.println("⚠️ Phone missing! Verifying (2s grace period)...");
          }
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // Keep task responsive, but don't ping RFID
        break;

      // ---------------------------------------------------------
      // STATE 3: Phone might be gone. Rapid fire scan for 2 seconds.
      // ---------------------------------------------------------
      case GRACE_PERIOD:
        if (checkCardPresent()) {
          // False alarm! The phone is back (or the reader just glitched).
          state = JAIL_ACTIVE;
          lastConfirmedTime = currentTick;
          lastScanTime = currentTick; // Reset the 30s timer
          Serial.println("😌 False alarm recovered. Continuing session...");
        } 
        else if (currentTick - graceStartTime >= pdMS_TO_TICKS(2000)) {
          // 2 seconds have passed and it's still missing. Officially removed.
          state = WAITING_FOR_PHONE;
          
          // Calculate time based on the LAST time we knew for a fact it was there
          int totalSeconds = (lastConfirmedTime - jailStartTime) / 1000;
          
          Serial.println("\n🔓 Phone Removed!");
          Serial.printf("🏆 Total Session Time: %d seconds.\n", totalSeconds);
          Serial.println("----------------------------------------");
        }
        vTaskDelay(pdMS_TO_TICKS(200)); // Aggressive scanning during grace period
        break;
    }
  }
}

void setup() {
  Serial.begin(115200);
  xTaskCreatePinnedToCore(rfidJailTestTask, "RFID_Jail", 4096, NULL, 1, NULL, 1);
  vTaskDelete(NULL);
}

void loop() {
  // Main loop remains empty and unblocked
}






