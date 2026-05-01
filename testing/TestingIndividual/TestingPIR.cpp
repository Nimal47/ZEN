#include <Arduino.h>

#define PIR_PIN 23

// Task handle (optional)
TaskHandle_t pirTaskHandle;

// Task function
void pirTask(void *parameter) {
  while (true) {
    int motion = digitalRead(PIR_PIN);

    if (motion == HIGH) {
      Serial.println("🚶 Motion Detected!");
    } else {
      Serial.println("😴 No Motion");
    }

    vTaskDelay(pdMS_TO_TICKS(500)); // RTOS delay (NOT delay())
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);

  Serial.println("PIR Sensor Ready (RTOS)...");

  // Create task
  xTaskCreate(
    pirTask,        // Task function
    "PIR Task",     // Name
    2048,           // Stack size
    NULL,           // Parameter
    1,              // Priority
    &pirTaskHandle  // Handle
  );
  vTaskDelete(NULL);
}

void loop() {
  // Empty (RTOS handles everything)
}