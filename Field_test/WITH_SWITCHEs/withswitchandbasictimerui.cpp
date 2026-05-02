#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <MFRC522.h>
#include <WiFi.h>
#include "time.h"

#include "/Users/nimal/Documents/PROJEC/MP3/goodman.h" //path to Image Header File in C array Format
#include "/Users/nimal/Documents/PROJEC/ADIS/src/secret.h" //path to your Credential folder 

// --- WI-FI CREDENTIALS ---
const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;
// --- PIN DEFINITIONS ---
#define TFT_CS      5
#define TFT_DC      2
#define TFT_RST     4
#define PIR_PIN     27
#define RFID_CS     21
#define RFID_RST    22
#define BTN_MODE    32 // New Button 1
#define BTN_TIMER   33 // New Button 2

// --- HARDWARE OBJECTS ---
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
MFRC522 mfrc522(RFID_CS, RFID_RST);

// --- SHARED RTOS VARIABLES & LOCKS ---
volatile TickType_t lastMotionTick = 0; 
SemaphoreHandle_t spiMutex;

// System Modes
enum SystemMode {
  MODE_DEFAULT,
  MODE_TIMER,
  MODE_REMINDER
};
volatile SystemMode currentMode = MODE_DEFAULT;
volatile int timerMinutes = 5; // Tracks the 5, 15, 30, 60 cycle

// Display States to prevent flickering
enum DisplayState {
  STATE_AWAKE_DEFAULT,
  STATE_AWAKE_TIMER,
  STATE_AWAKE_REMINDER,
  STATE_SLEEPING_TEXT,
  STATE_ASLEEP
};

// =======================================================
// TASK 1: NETWORK MANAGER
// =======================================================
void networkTask(void *parameter) {
  Serial.print("Connecting to Wi-Fi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(500));
    Serial.print(".");
  }
  Serial.println("\nWi-Fi Connected!");
  configTime(19800, 0, "time.google.com", "pool.ntp.org");
  vTaskDelete(NULL); 
}

// =======================================================
// TASK 2: PIR WATCHER
// =======================================================
void pirTask(void *parameter) {
  pinMode(PIR_PIN, INPUT);
  for (;;) {
    if (digitalRead(PIR_PIN) == HIGH) {
      lastMotionTick = xTaskGetTickCount(); 
    }
    vTaskDelay(pdMS_TO_TICKS(100)); 
  }
}

// =======================================================
// TASK 3: BUTTON MANAGER (NEW)
// =======================================================
void buttonTask(void *parameter) {
  pinMode(BTN_MODE, INPUT_PULLUP);
  pinMode(BTN_TIMER, INPUT_PULLUP);

  int lastModeState = HIGH;
  int lastTimerState = HIGH;

  for (;;) {
    int modeState = digitalRead(BTN_MODE);
    int timerState = digitalRead(BTN_TIMER);

    // --- BUTTON 1: CHANGE MODE ---
    if (modeState == LOW && lastModeState == HIGH) {
      currentMode = (SystemMode)((currentMode + 1) % 3); // Cycles 0 -> 1 -> 2 -> 0
      lastMotionTick = xTaskGetTickCount(); // Wake up the screen to show the new mode
      Serial.printf("🔘 Mode Switched: %d\n", currentMode);
    }
    lastModeState = modeState;

    // --- BUTTON 2: CHANGE TIMER ---
    if (timerState == LOW && lastTimerState == HIGH) {
      // Only do something if we are actually in Timer Mode
      if (currentMode == MODE_TIMER) {
        if (timerMinutes == 5) timerMinutes = 15;
        else if (timerMinutes == 15) timerMinutes = 30;
        else if (timerMinutes == 30) timerMinutes = 60;
        else timerMinutes = 5; // Loop back to 5
        
        lastMotionTick = xTaskGetTickCount(); // Wake up the screen to show the new time
        Serial.printf("⏱️ Timer Set To: %d min\n", timerMinutes);
      }
    }
    lastTimerState = timerState;

    // 50ms delay acts as a natural hardware debounce so a single press isn't read twice
    vTaskDelay(pdMS_TO_TICKS(50)); 
  }
}

// =======================================================
// TASK 4: PHONE JAIL RFID
// =======================================================
void rfidTask(void *parameter) {
  vTaskDelay(pdMS_TO_TICKS(1000));
  
  if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
    mfrc522.PCD_Init();
    xSemaphoreGive(spiMutex);
  }
  
  bool isPhoneInJail = false;
  TickType_t jailStartTime = 0;
  TickType_t lastCardSeenTime = 0;

  for (;;) {
    TickType_t currentTick = xTaskGetTickCount();
    MFRC522::StatusCode status;
    byte bufferATQA[2];
    byte bufferSize = sizeof(bufferATQA);

    if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
      status = mfrc522.PICC_WakeupA(bufferATQA, &bufferSize);
      if (status == MFRC522::STATUS_OK && mfrc522.PICC_ReadCardSerial()) {
        mfrc522.PICC_HaltA(); 
      }
      xSemaphoreGive(spiMutex);
    }

    if (status == MFRC522::STATUS_OK) {
      lastCardSeenTime = currentTick;
      if (!isPhoneInJail) {
        isPhoneInJail = true;
        jailStartTime = currentTick;
        Serial.println("\n🔒 Phone Locked in Jail!");
      }
    } else {
      if (isPhoneInJail && (currentTick - lastCardSeenTime > pdMS_TO_TICKS(2000))) {
        isPhoneInJail = false;
        TickType_t totalTicks = currentTick - jailStartTime - pdMS_TO_TICKS(2000);
        int focusSeconds = totalTicks / 1000;
        Serial.printf("🔓 Phone Removed! Focused for %02d:%02d\n", focusSeconds / 60, focusSeconds % 60);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(500)); 
  }
}

// =======================================================
// TASK 5: DISPLAY MANAGER
// =======================================================
void displayTask(void *parameter) {
  vTaskDelay(pdMS_TO_TICKS(500));
  
  if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
    tft.initR(INITR_BLACKTAB);
    tft.setRotation(1);
    tft.fillScreen(ST77XX_BLACK);
    xSemaphoreGive(spiMutex);
  }

  DisplayState currentState = STATE_ASLEEP; 
  int lastDrawnTimer = -1; 

  for (;;) {
    TickType_t currentTick = xTaskGetTickCount();
    TickType_t timeSinceMotion = currentTick - lastMotionTick;

    // RULE 1: AWAKE (0 to 10 seconds) -> Content depends on Mode!
    if (timeSinceMotion < pdMS_TO_TICKS(10000)) {
      
      // --- MODE 0: CYBERPUNK IMAGE ---
      if (currentMode == MODE_DEFAULT) {
        if (currentState != STATE_AWAKE_DEFAULT) {
          if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
            tft.fillScreen(ST77XX_BLACK);
            tft.drawRGBBitmap(15, 0, goodmannn, 128, 160);
            xSemaphoreGive(spiMutex);
          }
          currentState = STATE_AWAKE_DEFAULT;
        }
      }
      
      // --- MODE 1: TIMER SETUP ---
      else if (currentMode == MODE_TIMER) {
        // Redraw if we just switched to this mode, OR if the user pressed the timer button
        if (currentState != STATE_AWAKE_TIMER || lastDrawnTimer != timerMinutes) {
          if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
            tft.fillScreen(ST77XX_BLACK);
            tft.setCursor(10, 50);
            tft.setTextColor(ST77XX_GREEN);
            tft.setTextSize(2);
            tft.print("TIMER MODE");
            
            tft.setCursor(20, 80);
            tft.setTextColor(ST77XX_WHITE);
            tft.setTextSize(3);
            tft.printf("%02d MIN", timerMinutes);
            xSemaphoreGive(spiMutex);
          }
          currentState = STATE_AWAKE_TIMER;
          lastDrawnTimer = timerMinutes;
        }
      }
      
      // --- MODE 2: REMINDER ---
      else if (currentMode == MODE_REMINDER) {
        if (currentState != STATE_AWAKE_REMINDER) {
          if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
            tft.fillScreen(ST77XX_BLACK);
            tft.setCursor(15, 60);
            tft.setTextColor(ST77XX_BLUE);
            tft.setTextSize(2);
            tft.print("REMINDERS");
            tft.setCursor(15, 90);
            tft.setTextSize(1);
            tft.setTextColor(ST77XX_WHITE);
            tft.print("(Coming Soon)");
            xSemaphoreGive(spiMutex);
          }
          currentState = STATE_AWAKE_REMINDER;
        }
      }
    } 
    
    // RULE 2: SLEEPING WARNING (10 to 12 seconds)
    else if (timeSinceMotion < pdMS_TO_TICKS(12000)) {
      if (currentState != STATE_SLEEPING_TEXT) {
        if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
          tft.fillScreen(ST77XX_BLACK);
          tft.setCursor(20, 60);       
          tft.setTextColor(ST77XX_WHITE);
          tft.setTextSize(2);
          tft.print("Sleeping...");
          xSemaphoreGive(spiMutex);
        }
        currentState = STATE_SLEEPING_TEXT;
      }
    } 
    
    // RULE 3: DEEP SLEEP (After 12 seconds)
    else {
      if (currentState != STATE_ASLEEP) {
        if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
          tft.fillScreen(ST77XX_BLACK);
          xSemaphoreGive(spiMutex);
        }
        currentState = STATE_ASLEEP;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100)); 
  }
}

// =======================================================
// MAIN SETUP
// =======================================================
void setup() {
  Serial.begin(115200);
  
  spiMutex = xSemaphoreCreateMutex();
  SPI.begin(18, 19, 23); 

  if (spiMutex != NULL) {
    xTaskCreatePinnedToCore(networkTask, "Network", 4096, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(pirTask,     "PIR",     2048, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(buttonTask,  "Buttons", 2048, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(rfidTask,    "RFID",    4096, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(displayTask, "Display", 8192, NULL, 1, NULL, 1);
  }
  
  vTaskDelete(NULL);
}

void loop() {}