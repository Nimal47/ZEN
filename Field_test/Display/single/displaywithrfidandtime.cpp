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
#define TFT_CS   5
#define TFT_DC   2
#define TFT_RST  4
#define PIR_PIN  27
#define RFID_CS  21
#define RFID_RST 22

// --- HARDWARE OBJECTS ---
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
MFRC522 mfrc522(RFID_CS, RFID_RST);

// --- SHARED RTOS VARIABLES & LOCKS ---
volatile TickType_t lastMotionTick = 0; 
SemaphoreHandle_t spiMutex; // CRITICAL: Protects the shared SPI pins (18, 19, 23)

enum DisplayState {
  STATE_TIME,
  STATE_IMAGE,
  STATE_SLEEPING_TEXT,
  STATE_ASLEEP
};

// =======================================================
// TASK 1: NETWORK MANAGER (Runs on Core 0)
// =======================================================
void networkTask(void *parameter) {
  Serial.print("Connecting to Wi-Fi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(500));
    Serial.print(".");
  }
  Serial.println("\nWi-Fi Connected!");
  
  // Set time to Indian Standard Time (UTC +5:30 = 19800 seconds)
  configTime(19800, 0, "time.google.com", "pool.ntp.org");
  Serial.println("Time synchronized with Google NTP.");
  
  vTaskDelete(NULL); 
}

// =======================================================
// TASK 2: PIR WATCHER (Runs on Core 1)
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
// TASK 3: PHONE JAIL RFID (Runs on Core 1)
// =======================================================
void rfidTask(void *parameter) {
  // Give the main setup a moment to initialize the SPI bus
  vTaskDelay(pdMS_TO_TICKS(1000));
  
  // Safely take the lock to initialize the RFID chip
  if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
    mfrc522.PCD_Init();
    xSemaphoreGive(spiMutex);
  }
  
  Serial.println("🛡️ Phone Jail Timer Ready.");

  bool isPhoneInJail = false;
  TickType_t jailStartTime = 0;
  TickType_t lastCardSeenTime = 0;

  for (;;) {
    TickType_t currentTick = xTaskGetTickCount();
    MFRC522::StatusCode status;
    byte bufferATQA[2];
    byte bufferSize = sizeof(bufferATQA);

    // --- CRITICAL SECTION: LOCK SPI BUS TO READ RFID ---
    if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
      status = mfrc522.PICC_WakeupA(bufferATQA, &bufferSize);
      if (status == MFRC522::STATUS_OK) {
        if (mfrc522.PICC_ReadCardSerial()) {
          mfrc522.PICC_HaltA(); 
        }
      }
      xSemaphoreGive(spiMutex); // --- RELEASE LOCK ---
    }

    // Process the results
    if (status == MFRC522::STATUS_OK) {
      lastCardSeenTime = currentTick;
      if (!isPhoneInJail) {
        isPhoneInJail = true;
        jailStartTime = currentTick;
        Serial.println("\n🔒 Phone Locked in Jail! Focus time started.");
      }
    } 
    else {
      // 2-second timeout buffer to detect removal
      if (isPhoneInJail && (currentTick - lastCardSeenTime > pdMS_TO_TICKS(2000))) {
        isPhoneInJail = false;
        TickType_t totalTicks = currentTick - jailStartTime - pdMS_TO_TICKS(2000);
        int focusSeconds = totalTicks / 1000;
        int focusMinutes = focusSeconds / 60;
        int remainingSeconds = focusSeconds % 60;
        
        Serial.println("\n🔓 Phone Removed!");
        Serial.printf("🏆 Total Focus Session: %02d minutes and %02d seconds.\n", focusMinutes, remainingSeconds);
      }
    }
    
    // Check for the phone twice a second
    vTaskDelay(pdMS_TO_TICKS(500)); 
  }
}

// =======================================================
// TASK 4: DISPLAY MANAGER (Runs on Core 1)
// =======================================================
void displayTask(void *parameter) {
  vTaskDelay(pdMS_TO_TICKS(500));
  
  // Safely take the lock to initialize the Screen
  if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
    tft.initR(INITR_BLACKTAB);
    tft.setRotation(1);
    tft.fillScreen(ST77XX_BLACK);
    xSemaphoreGive(spiMutex);
  }

  DisplayState currentState = STATE_ASLEEP; 
  int lastSecond = -1; 

  for (;;) {
    TickType_t currentTick = xTaskGetTickCount();
    TickType_t timeSinceMotion = currentTick - lastMotionTick;

    // RULE 1: Time (0 to 3 seconds)
    if (timeSinceMotion < pdMS_TO_TICKS(3000)) {
      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) {
        if (currentState != STATE_TIME || timeinfo.tm_sec != lastSecond) {
          
          // --- LOCK SPI BUS TO DRAW ON SCREEN ---
          if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
            if (currentState != STATE_TIME) tft.fillScreen(ST77XX_BLACK); 
            tft.setCursor(15, 60);
            tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK); 
            tft.setTextSize(2);
            tft.printf("%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            xSemaphoreGive(spiMutex); // --- RELEASE LOCK ---
          }
          
          lastSecond = timeinfo.tm_sec;
          currentState = STATE_TIME;
        }
      }
    } 
    // RULE 2: Image (3 to 10 seconds)
    else if (timeSinceMotion < pdMS_TO_TICKS(10000)) {
      if (currentState != STATE_IMAGE) {
        if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
          tft.fillScreen(ST77XX_BLACK);
          tft.drawRGBBitmap(15, 0, goodmannn, 128, 160);
          xSemaphoreGive(spiMutex);
        }
        currentState = STATE_IMAGE;
      }
    } 
    // RULE 3: Sleeping Warning (10 to 12 seconds)
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
    // RULE 4: Deep Sleep (After 12 seconds)
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
  
  // 1. Create the Mutex to protect the SPI bus
  spiMutex = xSemaphoreCreateMutex();

  // 2. Initialize the shared SPI bus ONCE (SCK=18, MISO=19, MOSI=23)
  // We leave the CS pin empty here because each device manages its own CS pin.
  SPI.begin(18, 19, 23); 

  // 3. Create all Tasks if the Mutex was created successfully
  if (spiMutex != NULL) {
    xTaskCreatePinnedToCore(networkTask, "Network", 4096, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(pirTask,     "PIR",     2048, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(rfidTask,    "RFID",    4096, NULL, 1, NULL, 1);
    
    // Display needs a bit more memory (8192) to prevent stack overflows when drawing
    xTaskCreatePinnedToCore(displayTask, "Display", 8192, NULL, 1, NULL, 1);
  }
  
  // 4. Delete the default Arduino loop to hand control entirely to FreeRTOS
  vTaskDelete(NULL);
}

void loop() {
  // Empty
}