#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(1000); // Wait for serial to initialize
  Serial.println("Checking memory for ESP32 CAM AI Thinker");

  size_t free_psram = ESP.getFreePsram();
  if (free_psram > 0) {
    Serial.print("Free PSRAM: ");
    Serial.print(free_psram);
    Serial.println(" bytes");
  } else {
    Serial.println("PSRAM is not available");
  }

  Serial.print("Free heap (DRAM): ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes");
}

void loop() {
  // Nothing to do
}
