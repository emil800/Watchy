/*
* Standalone Watchy HR Monitor - NO Watchy library dependency
* Install: NimBLE-Arduino from Library Manager
* Board: ESP32S3 Dev Module
* PSRAM: Not required (works with or without PSRAM) 
*/

// Configuration
#include "settings.h"

// Watchy library dependencies
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Wire.h>
#include <DS3232RTC.h>
#include <nvs_flash.h>
#include "esp_task_wdt.h"
#include "esp_heap_caps.h"

// Deep Sleep dependencies
#include "esp_sleep.h"
#include "driver/rtc_io.h"

// File system for logging
#include <FS.h>
#include <LittleFS.h>

// WiFi for web server
#include <WiFi.h>
#include <WebServer.h>
WebServer server(80);
bool keepWebserverAlive = false;

// Use NimBLE
#include <NimBLEDevice.h>

// Display
GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(GxEPD2_154_D67(DISPLAY_CS, DISPLAY_DC, DISPLAY_RES, DISPLAY_BUSY));

// BLE UUIDs for Heart Rate Service
static NimBLEUUID serviceUUID((uint16_t)BLE_SERVICE_UUID);
static NimBLEUUID charUUID((uint16_t)BLE_CHAR_UUID);

// Global state
volatile int latestHR = 0;
volatile bool hrReceived = false;
bool isLogging = false;
unsigned long lastDisplayUpdate = 0;

NimBLEClient* globalClient = nullptr;
int lastDisplayedHR = 0;

// RR Interval storage
#if ENABLE_RR_INTERVALS
uint16_t latestRRIntervals[4] = {0};  // Store up to 4 RR intervals
uint8_t rrIntervalCount = 0;
#endif

// Include function headers (with inline implementations)
#include "utils_functions.h"
#include "display_functions.h"
#include "file_functions.h"
#include "ble_functions.h"
#include "wifi_functions.h"
#include "sleep_functions.h"

// Log buffer
LogEntry logBuffer[LOG_BUFFER_SIZE];
uint16_t logBufferIndex = 0;

void setup() {
  // KILL WIFI IMMEDIATELY
  WiFi.mode(WIFI_OFF);
  btStop(); // Stop Classic Bluetooth (we only want BLE)
  
  // Vibrate to prove code is running
  pinMode(VIB_MOTOR_PIN, OUTPUT);
  digitalWrite(VIB_MOTOR_PIN, true);
  delay(200);
  digitalWrite(VIB_MOTOR_PIN, false);

  /* s_begin(115200) */
  
  /* s_printMemoryStatus() */
  
  /* s_println("Configure Pins") */
  pinMode(UP_BTN_PIN, INPUT_PULLUP);
  pinMode(DOWN_BTN_PIN, INPUT_PULLUP);
  pinMode(BACK_BTN_PIN, INPUT_PULLUP);
  pinMode(MENU_BTN_PIN, INPUT_PULLUP);
  
  /* s_println("Initialize LittleFS for file logging") */
  if (!LittleFS.begin(true)) {
    /* s_println("LittleFS mount failed!") */
  } else {
    /* s_println("LittleFS mounted successfully") */
    
    File file = LittleFS.open(HR_LOG_FILE_NAME, "r");
    if (!file || file.size() == 0) {
      if (file) file.close();
      file = LittleFS.open(HR_LOG_FILE_NAME, "w");
      if (file) {
        #if ENABLE_RR_INTERVALS
        file.println("timestamp_ms,heart_rate_bpm,rr_interval_1_ms,rr_interval_2_ms,rr_interval_3_ms,rr_interval_4_ms");
        #else
        file.println("timestamp_ms,heart_rate_bpm");
        #endif
        file.close();
        /* s_println("Created log file with header") */
      }
    } else {
      size_t fileSize = file.size();
      file.close();
      /* s_printf("Log file exists, size: %d bytes\n", fileSize) */
    }
  }
  
  // Initialize NVS (only erase if needed - avoids erasing on every boot)
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    /* s_println("NVS partition was truncated and needs to be erased") */
    nvs_flash_erase();
    ret = nvs_flash_init();
  }
  if (ret != ESP_OK) {
    /* s_printf("NVS init failed: %d\n", ret) */
  } else {
    /* s_println("NVS initialized") */
  }
  
  // PSRAM setup (only if available)
  if (ESP.getPsramSize() > 0) {
    /* s_println("PSRAM available - enabling for large allocations") */
    heap_caps_malloc_extmem_enable(4096);
  } else {
    /* s_println("PSRAM not available - using internal RAM only") */
  }

  // Watchy 3.0 uses custom SPI pins. MUST init SPI before display!
  /* s_println("Starting SPI...") */
  SPI.begin(WATCHY_V3_SCK, WATCHY_V3_MISO, WATCHY_V3_MOSI, WATCHY_V3_SS);
  delay(100);
  
  /* s_println("Initializing display") */
  
  bool displayOk = false;
  try {
    initDisplay();
    /* s_println("Display initialized!") */
    displayOk = true;
  } catch (...) {
    /* s_println("ERROR: Display init failed in try block!") */
  }
  
  if (displayOk) {
    try {
      showMessage("READY", "Press UP");
      /* s_println("Display message shown!") */
    } catch (...) {
      /* s_println("ERROR: Display message failed!") */
    }
  }
  
  /* s_println("=================================") */
  /* s_println("Ready! Waiting for button press...") */
  /* s_println("UP button = Start BLE") */
  /* s_println("DOWN button = Stop logging") */
  /* s_println("Serial commands: 'd'=dump, 's'=size, 'h'=help") */
  /* s_println("Serial command: 'w'=start WiFi server") */
  /* s_println("=================================") */
}

void loop() {
  // Handle Serial commands for file access
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();
    if (cmd == "d" || cmd == "dump" || cmd == "download") {
      dumpLogFile();
    } else if (cmd == "s" || cmd == "size") {
      File file = LittleFS.open(HR_LOG_FILE_NAME, "r");
      if (file) {
        /* s_printf("Log file size: %d bytes\n", file.size()) */
        file.close();
      } else {
        /* s_println("Log file not found") */
      }
    } else if (cmd == "h" || cmd == "help") {
      /* s_println("\n=== Serial Commands ===") */
      /* s_println("d, dump, download - Dump log file contents") */
      /* s_println("s, size - Show log file size") */
      /* s_println("h, help - Show this help") */
      /* s_println("w, wifi - Start WiFi web server") */
      /* s_println("======================\n") */
    }
    else if (cmd == "w" || cmd == "wifi") {
      startWebServer();
    }
  }
  
  // Handle web server requests
  if (keepWebserverAlive) {
    server.handleClient();
    // Check if BACK button pressed to stop server (active LOW)
    if (digitalRead(BACK_BTN_PIN) == 0) {
      delay(50); // Debounce
      if (digitalRead(BACK_BTN_PIN) == 0) {
        keepWebserverAlive = false;
        server.stop();
        WiFi.mode(WIFI_OFF);
        showMessage("WiFi Off");
        
        // Check if we should go to sleep (only if logging is also not active)
        if (!isLogging) {
          delay(2000); // Give time to see "WiFi Off" message
          goToDeepSleep();
        }
      }
    }
    delay(1);
    return; // Don't process buttons while server is running
  }

  // Heartbeat to show loop is running
  static unsigned long lastBeat = 0;
  if (millis() - lastBeat > 5000) {
    /* s_println("Loop running...") */
    lastBeat = millis();
  }
  
  // Check for button press
  uint64_t btn = checkButtons();
  
  if (btn & UP_BTN_MASK) {
    /* s_println("UP BUTTON PRESSED!") */
    if (!isLogging) {
      startBLE();
      showMessage("READY", "for BLE");
    } else {
      updateHRDisplay(true);
    }
  }
  
  if (btn & DOWN_BTN_MASK) {
    /* s_println("DOWN BUTTON PRESSED!") */
    if (isLogging) {
      /* s_println("Setting isLogging to false...") */
      isLogging = false; // Set flag first
      delay(200); // Give runLoggingLoop time to exit
      stopLogging(); // Then cleanup
      showMessage("STOPPED", "Press UP", "to restart");
    }
  }
  
  delay(50);
}