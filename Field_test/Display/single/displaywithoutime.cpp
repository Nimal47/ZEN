#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <WiFi.h>
#include "time.h"

#include "/Users/nimal/Documents/PROJEC/MP3/goodman.h" //path to Image Header File in C array Format
#include "/Users/nimal/Documents/PROJEC/ADIS/src/secret.h" //path to your Credential folder 

// --- WI-FI CREDENTIALS ---
const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// --- PINS ---
#define TFT_CS   5
#define TFT_DC   2
#define TFT_RST  4
#define PIR_PIN  27

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

#define CYBERPUNK_HEIGHT 160
#define CYBERPUNK_WIDTH 128

// --- RTOS SHARED VARIABLES ---
volatile TickType_t lastMotionTick = 0; 

enum DisplayState {
  STATE_TIME,           // Showing Clock (0-3s)
  STATE_IMAGE,          // Showing Image (3-10s)
  STATE_SLEEPING_TEXT,  // Showing "Sleeping..." (10-12s)
  STATE_ASLEEP          // Screen Black (>12s)
};

// --- TASK 1: THE NETWORK MANAGER (CORE 0) ---
void networkTask(void *parameter) {
  // 1. Connect to Wi-Fi
  Serial.print("Connecting to Wi-Fi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(500));
    Serial.print(".");
  }
  Serial.println("\nWi-Fi Connected!");

  // 2. Configure NTP Time (19800 seconds = +5:30 UTC for IST)
  configTime(19800, 0, "time.google.com", "pool.ntp.org");
  Serial.println("Time synchronized with Google NTP.");

  // This task has finished its setup job, so we delete it to free memory.
  // Later, we will use this task for MQTT!
  vTaskDelete(NULL); 
}

// --- TASK 2: THE WATCHER (CORE 1) ---
void pirTask(void *parameter) {
  pinMode(PIR_PIN, INPUT);
  
  for (;;) {
    if (digitalRead(PIR_PIN) == HIGH) {
      lastMotionTick = xTaskGetTickCount(); 
    }
    vTaskDelay(pdMS_TO_TICKS(100)); 
  }
}

// --- TASK 3: THE SCREEN MANAGER (CORE 1) ---
void displayTask(void *parameter) {
  SPI.begin(18, 19, 23, TFT_CS);  
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);

  DisplayState currentState = STATE_ASLEEP; 
  int lastSecond = -1; // Used to prevent screen flickering when drawing the clock

  for (;;) {
    TickType_t currentTick = xTaskGetTickCount();
    TickType_t timeSinceMotion = currentTick - lastMotionTick;

    if (timeSinceMotion < pdMS_TO_TICKS(10000)) {
      if (currentState != STATE_IMAGE) {
        tft.fillScreen(ST77XX_BLACK);
        tft.drawRGBBitmap(15, 0, goodmannn, CYBERPUNK_WIDTH, CYBERPUNK_HEIGHT);
        currentState = STATE_IMAGE;
      }
    } 
    // RULE 3: 10 to 12 seconds -> Sleeping Warning
    else if (timeSinceMotion < pdMS_TO_TICKS(12000)) {
      if (currentState != STATE_SLEEPING_TEXT) {
        tft.fillScreen(ST77XX_BLACK);
        tft.setCursor(20, 60);       
        tft.setTextColor(ST77XX_WHITE);
        tft.setTextSize(2);
        tft.print("Sleeping...");
        currentState = STATE_SLEEPING_TEXT;
      }
    } 
    // RULE 4: After 12 seconds -> Deep Sleep
    else {
      if (currentState != STATE_ASLEEP) {
        tft.fillScreen(ST77XX_BLACK);
        currentState = STATE_ASLEEP;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100)); 
  }
}

// --- MAIN CHEF ---
void setup() {
  Serial.begin(115200);

  // Network on Core 0
  xTaskCreatePinnedToCore(networkTask, "Network", 4096, NULL, 2, NULL, 0);
  
  // Hardware on Core 1
  xTaskCreatePinnedToCore(pirTask, "PIR", 2048, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(displayTask, "Display", 4096, NULL, 1, NULL, 1);

  vTaskDelete(NULL);
}

void loop() {
}