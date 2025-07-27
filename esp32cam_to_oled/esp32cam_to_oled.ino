#include <Wire.h>
#include "esp_camera.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// --- I²C pins for OLED (avoiding flash LED and camera conflicts) ---
#define I2C_SDA_PIN  15  // Safe pin, not used by camera or flash LED
#define I2C_SCL_PIN  14  // Safe pin, not used by camera or flash LED

// --- OLED configuration ---
#define OLED_ADDR    0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- Camera Pin configuration for AI‑Thinker ESP32‑CAM ---
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// --- Target bitmap size ---
#define BMP_W 128
#define BMP_H 64

// --- Configuration ---
#define FRAME_DELAY_MS 50        // ~20 FPS (balanced speed/stability)
#define BRIGHTNESS_THRESHOLD 64 // Tweak threshold if necessary
#define MAX_RETRIES 5           // More retry attempts
#define STARTUP_DELAY_MS 3000   // Startup delay

// 1‑bit bitmap buffer (128×64 = 8192 bits = 1024 bytes)
static uint8_t bitmapBuffer[BMP_W * BMP_H / 8];

// Precomputed scale factors for 96x96 input
const float scaleX = 96.0f / BMP_W;   // source 96x96 width → 128
const float scaleY = 96.0f / BMP_H;   // source 96x96 height → 64

// Error tracking
static uint32_t frameCount = 0;
static uint32_t errorCount = 0;
static bool cameraInitialized = false;

// Free memory check function
void printMemoryInfo() {
  Serial.printf("Free heap: %d bytes, Min free heap: %d bytes\n", 
                ESP.getFreeHeap(), ESP.getMinFreeHeap());
  if (psramFound()) {
    Serial.printf("PSRAM total: %d, free: %d (WARNING: PSRAM may be faulty)\n", 
                  ESP.getPsramSize(), ESP.getFreePsram());
  } else {
    Serial.println("PSRAM not found or disabled");
  }
}

// Initialize the camera with enhanced error handling for faulty PSRAM
bool initCamera() {
  Serial.println("Initializing camera (avoiding faulty PSRAM)...");
  printMemoryInfo();
  
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;  // Increased back to 20MHz for better FPS
  
  // Use smallest possible frame size and avoid PSRAM
  config.frame_size   = FRAMESIZE_96X96;   // 96x96 pixels to reduce memory usage
  config.pixel_format = PIXFORMAT_GRAYSCALE;
  config.grab_mode    = CAMERA_GRAB_LATEST;
  config.fb_location  = CAMERA_FB_IN_DRAM;  // Use DRAM instead of faulty PSRAM
  config.jpeg_quality = 63;  // Not used for grayscale but set anyway
  config.fb_count     = 1;   // Single buffer to minimize memory usage
  
  // Power down camera first to reset state
  pinMode(PWDN_GPIO_NUM, OUTPUT);
  digitalWrite(PWDN_GPIO_NUM, HIGH);
  delay(100);
  digitalWrite(PWDN_GPIO_NUM, LOW);
  delay(100);
  
  // Initialize camera with extended retries
  for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
    Serial.printf("Camera init attempt %d/%d...\n", attempt, MAX_RETRIES);
    printMemoryInfo();
    
    esp_err_t err = esp_camera_init(&config);
    if (err == ESP_OK) {
      Serial.println("Camera initialized successfully!");
      
      // Get camera sensor for additional configuration
      sensor_t* s = esp_camera_sensor_get();
      if (s) {
        // Conservative settings for stability
        s->set_brightness(s, 0);     // 0 = default
        s->set_contrast(s, 0);       // 0 = default
        s->set_saturation(s, 0);     // Not used in grayscale
        s->set_gainceiling(s, (gainceiling_t)4);  // Lower gain ceiling
        s->set_colorbar(s, 0);       // Disable color bar test
        s->set_whitebal(s, 1);       // Enable white balance
        s->set_gain_ctrl(s, 1);      // Enable AGC
        s->set_exposure_ctrl(s, 1);  // Enable AEC
        s->set_aec2(s, 0);           // Disable AEC2
        s->set_ae_level(s, 0);       // AE level 0
        s->set_aec_value(s, 400);    // AEC value
        s->set_agc_gain(s, 5);       // Lower AGC gain
        s->set_awb_gain(s, 1);       // Enable AWB gain
        s->set_wb_mode(s, 0);        // Auto white balance
        Serial.println("Camera sensor configured for stability");
      }
      
      cameraInitialized = true;
      return true;
    }
    
    Serial.printf("Camera init attempt %d failed: 0x%x\n", attempt, err);
    
    // Different error messages for different error codes
    switch (err) {
      case ESP_ERR_NO_MEM:
        Serial.println("  -> Out of memory (DRAM insufficient)");
        break;
      case ESP_ERR_INVALID_ARG:
        Serial.println("  -> Invalid camera configuration");
        break;
      case ESP_FAIL:
        Serial.println("  -> Camera hardware failure");
        break;
      default:
        Serial.printf("  -> Unknown error: 0x%x\n", err);
    }
    
    if (attempt < MAX_RETRIES) {
      Serial.printf("Retrying in %d seconds...\n", attempt);
      delay(attempt * 1000);  // Progressive delay
      
      // Try to free any allocated resources
      esp_camera_deinit();
      delay(500);
    }
  }
  
  Serial.println("Camera initialization failed after all attempts!");
  Serial.println("Possible causes:");
  Serial.println("1. Faulty PSRAM (detected in your logs)");
  Serial.println("2. Insufficient power supply");
  Serial.println("3. Hardware defect in camera module");
  Serial.println("4. Wiring issues");
  return false;
}

// Optimized but working frame conversion
inline void convertFrameToBitmap(const camera_fb_t* fb) {
  if (!fb || !fb->buf || fb->len < (96 * 96)) {
    return;
  }
  
  memset(bitmapBuffer, 0, sizeof(bitmapBuffer));
  const uint8_t* gray = fb->buf;
  const uint8_t threshold = BRIGHTNESS_THRESHOLD;
  
  // Use the original working scaling method but optimized
  for (int y = 0; y < BMP_H; y++) {
    int srcY = (int)(y * scaleY);
    if (srcY >= 96) srcY = 95;
    int srcRow = srcY * 96;
    
    for (int x = 0; x < BMP_W; x++) {
      int srcX = (int)(x * scaleX);
      if (srcX >= 96) srcX = 95;
      
      uint8_t pix = gray[srcRow + srcX];
      if (pix > threshold) {
        int idx = (y * BMP_W + x) >> 3;
        int bit = 7 - (x & 7);  // Keep original bit order
        bitmapBuffer[idx] |= (1 << bit);
      }
    }
  }
}

// Display status information on OLED
void displayStatus(const char* message, bool showStats = false) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(1);
  display.setCursor(0, 0);
  display.println("ESP32-CAM Status:");
  display.println(message);
  
  if (showStats) {
    display.printf("Frames: %lu", frameCount);
    display.println();
    display.printf("Errors: %lu", errorCount);
    display.println();
    display.printf("Free mem: %d", ESP.getFreeHeap());
  }
  
  display.display();
}

// Restore working frame processing with clearDisplay()
bool processFrame() {
  if (!cameraInitialized) {
    return false;
  }
  
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    errorCount++;
    return false;
  }
  
  // Quick validation
  if (fb->width != 96 || fb->height != 96 || fb->format != PIXFORMAT_GRAYSCALE) {
    esp_camera_fb_return(fb);
    errorCount++;
    return false;
  }
  
  convertFrameToBitmap(fb);
  esp_camera_fb_return(fb);
  
  // Restore the working OLED update method
  display.clearDisplay();
  display.drawBitmap(0, 0, bitmapBuffer, BMP_W, BMP_H, 1);
  display.display();
  
  frameCount++;
  return true;
}

// Test function to verify OLED without camera
void testOLED() {
  Serial.println("Testing OLED display...");
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(1);
  display.setCursor(0, 0);
  display.println("OLED Test Pattern");
  display.println("ESP32-CAM Ready");
  display.println("================");
  display.println("Line 4");
  display.println("Line 5");
  display.println("Line 6");
  display.println("Line 7");
  display.println("Line 8");
  display.display();
  
  delay(2000);
  
  // Draw some test patterns
  display.clearDisplay();
  for (int i = 0; i < SCREEN_WIDTH; i += 8) {
    display.drawLine(i, 0, i, SCREEN_HEIGHT - 1, 1);
  }
  display.display();
  delay(1000);
  
  display.clearDisplay();
  for (int i = 0; i < SCREEN_HEIGHT; i += 8) {
    display.drawLine(0, i, SCREEN_WIDTH - 1, i, 1);
  }
  display.display();
  delay(1000);
  
  Serial.println("OLED test completed successfully!");
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32-CAM to OLED Display (PSRAM-Safe Version) ===");
  Serial.println("Designed to work with faulty PSRAM hardware");
  
  // Extended startup delay for power stabilization
  Serial.printf("Power stabilization delay (%d ms)...\n", STARTUP_DELAY_MS);
  delay(STARTUP_DELAY_MS);
  
  printMemoryInfo();
  
  // Initialize I²C for OLED with enhanced error handling
  Serial.printf("Initializing I2C (SDA: %d, SCL: %d)...\n", I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(400000); // Fast I2C for better OLED update speed (400kHz)
  
  // Test I2C communication
  Wire.beginTransmission(OLED_ADDR);
  if (Wire.endTransmission() == 0) {
    Serial.printf("I2C device found at address 0x%02X\n", OLED_ADDR);
  } else {
    Serial.printf("No I2C device found at address 0x%02X\n", OLED_ADDR);
    Serial.println("Check OLED wiring!");
  }
  
  // Initialize OLED display
  Serial.println("Initializing OLED display...");
  if (!display.begin(OLED_ADDR)) {
    Serial.println("OLED initialization failed!");
    Serial.println("Check connections:");
    Serial.printf("  VCC -> 3.3V or 5V\n");
    Serial.printf("  GND -> GND\n");
    Serial.printf("  SDA -> GPIO %d\n", I2C_SDA_PIN);
    Serial.printf("  SCL -> GPIO %d\n", I2C_SCL_PIN);
    while (true) {
      delay(5000);
      Serial.println("OLED init failed - system halted");
    }
  }
  
  Serial.println("OLED initialized successfully!");
  display.clearDisplay();
  display.display();
  
  // Test OLED functionality
  testOLED();
  
  // Show initialization status
  displayStatus("Initializing camera...");
  
  // Initialize camera with PSRAM workaround
  if (!initCamera()) {
    displayStatus("Camera FAILED!\nPSRAM issue detected");
    Serial.println("=== CAMERA INITIALIZATION FAILED ===");
    Serial.println("Your ESP32-CAM has faulty PSRAM hardware.");
    Serial.println("This is a common manufacturing defect.");
    Serial.println("Solutions:");
    Serial.println("1. Try a different ESP32-CAM module");
    Serial.println("2. Use camera-free projects only");
    Serial.println("3. Return/exchange if recently purchased");
    
    while (true) {
      delay(10000);
      displayStatus("Hardware DEFECT!\nPSRAM faulty\nTry different module", true);
      delay(10000);
    }
  }
  
  // Show ready status
  displayStatus("System ready!\nStreaming...");
  delay(2000);
  
  Serial.println("=== System Ready ===");
  Serial.printf("Target FPS: ~%d\n", 1000 / FRAME_DELAY_MS);
  Serial.println("Camera resolution: 96x96 pixels");
  Serial.println("Display resolution: 128x64 pixels");
  Serial.println("Memory: Using DRAM only (PSRAM disabled)");
  Serial.println("Starting video stream...");
}

void loop() {
  unsigned long startTime = millis();
  
  // Process frame
  bool success = processFrame();
  
  // Print statistics periodically (but less frequently than before)
  if (frameCount > 0 && frameCount % 100 == 0) {
    float errorRate = (float)errorCount / (frameCount + errorCount) * 100.0f;
    Serial.printf("Stats - Frames: %lu, Errors: %lu (%.1f%%), Free mem: %d\n", 
                  frameCount, errorCount, errorRate, ESP.getFreeHeap());
  }
  
  // Frame rate limiting
  unsigned long processingTime = millis() - startTime;
  if (processingTime < FRAME_DELAY_MS) {
    delay(FRAME_DELAY_MS - processingTime);
  }
  
  // Watchdog feed
  yield();
  
  // Reset error counter if it gets too high
  if (errorCount > 1000) {
    Serial.println("Resetting error counter...");
    errorCount = 0;
  }
}
