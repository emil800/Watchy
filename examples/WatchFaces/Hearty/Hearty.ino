

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

// File system for logging
#if ENABLE_FILE_LOGGING
#include <FS.h>
#include <LittleFS.h>
#endif

// WiFi for web server (optional)
#if ENABLE_WIFI_SERVER
#include <WiFi.h>
#include <WebServer.h>
WebServer server(80);
bool keepWebserverAlive = false;
#endif

// Use NimBLE
#include <NimBLEDevice.h>

// Debug macros - conditionally compile Serial output
#if ENABLE_SERIAL_DEBUG
  #define DEBUG_PRINT(x)      Serial.print(x)
  #define DEBUG_PRINTLN(x)    Serial.println(x)
  #define DEBUG_PRINTF(...)   Serial.printf(__VA_ARGS__)
  #define DEBUG_FLUSH()       Serial.flush()
  #define DEBUG_BEGIN(x)      Serial.begin(x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(...)
  #define DEBUG_FLUSH()
  #define DEBUG_BEGIN(x)
#endif

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

// File logging function
void logHRToFile(int hr) {
  #if ENABLE_FILE_LOGGING
  File file = LittleFS.open(HR_LOG_FILE_NAME, FILE_APPEND);
  if (file) {
    unsigned long timestamp = millis();
    file.printf("%lu,%d\n", timestamp, hr);
    file.close();
    DEBUG_PRINTF("Logged HR: %d BPM to file\n", hr);
  } else {
    DEBUG_PRINTLN("Failed to open log file");
  }
  #endif
}

// Dump log file to Serial
void dumpLogFile() {
  #if ENABLE_FILE_LOGGING
  DEBUG_PRINTLN("\n=== Log File Contents ===");
  File file = LittleFS.open(HR_LOG_FILE_NAME, "r");
  if (file) {
    DEBUG_PRINTF("File size: %d bytes\n", file.size());
    DEBUG_PRINTLN("---");
    while (file.available()) {
      String line = file.readStringUntil('\n');
      DEBUG_PRINTLN(line);
    }
    file.close();
    DEBUG_PRINTLN("---");
    DEBUG_PRINTLN("=== End of File ===");
  } else {
    DEBUG_PRINTLN("Failed to open log file");
  }
  #else
  DEBUG_PRINTLN("File logging is disabled");
  #endif
}

// HR notification callback
void notifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
  if (length > 0) {
    uint8_t flags = pData[0];
    int hr = 0;

    if (flags & 0x01) {
      // HR is uint16
      if (length >= 3) {
        hr = pData[1] | (pData[2] << 8);
      }
    } else {
      // HR is uint8
      if (length >= 2) {
        hr = pData[1];
      }
    }

    if (hr >= HR_MIN_VALID && hr <= HR_MAX_VALID) {
      latestHR = hr;
      hrReceived = true;
      DEBUG_PRINTF("HR: %d BPM\n", hr);
      
      // Log to file if enabled
      logHRToFile(hr);
    }
  }
}

void printMemoryStatus() {
  DEBUG_PRINTLN("=== Memory ===");
  DEBUG_PRINTF("Free Heap: %d bytes\n", ESP.getFreeHeap());
  if (ESP.getPsramSize() > 0) {
    DEBUG_PRINTF("PSRAM: %d bytes total, %d free\n", ESP.getPsramSize(), ESP.getFreePsram());
    DEBUG_PRINTF("SPIRAM: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  } else {
    DEBUG_PRINTLN("PSRAM: Not available");
  }
  DEBUG_PRINTF("Internal RAM: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

void initDisplay() {
  display.init(DISPLAY_INIT_BAUD, true, DISPLAY_INIT_RESET_DELAY, DISPLAY_INIT_PARTIAL);
  display.setRotation(0);
  display.setTextColor(GxEPD_BLACK);
}

void showMessage(String line1, String line2 = "", String line3 = "") {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(10, 50);
    display.println(line1);
    if (line2.length() > 0) {
      display.setCursor(10, 80);
      display.println(line2);
    }
    if (line3.length() > 0) {
      display.setCursor(10, 110);
      display.println(line3);
    }
  } while (display.nextPage());
}

void updateHRDisplay(bool forceUpdate = false) {
  unsigned long now = millis();
  
  if (forceUpdate || 
      (hrReceived && latestHR != lastDisplayedHR) ||
      (now - lastDisplayUpdate > DISPLAY_UPDATE_INTERVAL_MS)) {
    
    DEBUG_PRINTF("Display update: HR=%d\n", latestHR);
    printMemoryStatus();
    
    display.setPartialWindow(0, 30, 200, 80);
    display.firstPage();
    do {
      display.fillScreen(GxEPD_WHITE);
      display.setFont(&FreeMonoBold9pt7b);
      display.setTextColor(GxEPD_BLACK);
      
      display.setCursor(10, 60);
      display.setTextSize(2);
      if (hrReceived) {
        display.print(latestHR);
      } else {
        display.print("--");
      }
      display.setTextSize(1);
      display.println(" BPM");
    } while (display.nextPage());
    
    lastDisplayedHR = latestHR;
    lastDisplayUpdate = now;
    
    DEBUG_PRINTLN("Display done");
    printMemoryStatus();
  }
}

uint64_t checkButtons() {
  // Watchy buttons are active LOW (pressed = 0)
  if (digitalRead(UP_BTN_PIN) == 0) {
    delay(50);
    if (digitalRead(UP_BTN_PIN) == 0) {
      while (digitalRead(UP_BTN_PIN) == 0) delay(10);
      return UP_BTN_MASK;
    }
  }
  if (digitalRead(DOWN_BTN_PIN) == 0) {
    delay(50);
    if (digitalRead(DOWN_BTN_PIN) == 0) {
      while (digitalRead(DOWN_BTN_PIN) == 0) delay(10);
      return DOWN_BTN_MASK;
    }
  }
  return 0;
}

void runLoggingLoop() {
  DEBUG_PRINTLN("=== Logging Loop ===");
  isLogging = true;
  lastDisplayUpdate = millis();
  
  showMessage("Logging...", "UP=Refresh", "DOWN=Stop");
  delay(2000);
  
  while (isLogging) {
    if (globalClient == nullptr || !globalClient->isConnected()) {
      DEBUG_PRINTLN("Lost connection");
      isLogging = false;
      break;
    }
    
    updateHRDisplay(false);
    
    uint64_t btn = checkButtons();
    if (btn & DOWN_BTN_MASK) {
      DEBUG_PRINTLN("Stop");
      isLogging = false;
      break;
    } else if (btn & UP_BTN_MASK) {
      DEBUG_PRINTLN("Refresh");
      updateHRDisplay(true);
    }
    
    delay(100);
    yield();
  }
  
  DEBUG_PRINTLN("=== Loop Exit ===");
}

void stopLogging() {
  if (isLogging) {
    DEBUG_PRINTLN("Stopping...");
    isLogging = false;

    if (globalClient != nullptr && globalClient->isConnected()) {
      globalClient->disconnect();
      delay(300);
    }
    
    if (globalClient != nullptr) {
      NimBLEDevice::deleteClient(globalClient);
      globalClient = nullptr;
    }

    NimBLEDevice::deinit(true);
    DEBUG_PRINTLN("Stopped");
    printMemoryStatus();
  }
}

void startBLE() {
  DEBUG_PRINTLN("=== Starting NimBLE ===");
  printMemoryStatus();
  
  esp_task_wdt_deinit();
  setCpuFrequencyMhz(240);
  
  hrReceived = false;
  latestHR = 0;
  
  DEBUG_PRINTLN("Init NimBLE...");
  NimBLEDevice::init(BLE_DEVICE_NAME);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  
  NimBLEDevice::setSecurityAuth(false, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  
  printMemoryStatus();
  
  DEBUG_PRINTLN("Scanning...");
  DEBUG_FLUSH();
  NimBLEScan* pScan = NimBLEDevice::getScan();
  pScan->setActiveScan(BLE_SCAN_ACTIVE);
  pScan->setInterval(BLE_SCAN_INTERVAL_MS);
  pScan->setWindow(BLE_SCAN_WINDOW_MS);
  
  // getResults(duration) does a blocking scan and returns results
  DEBUG_PRINTF("Starting scan (%d ms)...\n", BLE_SCAN_DURATION_MS);
  DEBUG_FLUSH();
  NimBLEScanResults results = pScan->getResults(BLE_SCAN_DURATION_MS, false);
  
  DEBUG_PRINTF("Scan complete. Found %d devices\n", results.getCount());
  DEBUG_FLUSH();
  
  // Find HR device and connect
  bool deviceFound = false;
  for (int i = 0; i < results.getCount(); i++) {
    const NimBLEAdvertisedDevice* device = results.getDevice(i);
    DEBUG_PRINTF("Checking device %d: %s\n", i, device->getAddress().toString().c_str());
    DEBUG_FLUSH();
    
    if (device->haveServiceUUID() && device->isAdvertisingService(serviceUUID)) {
      DEBUG_PRINTF("Found HR device: %s\n", device->getAddress().toString().c_str());
      DEBUG_FLUSH();
      deviceFound = true;
      
      // Connect immediately when found
      DEBUG_PRINTLN("Connecting...");
      DEBUG_FLUSH();
      NimBLEClient* pClient = NimBLEDevice::createClient();
      
      delay(500);
      
      // connect() expects a pointer to NimBLEAdvertisedDevice
      if (!pClient->connect(device)) {
        DEBUG_PRINTLN("Connect fail");
        DEBUG_FLUSH();
        NimBLEDevice::deleteClient(pClient);
        NimBLEDevice::deinit(true);
        showMessage("Connect", "failed");
        delay(2000);
        return;
      }
      
      pScan->clearResults();
      
      DEBUG_PRINTLN("Connected!");
      DEBUG_FLUSH();
      printMemoryStatus();
      
      delay(1000);
      NimBLERemoteService* pSvc = pClient->getService(serviceUUID);
      
      if (pSvc == nullptr) {
        DEBUG_PRINTLN("No service");
        DEBUG_FLUSH();
        pClient->disconnect();
        NimBLEDevice::deleteClient(pClient);
        NimBLEDevice::deinit(true);
        showMessage("Service", "not found");
        delay(2000);
        return;
      }
      
      NimBLERemoteCharacteristic* pChar = pSvc->getCharacteristic(charUUID);
      
      if (pChar == nullptr) {
        DEBUG_PRINTLN("No char");
        DEBUG_FLUSH();
        pClient->disconnect();
        NimBLEDevice::deleteClient(pClient);
        NimBLEDevice::deinit(true);
        showMessage("Char", "not found");
        delay(2000);
        return;
      }
      
      DEBUG_PRINTLN("Setup notify...");
      DEBUG_FLUSH();
      
      if (pChar->canNotify()) {
        if (pChar->subscribe(true, notifyCallback)) {
          DEBUG_PRINTLN("Subscribed");
          DEBUG_FLUSH();
          printMemoryStatus();
          
          globalClient = pClient;
          
      DEBUG_PRINTLN("Waiting for HR...");
      DEBUG_FLUSH();
      int waitCount = 0;
      while (!hrReceived && waitCount < HR_WAIT_TIMEOUT_SEC) {
        delay(1000);
        waitCount++;
      }
          
          if (!hrReceived) {
            DEBUG_PRINTLN("No HR data");
            DEBUG_FLUSH();
            showMessage("No HR data", "Check sensor");
            delay(3000);
          }
          
          runLoggingLoop();
          
          // Cleanup after loop
          stopLogging();
          
        } else {
          DEBUG_PRINTLN("Subscribe fail");
          DEBUG_FLUSH();
          pClient->disconnect();
          NimBLEDevice::deleteClient(pClient);
          NimBLEDevice::deinit(true);
          showMessage("Subscribe", "failed");
          delay(2000);
        }
      } else {
        DEBUG_PRINTLN("No notify");
        DEBUG_FLUSH();
        pClient->disconnect();
        NimBLEDevice::deleteClient(pClient);
        NimBLEDevice::deinit(true);
        showMessage("No notify");
        delay(2000);
      }
      
      break; // Exit loop after processing device
    }
  }
  
  if (!deviceFound) {
    DEBUG_PRINTLN("No HR device found");
    DEBUG_FLUSH();
    pScan->clearResults();
    NimBLEDevice::deinit(true);
    showMessage("No HR device", "found");
    delay(2000);
    return;
  }
}

#if ENABLE_WIFI_SERVER
void startWebServer() {
  DEBUG_PRINTLN("Starting WiFi web server...");
  DEBUG_FLUSH();
  
  #if ENABLE_FILE_LOGGING
  if (!LittleFS.begin(true)) {
    DEBUG_PRINTLN("LittleFS mount failed for web server");
    DEBUG_FLUSH();
    return;
  }
  #endif
  
  IPAddress ip;
  
  #if WIFI_MODE_STATION
  // Connect to existing WiFi network
  WiFi.mode(WIFI_STA);
  DEBUG_PRINTF("Connecting to WiFi: %s\n", WIFI_SSID);
  DEBUG_FLUSH();
  
  #if STATIC_IP_ENABLED
  // Configure static IP
  IPAddress local_IP STATIC_IP_ADDRESS;
  IPAddress gateway STATIC_GATEWAY;
  IPAddress subnet STATIC_SUBNET;
  IPAddress primaryDNS(8, 8, 8, 8);  // Google DNS (optional)
  
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS)) {
    DEBUG_PRINTLN("WiFi static IP config failed!");
    DEBUG_FLUSH();
  } else {
    DEBUG_PRINTF("Static IP configured: %d.%d.%d.%d\n", 
                 local_IP[0], local_IP[1], local_IP[2], local_IP[3]);
    DEBUG_FLUSH();
  }
  #endif

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    DEBUG_PRINT(".");
    DEBUG_FLUSH();
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    ip = WiFi.localIP();
    DEBUG_PRINTLN("\nWiFi connected!");
    DEBUG_PRINTF("IP address: %s\n", ip.toString().c_str());
    DEBUG_FLUSH();
  } else {
    DEBUG_PRINTLN("\nWiFi connection failed!");
    DEBUG_FLUSH();
    showMessage("WiFi Failed", "Check credentials");
    return;
  }
  #else
  // Create Access Point mode
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
  ip = WiFi.softAPIP();
  DEBUG_PRINTF("WiFi AP started: %s\n", ip.toString().c_str());
  DEBUG_FLUSH();
  #endif
  
  server.on("/", HTTP_GET, []() {
    String html = "<!DOCTYPE html><html><head><title>Watchy HR Data</title></head><body>";
    html += "<h1>Watchy Heart Rate Data</h1>";
    html += "<p><a href='/download'>Download CSV File</a></p>";
    html += "<p><a href='/delete'>Delete Log File</a></p>";
    html += "<p><a href='/info'>File Info</a></p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
  });
  
  server.on("/download", HTTP_GET, []() {
    #if ENABLE_FILE_LOGGING
    File file = LittleFS.open(HR_LOG_FILE_NAME, "r");
    if (file) {
      server.sendHeader("Content-Type", "text/csv");
      server.sendHeader("Content-Disposition", "attachment; filename=hr_log.csv");
      server.streamFile(file, "text/csv");
      file.close();
      DEBUG_PRINTLN("File downloaded via web");
      DEBUG_FLUSH();
    } else {
      server.send(404, "text/plain", "File not found");
    }
    #else
    server.send(404, "text/plain", "File logging disabled");
    #endif
  });
  
  server.on("/delete", HTTP_GET, []() {
    #if ENABLE_FILE_LOGGING
    if (LittleFS.remove(HR_LOG_FILE_NAME)) {
      server.send(200, "text/html", "<h1>File Deleted</h1><p><a href='/'>Back</a></p>");
      DEBUG_PRINTLN("Log file deleted via web");
      DEBUG_FLUSH();
    } else {
      server.send(500, "text/plain", "Failed to delete file");
    }
    #else
    server.send(404, "text/plain", "File logging disabled");
    #endif
  });
  
  server.on("/info", HTTP_GET, []() {
    #if ENABLE_FILE_LOGGING
    File file = LittleFS.open(HR_LOG_FILE_NAME, "r");
    if (file) {
      size_t fileSize = file.size();
      file.close();
      String html = "<!DOCTYPE html><html><head><title>File Info</title></head><body>";
      html += "<h1>Log File Info</h1>";
      html += "<p>File: " + String(HR_LOG_FILE_NAME) + "</p>";
      html += "<p>Size: " + String(fileSize) + " bytes</p>";
      html += "<p><a href='/'>Back</a></p>";
      html += "</body></html>";
      server.send(200, "text/html", html);
    } else {
      server.send(404, "text/plain", "File not found");
    }
    #else
    server.send(404, "text/plain", "File logging disabled");
    #endif
  });
  
  server.begin();
  keepWebserverAlive = true;
  
  char ipStr[20];
  sprintf(ipStr, "%s", ip.toString().c_str());
  showMessage("WiFi ACTIVE", ipStr, "w=download");
  
  DEBUG_PRINTLN("Web server started. Visit http://");
  DEBUG_PRINTLN(ip.toString());
  DEBUG_FLUSH();
}
#endif

void setup() {
  // 0. IMMEDIATE FEEDBACK: Vibrate to prove code is running
  pinMode(VIB_MOTOR_PIN, OUTPUT);
  digitalWrite(VIB_MOTOR_PIN, true);
  delay(200);
  digitalWrite(VIB_MOTOR_PIN, false);

  // 1. CRITICAL: Wait for Serial to be ready on ESP32-S3
  #if ENABLE_SERIAL_DEBUG
  DEBUG_BEGIN(115200);
  
  // Force wait for Serial with timeout
  unsigned long start = millis();
  while (!Serial && (millis() - start) < 3000) {
    delay(10);
  }
  #endif
  
  delay(500); // Extra delay for stability
  
  // Force output even if Serial not ready
  DEBUG_PRINTLN();
  DEBUG_PRINTLN();
  DEBUG_PRINTLN("=================================");
  DEBUG_PRINTLN("=== WATCHY HR (NimBLE) START ===");
  DEBUG_PRINTLN("=================================");
  DEBUG_FLUSH();
  delay(100);
  
  // Check PSRAM (optional)
  if (ESP.getPsramSize() > 0) {
    DEBUG_PRINTF("PSRAM detected: %d bytes total, %d free\n", ESP.getPsramSize(), ESP.getFreePsram());
  } else {
    DEBUG_PRINTLN("PSRAM: Not available (not required)");
  }
  DEBUG_FLUSH();
  
  printMemoryStatus();
  
  DEBUG_PRINTLN("Setting up pins...");
  DEBUG_FLUSH();
  
  // Setup pins
  pinMode(UP_BTN_PIN, INPUT_PULLUP);
  pinMode(DOWN_BTN_PIN, INPUT_PULLUP);
  pinMode(BACK_BTN_PIN, INPUT_PULLUP);
  pinMode(MENU_BTN_PIN, INPUT_PULLUP);
  
  DEBUG_PRINTLN("Initializing NVS...");
  DEBUG_FLUSH();
  
  // Initialize LittleFS for file logging
  #if ENABLE_FILE_LOGGING
  DEBUG_PRINTLN("Initializing LittleFS...");
  DEBUG_FLUSH();
  if (!LittleFS.begin(true)) {
    DEBUG_PRINTLN("LittleFS mount failed!");
    DEBUG_FLUSH();
  } else {
    DEBUG_PRINTLN("LittleFS mounted successfully");
    DEBUG_FLUSH();
    
    // Create/clear log file header if file doesn't exist or is empty
    File file = LittleFS.open(HR_LOG_FILE_NAME, "r");
    if (!file || file.size() == 0) {
      if (file) file.close();
      file = LittleFS.open(HR_LOG_FILE_NAME, "w");
      if (file) {
        file.println("timestamp_ms,heart_rate_bpm");
        file.close();
        DEBUG_PRINTLN("Created log file with header");
        DEBUG_FLUSH();
      }
    } else {
      size_t fileSize = file.size();
      file.close();
      DEBUG_PRINTF("Log file exists, size: %d bytes\n", fileSize);
      DEBUG_FLUSH();
    }
  }
  #endif
  
  // Initialize NVS (only erase if needed - avoids erasing on every boot)
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    DEBUG_PRINTLN("NVS partition was truncated and needs to be erased");
    DEBUG_FLUSH();
    nvs_flash_erase();
    ret = nvs_flash_init();
  }
  if (ret != ESP_OK) {
    DEBUG_PRINTF("NVS init failed: %d\n", ret);
    DEBUG_FLUSH();
  } else {
    DEBUG_PRINTLN("NVS initialized");
    DEBUG_FLUSH();
  }
  
  // PSRAM setup (only if available)
  if (ESP.getPsramSize() > 0) {
    DEBUG_PRINTLN("PSRAM available - enabling for large allocations");
    DEBUG_FLUSH();
    heap_caps_malloc_extmem_enable(4096);
  } else {
    DEBUG_PRINTLN("PSRAM not available - using internal RAM only");
    DEBUG_FLUSH();
  }
  
  // Init display
  DEBUG_PRINTLN("Initializing display...");
  DEBUG_FLUSH();

  // CRITICAL: Watchy 3.0 uses custom SPI pins. MUST init SPI before display!
  DEBUG_PRINTLN("Starting SPI...");
  DEBUG_FLUSH();
  SPI.begin(WATCHY_V3_SCK, WATCHY_V3_MISO, WATCHY_V3_MOSI, WATCHY_V3_SS);
  delay(100);
  
  DEBUG_PRINTLN("Initializing display...");
  DEBUG_FLUSH();
  
  bool displayOk = false;
  try {
    initDisplay();
    DEBUG_PRINTLN("Display initialized!");
    DEBUG_FLUSH();
    displayOk = true;
  } catch (...) {
    DEBUG_PRINTLN("ERROR: Display init failed in try block!");
    DEBUG_FLUSH();
  }
  
  if (displayOk) {
    try {
      showMessage("READY", "", "Press UP");
      DEBUG_PRINTLN("Display message shown!");
      DEBUG_FLUSH();
    } catch (...) {
      DEBUG_PRINTLN("ERROR: Display message failed!");
      DEBUG_FLUSH();
    }
  } else {
    DEBUG_PRINTLN("WARNING: Display not initialized, continuing anyway...");
    DEBUG_FLUSH();
  }
  
  DEBUG_PRINTLN("=================================");
  DEBUG_PRINTLN("Ready! Waiting for button press...");
  DEBUG_PRINTLN("UP button = Start BLE");
  DEBUG_PRINTLN("DOWN button = Stop logging");
  #if ENABLE_SERIAL_DEBUG && ENABLE_FILE_LOGGING
  DEBUG_PRINTLN("Serial commands: 'd'=dump, 's'=size, 'h'=help");
  #if ENABLE_WIFI_SERVER
  DEBUG_PRINTLN("Serial command: 'w'=start WiFi server");
  #endif
  #endif
  DEBUG_PRINTLN("=================================");
  DEBUG_FLUSH();
}

void loop() {
  // Handle Serial commands for file access
  #if ENABLE_SERIAL_DEBUG && ENABLE_FILE_LOGGING
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();
    if (cmd == "d" || cmd == "dump" || cmd == "download") {
      dumpLogFile();
    } else if (cmd == "s" || cmd == "size") {
      File file = LittleFS.open(HR_LOG_FILE_NAME, "r");
      if (file) {
        DEBUG_PRINTF("Log file size: %d bytes\n", file.size());
        file.close();
      } else {
        DEBUG_PRINTLN("Log file not found");
      }
    } else if (cmd == "h" || cmd == "help") {
      DEBUG_PRINTLN("\n=== Serial Commands ===");
      DEBUG_PRINTLN("d, dump, download - Dump log file contents");
      DEBUG_PRINTLN("s, size - Show log file size");
      DEBUG_PRINTLN("h, help - Show this help");
      #if ENABLE_WIFI_SERVER
      DEBUG_PRINTLN("w, wifi - Start WiFi web server");
      #endif
      DEBUG_PRINTLN("======================\n");
    }
    #if ENABLE_WIFI_SERVER
    else if (cmd == "w" || cmd == "wifi") {
      startWebServer();
    }
    #endif
  }
  #endif
  
  #if ENABLE_WIFI_SERVER
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
        DEBUG_PRINTLN("WiFi server stopped");
        DEBUG_FLUSH();
      }
    }
    delay(1);
    return; // Don't process buttons while server is running
  }
  #endif
  
  // Heartbeat to show loop is running
  static unsigned long lastBeat = 0;
  if (millis() - lastBeat > 5000) {
    DEBUG_PRINTLN("[Loop running...]");
    DEBUG_FLUSH();
    lastBeat = millis();
  }
  
  // Check for button press
  uint64_t btn = checkButtons();
  
  if (btn & UP_BTN_MASK) {
    DEBUG_PRINTLN("!!! UP BUTTON PRESSED !!!");
    DEBUG_FLUSH();
    if (!isLogging) {
      startBLE();
      showMessage("READY", "", "for BLE");
    } else {
      updateHRDisplay(true);
    }
  }
  
  if (btn & DOWN_BTN_MASK) {
    DEBUG_PRINTLN("!!! DOWN BUTTON PRESSED !!!");
    DEBUG_FLUSH();
    if (isLogging) {
      stopLogging();
      showMessage("STOPPED", "Press UP", "to restart");
    }
  }
  
  delay(50);
}