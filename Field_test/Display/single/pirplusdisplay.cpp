#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

#include "/Users/nimal/Documents/PROJEC/MP3/goodman.h" //path to Image Header File in C array Format
#include "/Users/nimal/Documents/PROJEC/ADIS/src/secret.h" //path to your Credential folder 

// --- WI-FI CREDENTIALS ---
const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// --- PINS ---
#define TFT_CS   5
#define TFT_DC   2
#define TFT_RST  4
#define PIR_PIN  27 // CHANGED FROM 23 TO AVOID SPI CONFLICT!

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

#define CYBERPUNK_HEIGHT 160
#define CYBERPUNK_WIDTH 128

// --- RTOS SHARED VARIABLES ---
// 'volatile' tells the compiler this variable can change at any time by different tasks
volatile TickType_t lastMotionTick = 0; 

// Display States
enum DisplayState {
  STATE_AWAKE,          // Showing Image
  STATE_SLEEPING_TEXT,  // Showing "Sleeping..."
  STATE_ASLEEP          // Screen Black
};

// --- TASK 1: THE WATCHER ---
void pirTask(void *parameter) {
  pinMode(PIR_PIN, INPUT);
  
  for (;;) {
    if (digitalRead(PIR_PIN) == HIGH) {
      // Record the exact OS tick when motion was last detected
      lastMotionTick = xTaskGetTickCount(); 
      Serial.println("🚶 Motion Detected! Resetting timer.");
    }
    
    // Check the sensor frequently (every 100ms)
    vTaskDelay(pdMS_TO_TICKS(100)); 
  }
}

// --- TASK 2: THE SCREEN MANAGER ---
void displayTask(void *parameter) {
  // Initialize screen
  SPI.begin(18, 19, 23, TFT_CS);  
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);

  // We start in a 'Force Update' state so it draws immediately on boot
  DisplayState currentState = STATE_ASLEEP; 

  for (;;) {
    // How much time has passed since the PIR sensor last saw you?
    TickType_t currentTick = xTaskGetTickCount();
    TickType_t timeSinceMotion = currentTick - lastMotionTick;

    // RULE 1: If motion was within the last 10 seconds (10,000 ms)
    if (timeSinceMotion < pdMS_TO_TICKS(10000)) {
      if (currentState != STATE_AWAKE) {
        Serial.println("💻 Display: Waking up! Showing image.");
        tft.fillScreen(ST77XX_BLACK);
        tft.drawRGBBitmap(15, 0, goodmannn, CYBERPUNK_WIDTH, CYBERPUNK_HEIGHT);
        currentState = STATE_AWAKE;
      }
    } 
    // RULE 2: Between 10 and 12 seconds (No motion for 10s)
    else if (timeSinceMotion < pdMS_TO_TICKS(12000)) {
      if (currentState != STATE_SLEEPING_TEXT) {
        Serial.println("💻 Display: 10s timeout. Going to sleep soon...");
        tft.fillScreen(ST77XX_BLACK);
        tft.setCursor(20, 60);       // Center text roughly
        tft.setTextColor(ST77XX_WHITE);
        tft.setTextSize(2);
        tft.print("Sleeping...");
        currentState = STATE_SLEEPING_TEXT;
      }
    } 
    // RULE 3: After 12 seconds
    else {
      if (currentState != STATE_ASLEEP) {
        Serial.println("💻 Display: Screen OFF.");
        tft.fillScreen(ST77XX_BLACK);
        currentState = STATE_ASLEEP;
      }
    }

    // This task doesn't need to run constantly, 100ms pauses are fine
    vTaskDelay(pdMS_TO_TICKS(100)); 
  }
}

// --- MAIN CHEF ---
void setup() {
  Serial.begin(115200);
  Serial.println("Starting ADIS Phase 1...");

  // Boot up the tasks!
  xTaskCreatePinnedToCore(pirTask, "PIR", 2048, NULL, 1, NULL, 1);
  
  // The display needs a bit more memory (4096) to handle the image
  xTaskCreatePinnedToCore(displayTask, "Display", 4096, NULL, 1, NULL, 1);

  // Hand over control to FreeRTOS
  vTaskDelete(NULL);
}

void loop() {
}