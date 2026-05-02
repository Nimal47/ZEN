#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <MFRC522.h>
#include <WiFi.h>
#include "time.h"
#include <Adafruit_SSD1306.h>
#include <Firebase_ESP_Client.h>
#include <ArduinoJson.h>
#include "addons/RTDBHelper.h"
#include "/Users/nimal/Documents/PROJEC/MP3/goodman.h"

// --- WI-FI CREDENTIALS ---
#include "/Users/nimal/Documents/PROJEC/MP3/goodman.h" //path to Image Header File in C array Format
#include "/Users/nimal/Documents/PROJEC/ADIS/src/secret.h" //path to your Credential folder 

// --- WI-FI CREDENTIALS ---
const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// --- SYSTEM TIMING CONSTANTS ---
#define SCREEN_AWAKE_MS       10000  
#define SCREEN_SLEEP_WARN_MS  12000  
#define TIMER_AUTOSTART_SEC   5      
#define TIME_UP_DISPLAY_MS    5000   
#define RFID_TIMEOUT_MS       2000   
#define FOCUS_POPUP_MS        3000   
#define SYNC_HOUR             23
#define SYNC_MINUTE           45

// --- PIN DEFINITIONS ---
#define TFT_CS      5
#define TFT_DC      2
#define TFT_RST     4
#define PIR_PIN     27
#define RFID_CS     21
#define RFID_RST    22
#define BTN_MODE    32 
#define BTN_TIMER   33 

#define OLED_SDA    25
#define OLED_SCL    26
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// --- HARDWARE OBJECTS ---
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
MFRC522 mfrc522(RFID_CS, RFID_RST);
Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// --- SHARED RTOS VARIABLES & LOCKS ---
volatile TickType_t lastMotionTick = 0; 
SemaphoreHandle_t spiMutex;

// System Modes (NOW INCLUDES STOPWATCH)

enum SystemMode { MODE_DEFAULT, MODE_TIMER, MODE_STOPWATCH, MODE_REMINDER };
volatile SystemMode currentMode = MODE_DEFAULT;

// --- MANUAL TIMER VARIABLES ---
volatile int timerMinutes = 1;  
volatile TickType_t lastTimerInteractionTick = 0; 
volatile bool isTimerRunning = false;             
volatile TickType_t timerStartTick = 0;           
volatile TickType_t timeUpStartTick = 0; 

// Focus Session Data
struct FocusSession {
    String startTime;
    String endTime;
    int durationSec;
};

std::vector<FocusSession> dailyLog;
String currentReminder = "No Reminders";

// --- NEW: STOPWATCH VARIABLES ---
volatile bool isStopwatchRunning = false;
volatile unsigned long accumulatedStopwatchSeconds = 0;
volatile TickType_t stopwatchStartTick = 0;

// --- PHONE JAIL NOTIFICATION VARIABLES ---
volatile bool isPhoneInJail = false;
volatile TickType_t jailStartTime = 0;
volatile bool showJailStartMessage = false;
volatile TickType_t jailStartMessageTick = 0;
volatile bool showJailSummary = false;
volatile TickType_t summaryStartTick = 0;
volatile int lastFocusSeconds = 0;

// Display States
enum DisplayState {
  STATE_AWAKE_DEFAULT,
  STATE_AWAKE_TIMER,
  STATE_AWAKE_STOPWATCH,   // NEW STATE!
  STATE_AWAKE_REMINDER,
  STATE_AWAKE_FOCUS_START, 
  STATE_AWAKE_SUMMARY,     
  STATE_SLEEPING_TEXT,
  STATE_ASLEEP
};

// =======================================================
// TASK 1: NETWORK MANAGER
// =======================================================
void networkTask(void *parameter) {
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) vTaskDelay(pdMS_TO_TICKS(500));
    
    configTime(19800, 0, "time.google.com");
    
    config.database_url = FIREBASE_HOST;
    config.signer.tokens.legacy_token = FIREBASE_AUTH;
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    bool hasSyncedToday = false;

    for (;;) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            // 1. DAILY DATA PUSH (11:45 PM)
            if (timeinfo.tm_hour == SYNC_HOUR && timeinfo.tm_min == SYNC_MINUTE && !hasSyncedToday) {
                Serial.println("☁️ Firebase: Starting Daily Sync...");
                for (const auto& session : dailyLog) {
                    FirebaseJson json;
                    json.add("start", session.startTime);
                    json.add("end", session.endTime);
                    json.add("duration", session.durationSec);
                    Firebase.RTDB.pushJSON(&fbdo, "/focus_sessions", &json);
                }
                dailyLog.clear(); // Clear local buffer after push
                hasSyncedToday = true;
            }
            if (timeinfo.tm_hour == 0) hasSyncedToday = false; // Reset at midnight

            // 2. PERIODIC REMINDER PULL (Every 5 minutes)
            if (timeinfo.tm_min % 5 == 0) {
                if (Firebase.RTDB.getString(&fbdo, "/reminders/latest")) {
                    currentReminder = fbdo.stringData();
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30000)); // Check every 30s
    }
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

// --- UTILITY: GET TIME STRING ---
String getTimestamp() {
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)) return "00:00:00";
    char timeStr[20];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
    return String(timeStr);
}

// =======================================================
// TASK 3: BUTTON MANAGER
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
      // Now cycles through 4 modes instead of 3
      currentMode = (SystemMode)((currentMode + 1) % 4); 
      lastMotionTick = xTaskGetTickCount(); 
      
      // Reset Timer if entering Timer Mode
      if (currentMode == MODE_TIMER) {
        lastTimerInteractionTick = xTaskGetTickCount();
        isTimerRunning = false; 
      }
      
      // Reset Stopwatch if entering Stopwatch Mode
      if (currentMode == MODE_STOPWATCH) {
        isStopwatchRunning = false;
        accumulatedStopwatchSeconds = 0; 
      }
      
      Serial.printf("🔘 Mode Switched: %d\n", currentMode);
    }
    lastModeState = modeState;

    // --- BUTTON 2: ACTION BUTTON ---
    if (timerState == LOW && lastTimerState == HIGH) {
      lastMotionTick = xTaskGetTickCount(); 
      
      // Action for Timer Mode
      if (currentMode == MODE_TIMER) {
        if (timerMinutes == 1) timerMinutes = 5;
        else if (timerMinutes == 5) timerMinutes = 15;
        else if (timerMinutes == 15) timerMinutes = 30;
        else if (timerMinutes == 30) timerMinutes = 60;
        else timerMinutes = 1; 
        
        lastTimerInteractionTick = xTaskGetTickCount(); 
        isTimerRunning = false;                         
        Serial.printf("⏱️ Timer Set To: %d min\n", timerMinutes);
      }
      
      // Action for Stopwatch Mode (Play/Pause Toggle)
      else if (currentMode == MODE_STOPWATCH) {
        if (isStopwatchRunning) {
          // PAUSE IT
          isStopwatchRunning = false;
          // Save the time we accrued so far
          accumulatedStopwatchSeconds += (xTaskGetTickCount() - stopwatchStartTick) / 1000;
          Serial.println("⏸️ Stopwatch Paused");
        } else {
          // START/RESUME IT
          isStopwatchRunning = true;
          stopwatchStartTick = xTaskGetTickCount();
          Serial.println("▶️ Stopwatch Started");
        }
      }
    }
    lastTimerState = timerState;
    vTaskDelay(pdMS_TO_TICKS(50)); 
  }
}

// =======================================================
// TASK 4: PHONE JAIL RFID
// =======================================================
void rfidTask(void *parameter) {
  // Give the main setup a moment to initialize the SPI bus
  vTaskDelay(pdMS_TO_TICKS(1000));
  
  if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
    mfrc522.PCD_Init();
    xSemaphoreGive(spiMutex);
  }
  
  TickType_t lastCardSeenTime = 0;
  String sessionStartStr; // Temporarily holds the start time for Firebase

  for (;;) {
    TickType_t currentTick = xTaskGetTickCount();
    MFRC522::StatusCode status;
    byte bufferATQA[2];
    byte bufferSize = sizeof(bufferATQA);

    // --- CRITICAL SECTION: LOCK SPI BUS TO READ RFID ---
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
        // --- PHONE JUST ENTERED JAIL ---
        isPhoneInJail = true;
        jailStartTime = currentTick;
        sessionStartStr = getTimestamp(); // Capture the exact Start Time for Firebase
        
        showJailStartMessage = true;          // Trigger the 3-second Start Popup
        jailStartMessageTick = currentTick;   
        showJailSummary = false;              // Cancel any old summaries
        
        lastMotionTick = currentTick;         // Wake up screen instantly
        Serial.println("\n🔒 Phone Locked in Jail! Focus time started.");
      }
    } else {
      // Timeout Check: Phone lifted for more than 2 seconds (RFID_TIMEOUT_MS)
      if (isPhoneInJail && (currentTick - lastCardSeenTime > pdMS_TO_TICKS(RFID_TIMEOUT_MS))) {
        
        // --- PHONE JUST LEFT JAIL ---
        isPhoneInJail = false;
        
        // Calculate duration (subtracting the 2 second timeout buffer)
        int duration = (currentTick - jailStartTime - pdMS_TO_TICKS(RFID_TIMEOUT_MS)) / 1000;
        lastFocusSeconds = duration; // Save for the Display summary
        
        // --- FIREBASE LOGIC: ONLY SAVE IF > 15 MIN (900 seconds) ---
        if (duration >= 900) {
            dailyLog.push_back({sessionStartStr, getTimestamp(), duration});
            Serial.println("💾 Session valid (>15m). Saved to local buffer for 11:45 PM sync.");
        } else {
            Serial.println("⚠️ Session too short (<15m), discarded from Firebase log.");
        }
        
        showJailStartMessage = false;         // Cancel start message if picked up instantly
        showJailSummary = true;               // Trigger the 3-second End Popup
        summaryStartTick = currentTick;       
        
        lastMotionTick = currentTick;         // Wake up screen to show summary
        Serial.printf("🔓 Phone Removed! Focused for %d seconds\n", lastFocusSeconds);
      }
    }
    
    // Check for the phone twice a second
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
  int lastDrawnTimerSecond = -1; 
  int lastDrawnStopwatchSecond = -1; // Added for Stopwatch anti-flicker
  int lastSetupCountdown = -1; 

  for (;;) {
    TickType_t currentTick = xTaskGetTickCount();
    TickType_t timeSinceMotion = currentTick - lastMotionTick;

    // RULE 1: AWAKE
    if (timeSinceMotion < pdMS_TO_TICKS(SCREEN_AWAKE_MS)) {
      
      // --- NOTIFICATION OVERRIDE A: FOCUS STARTED ---
      if (showJailStartMessage) {
        lastMotionTick = currentTick; 
        if (currentState != STATE_AWAKE_FOCUS_START) {
          if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
            tft.fillScreen(ST77XX_BLACK);
            tft.setCursor(5, 50);
            tft.setTextColor(ST77XX_MAGENTA);
            tft.setTextSize(2);
            tft.print("FOCUS MODE");
            tft.setCursor(20, 80);
            tft.setTextColor(ST77XX_WHITE);
            tft.setTextSize(1);
            tft.print("Tracking started...");
            xSemaphoreGive(spiMutex);
          }
          currentState = STATE_AWAKE_FOCUS_START;
        }
        if (currentTick - jailStartMessageTick > pdMS_TO_TICKS(FOCUS_POPUP_MS)) {
          showJailStartMessage = false;
          currentState = STATE_ASLEEP; 
        }
      }
      
      // --- NOTIFICATION OVERRIDE B: FOCUS SUMMARY ---
      else if (showJailSummary) {
        lastMotionTick = currentTick; 
        if (currentState != STATE_AWAKE_SUMMARY) {
          if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
            tft.fillScreen(ST77XX_BLACK);
            tft.setCursor(5, 30);
            tft.setTextColor(ST77XX_CYAN);
            tft.setTextSize(2);
            tft.print("FOCUS OVER");
            tft.setCursor(25, 70);
            tft.setTextColor(ST77XX_WHITE);
            tft.setTextSize(3);
            tft.printf("%02d:%02d", lastFocusSeconds / 60, lastFocusSeconds % 60);
            xSemaphoreGive(spiMutex);
          }
          currentState = STATE_AWAKE_SUMMARY;
        }
        if (currentTick - summaryStartTick > pdMS_TO_TICKS(FOCUS_POPUP_MS)) {
          showJailSummary = false;
          currentState = STATE_ASLEEP; 
        }
      }

      // --- STANDARD MODES ---
      else {
        
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
        
        // --- MODE 1: COUNTDOWN TIMER ---
        else if (currentMode == MODE_TIMER) {
          bool justWokeUp = (currentState != STATE_AWAKE_TIMER);
          currentState = STATE_AWAKE_TIMER; 
          
          if (!isTimerRunning) {
            long waitTimeRemaining = TIMER_AUTOSTART_SEC - ((currentTick - lastTimerInteractionTick) / 1000);
            if (waitTimeRemaining <= 0) {
              isTimerRunning = true;
              timerStartTick = currentTick;
              lastDrawnTimerSecond = -1; 
              if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
                 tft.fillScreen(ST77XX_BLACK);
                 xSemaphoreGive(spiMutex);
              }
            } 
            else if (waitTimeRemaining != lastSetupCountdown || justWokeUp) {
              if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
                tft.fillScreen(ST77XX_BLACK);
                tft.setCursor(10, 30);
                tft.setTextColor(ST77XX_GREEN);
                tft.setTextSize(2);
                tft.print("TIMER SETUP");
                tft.setCursor(30, 60);
                tft.setTextColor(ST77XX_WHITE);
                tft.setTextSize(3);
                tft.printf("%02d M", timerMinutes);
                tft.setCursor(15, 100);
                tft.setTextColor(ST77XX_YELLOW);
                tft.setTextSize(1);
                tft.printf("Auto-start in %d...", waitTimeRemaining);
                xSemaphoreGive(spiMutex);
              }
              lastSetupCountdown = waitTimeRemaining;
            }
          }
          else {
            long elapsedSeconds = (currentTick - timerStartTick) / 1000;
            long totalSeconds = timerMinutes * 60;
            long remainingSeconds = totalSeconds - elapsedSeconds;

            if (remainingSeconds > 0) {
              lastMotionTick = currentTick; 
              if (remainingSeconds != lastDrawnTimerSecond || justWokeUp) {
                long m = remainingSeconds / 60;
                long s = remainingSeconds % 60;
                if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
                  if (justWokeUp) tft.fillScreen(ST77XX_BLACK);
                  tft.setCursor(15, 60);
                  tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK); 
                  tft.setTextSize(3);
                  tft.printf("%02d:%02d", m, s);
                  xSemaphoreGive(spiMutex);
                }
                lastDrawnTimerSecond = remainingSeconds;
              }
            } 
            else {
              lastMotionTick = currentTick; 
              if (lastDrawnTimerSecond != 0 || justWokeUp) {
                if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
                  tft.fillScreen(ST77XX_RED);
                  tft.setCursor(15, 60);
                  tft.setTextColor(ST77XX_WHITE, ST77XX_RED);
                  tft.setTextSize(3);
                  tft.print("TIME UP!");
                  xSemaphoreGive(spiMutex);
                }
                if (lastDrawnTimerSecond != 0) timeUpStartTick = currentTick; 
                lastDrawnTimerSecond = 0; 
              }
              if (currentTick - timeUpStartTick > pdMS_TO_TICKS(TIME_UP_DISPLAY_MS)) {
                 currentMode = MODE_DEFAULT;     
                 isTimerRunning = false;         
                 timerMinutes = 1;               
                 lastMotionTick = currentTick;   
              }
            }
          }
        }
        
        // --- MODE 2: STOPWATCH (NEW!) ---
        else if (currentMode == MODE_STOPWATCH) {
          bool justWokeUp = (currentState != STATE_AWAKE_STOPWATCH);
          currentState = STATE_AWAKE_STOPWATCH; 
          
          unsigned long currentElapsed = accumulatedStopwatchSeconds;
          
          if (isStopwatchRunning) {
             currentElapsed += (currentTick - stopwatchStartTick) / 1000;
             lastMotionTick = currentTick; // Feed watchdog so screen stays awake!
          }
          
          // Draw if the second changed OR if we just woke the screen up
          if (currentElapsed != lastDrawnStopwatchSecond || justWokeUp) {
            long m = currentElapsed / 60;
            long s = currentElapsed % 60;
            
            if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
              if (justWokeUp) {
                tft.fillScreen(ST77XX_BLACK);
                tft.setCursor(10, 30);
                tft.setTextColor(ST77XX_ORANGE);
                tft.setTextSize(2);
                tft.print("STOPWATCH");
              }
              
              // Draw the time
              tft.setCursor(20, 60);
              tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK); 
              tft.setTextSize(3);
              tft.printf("%02d:%02d", m, s);
              
              // Draw the Status (Running / Paused)
              tft.setCursor(35, 100);
              tft.setTextSize(1);
              if (isStopwatchRunning) {
                tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
                tft.print(" RUNNING "); // Extra spaces overwrite old text cleanly
              } else {
                tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
                tft.print(" PAUSED  ");
              }
              
              xSemaphoreGive(spiMutex);
            }
            lastDrawnStopwatchSecond = currentElapsed;
          }
        }
        
        // --- MODE 3: REMINDER ---
        else if (currentMode == MODE_REMINDER) {
          if (currentState != STATE_AWAKE_REMINDER) {
            if (xSemaphoreTake(spiMutex, portMAX_DELAY)) {
              tft.fillScreen(ST77XX_BLACK);
              tft.setCursor(10, 30);
              tft.setTextColor(ST77XX_BLUE);
              tft.setTextSize(2);
              tft.print("REMINDERS");
          
              tft.setCursor(10, 60);
              tft.setTextColor(ST77XX_WHITE);
              tft.setTextSize(1);
              tft.println(currentReminder); // Fetched from Firebase
          
              xSemaphoreGive(spiMutex);
            }
            currentState = STATE_AWAKE_REMINDER;
          }
        }
      }
    } 
    
    // RULE 2: SLEEPING WARNING
    else if (timeSinceMotion < pdMS_TO_TICKS(SCREEN_SLEEP_WARN_MS)) {
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
    
    // RULE 3: DEEP SLEEP
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
// TASK 6: I2C SECONDARY DISPLAY (NEW!)
// =======================================================
void clockTask(void *parameter) {
  Wire.begin(OLED_SDA, OLED_SCL);
  if(!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C is standard for 0.96" OLED
    Serial.println(F("SSD1306 allocation failed"));
    vTaskDelete(NULL);
  }
  
  for(;;) {
    struct tm timeinfo;
    if(getLocalTime(&timeinfo)) {
      oled.clearDisplay();
      oled.setTextColor(SSD1306_WHITE);
      
      // Center the text roughly
      oled.setCursor(15, 20); 
      oled.setTextSize(3);
      oled.printf("%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
      
      // Small seconds underneath
      oled.setCursor(55, 50);
      oled.setTextSize(1);
      oled.printf("%02d", timeinfo.tm_sec);
      
      oled.display();
    }
    // Update exactly once a second
    vTaskDelay(pdMS_TO_TICKS(1000));
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
    xTaskCreatePinnedToCore(networkTask, "Network", 16384, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(pirTask,     "PIR",     2048, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(buttonTask,  "Buttons", 2048, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(rfidTask,    "RFID",    8192, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(displayTask, "Display", 10240, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(clockTask,   "Clock",   2048, NULL, 1, NULL, 1);
  }
  
  vTaskDelete(NULL);
}

void loop() {}