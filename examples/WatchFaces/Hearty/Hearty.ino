// Standalone Watchy HR Monitor - NO Watchy library dependency
// Install: NimBLE-Arduino from Library Manager
// Board: ESP32S3 Dev Module
// PSRAM: Not required (works with or without PSRAM)

#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Wire.h>
#include <DS3232RTC.h>
#include <nvs_flash.h>
#include "esp_task_wdt.h"
#include "esp_heap_caps.h"

// Use NimBLE
#include <NimBLEDevice.h>

// Watchy 3.0 Pin Definitions

#define WATCHY_V3_SDA 12
#define WATCHY_V3_SCL 11

#define WATCHY_V3_SS    33
#define WATCHY_V3_MOSI  48
#define WATCHY_V3_MISO  46
#define WATCHY_V3_SCK   47

#define MENU_BTN_PIN  7
#define BACK_BTN_PIN  6
#define UP_BTN_PIN    0
#define DOWN_BTN_PIN  8

#define DISPLAY_CS    33
#define DISPLAY_DC    34
#define DISPLAY_RES   35
#define DISPLAY_BUSY  36
#define ACC_INT_1_PIN 14
#define ACC_INT_2_PIN 13
#define VIB_MOTOR_PIN 17
#define BATT_ADC_PIN 9
#define CHRG_STATUS_PIN 10
#define USB_DET_PIN 21
#define RTC_INT_PIN -1 //not used

#define MENU_BTN_MASK (BIT64(7))
#define BACK_BTN_MASK (BIT64(6))
#define UP_BTN_MASK   (BIT64(0))
#define DOWN_BTN_MASK (BIT64(8))
#define ACC_INT_MASK  (BIT64(14))
#define BTN_PIN_MASK  MENU_BTN_MASK|BACK_BTN_MASK|UP_BTN_MASK|DOWN_BTN_MASK


// Display
GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(GxEPD2_154_D67(DISPLAY_CS, DISPLAY_DC, DISPLAY_RES, DISPLAY_BUSY));

// BLE UUIDs for Heart Rate Service
static NimBLEUUID serviceUUID((uint16_t)0x180D);
static NimBLEUUID charUUID((uint16_t)0x2A37);

// Global state
volatile int latestHR = 0;
volatile bool hrReceived = false;
bool isLogging = false;
unsigned long lastDisplayUpdate = 0;
const unsigned long DISPLAY_UPDATE_INTERVAL = 3000;

NimBLEClient* globalClient = nullptr;
int lastDisplayedHR = 0;

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

    if (hr > 0 && hr < 255) {
      latestHR = hr;
      hrReceived = true;
      Serial.printf("HR: %d BPM\n", hr);
    }
  }
}

void printMemoryStatus() {
  Serial.println("=== Memory ===");
  Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
  if (ESP.getPsramSize() > 0) {
    Serial.printf("PSRAM: %d bytes total, %d free\n", ESP.getPsramSize(), ESP.getFreePsram());
    Serial.printf("SPIRAM: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  } else {
    Serial.println("PSRAM: Not available");
  }
  Serial.printf("Internal RAM: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

void initDisplay() {
  display.init(115200, true, 2, false);
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
      (now - lastDisplayUpdate > DISPLAY_UPDATE_INTERVAL)) {
    
    Serial.printf("Display update: HR=%d\n", latestHR);
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
    
    Serial.println("Display done");
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
  Serial.println("=== Logging Loop ===");
  isLogging = true;
  lastDisplayUpdate = millis();
  
  showMessage("Logging...", "UP=Refresh", "DOWN=Stop");
  delay(2000);
  
  while (isLogging) {
    if (globalClient == nullptr || !globalClient->isConnected()) {
      Serial.println("Lost connection");
      isLogging = false;
      break;
    }
    
    updateHRDisplay(false);
    
    uint64_t btn = checkButtons();
    if (btn & DOWN_BTN_MASK) {
      Serial.println("Stop");
      isLogging = false;
      break;
    } else if (btn & UP_BTN_MASK) {
      Serial.println("Refresh");
      updateHRDisplay(true);
    }
    
    delay(100);
    yield();
  }
  
  Serial.println("=== Loop Exit ===");
}

void stopLogging() {
  if (isLogging) {
    Serial.println("Stopping...");
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
    Serial.println("Stopped");
    printMemoryStatus();
  }
}

void startBLE() {
  Serial.println("=== Starting NimBLE ===");
  printMemoryStatus();
  
  esp_task_wdt_deinit();
  setCpuFrequencyMhz(240);
  
  hrReceived = false;
  latestHR = 0;
  
  Serial.println("Init NimBLE...");
  NimBLEDevice::init("Watchy-HR");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  
  NimBLEDevice::setSecurityAuth(false, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  
  printMemoryStatus();
  
  Serial.println("Scanning...");
  Serial.flush();
  NimBLEScan* pScan = NimBLEDevice::getScan();
  pScan->setActiveScan(true);
  pScan->setInterval(100);
  pScan->setWindow(99);
  
  // getResults(duration) does a blocking scan and returns results
  Serial.println("Starting scan (5 seconds)...");
  Serial.flush();
  NimBLEScanResults results = pScan->getResults(5000, false);
  
  Serial.printf("Scan complete. Found %d devices\n", results.getCount());
  Serial.flush();
  
  // Find HR device and connect
  bool deviceFound = false;
  for (int i = 0; i < results.getCount(); i++) {
    const NimBLEAdvertisedDevice* device = results.getDevice(i);
    Serial.printf("Checking device %d: %s\n", i, device->getAddress().toString().c_str());
    Serial.flush();
    
    if (device->haveServiceUUID() && device->isAdvertisingService(serviceUUID)) {
      Serial.printf("Found HR device: %s\n", device->getAddress().toString().c_str());
      Serial.flush();
      deviceFound = true;
      
      // Connect immediately when found
      Serial.println("Connecting...");
      Serial.flush();
      NimBLEClient* pClient = NimBLEDevice::createClient();
      
      delay(500);
      
      // connect() expects a pointer to NimBLEAdvertisedDevice
      if (!pClient->connect(device)) {
        Serial.println("Connect fail");
        Serial.flush();
        NimBLEDevice::deleteClient(pClient);
        NimBLEDevice::deinit(true);
        showMessage("Connect", "failed");
        delay(2000);
        return;
      }
      
      pScan->clearResults();
      
      Serial.println("Connected!");
      Serial.flush();
      printMemoryStatus();
      
      delay(1000);
      NimBLERemoteService* pSvc = pClient->getService(serviceUUID);
      
      if (pSvc == nullptr) {
        Serial.println("No service");
        Serial.flush();
        pClient->disconnect();
        NimBLEDevice::deleteClient(pClient);
        NimBLEDevice::deinit(true);
        showMessage("Service", "not found");
        delay(2000);
        return;
      }
      
      NimBLERemoteCharacteristic* pChar = pSvc->getCharacteristic(charUUID);
      
      if (pChar == nullptr) {
        Serial.println("No char");
        Serial.flush();
        pClient->disconnect();
        NimBLEDevice::deleteClient(pClient);
        NimBLEDevice::deinit(true);
        showMessage("Char", "not found");
        delay(2000);
        return;
      }
      
      Serial.println("Setup notify...");
      Serial.flush();
      
      if (pChar->canNotify()) {
        if (pChar->subscribe(true, notifyCallback)) {
          Serial.println("Subscribed");
          Serial.flush();
          printMemoryStatus();
          
          globalClient = pClient;
          
          Serial.println("Waiting for HR...");
          Serial.flush();
          int waitCount = 0;
          while (!hrReceived && waitCount < 10) {
            delay(1000);
            waitCount++;
          }
          
          if (!hrReceived) {
            Serial.println("No HR data");
            Serial.flush();
            showMessage("No HR data", "Check sensor");
            delay(3000);
          }
          
          runLoggingLoop();
          
          // Cleanup after loop
          stopLogging();
          
        } else {
          Serial.println("Subscribe fail");
          Serial.flush();
          pClient->disconnect();
          NimBLEDevice::deleteClient(pClient);
          NimBLEDevice::deinit(true);
          showMessage("Subscribe", "failed");
          delay(2000);
        }
      } else {
        Serial.println("No notify");
        Serial.flush();
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
    Serial.println("No HR device found");
    Serial.flush();
    pScan->clearResults();
    NimBLEDevice::deinit(true);
    showMessage("No HR device", "found");
    delay(2000);
    return;
  }
}

void setup() {
  // 0. IMMEDIATE FEEDBACK: Vibrate to prove code is running
  pinMode(VIB_MOTOR_PIN, OUTPUT);
  digitalWrite(VIB_MOTOR_PIN, true);
  delay(200);
  digitalWrite(VIB_MOTOR_PIN, false);

  // 1. CRITICAL: Wait for Serial to be ready on ESP32-S3
  Serial.begin(115200);
  
  // Force wait for Serial with timeout
  unsigned long start = millis();
  while (!Serial && (millis() - start) < 3000) {
    delay(10);
  }
  
  delay(500); // Extra delay for stability
  
  // Force output even if Serial not ready
  Serial.println();
  Serial.println();
  Serial.println("=================================");
  Serial.println("=== WATCHY HR (NimBLE) START ===");
  Serial.println("=================================");
  Serial.flush();
  delay(100);
  
  // Check PSRAM (optional)
  if (ESP.getPsramSize() > 0) {
    Serial.printf("PSRAM detected: %d bytes total, %d free\n", ESP.getPsramSize(), ESP.getFreePsram());
  } else {
    Serial.println("PSRAM: Not available (not required)");
  }
  Serial.flush();
  
  printMemoryStatus();
  
  Serial.println("Setting up pins...");
  Serial.flush();
  
  // Setup pins
  pinMode(UP_BTN_PIN, INPUT_PULLUP);
  pinMode(DOWN_BTN_PIN, INPUT_PULLUP);
  pinMode(BACK_BTN_PIN, INPUT_PULLUP);
  pinMode(MENU_BTN_PIN, INPUT_PULLUP);
  
  Serial.println("Initializing NVS...");
  Serial.flush();
  
  // Initialize NVS (only erase if needed - avoids erasing on every boot)
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    Serial.println("NVS partition was truncated and needs to be erased");
    Serial.flush();
    nvs_flash_erase();
    ret = nvs_flash_init();
  }
  if (ret != ESP_OK) {
    Serial.printf("NVS init failed: %d\n", ret);
    Serial.flush();
  } else {
    Serial.println("NVS initialized");
    Serial.flush();
  }
  
  // PSRAM setup (only if available)
  if (ESP.getPsramSize() > 0) {
    Serial.println("PSRAM available - enabling for large allocations");
    Serial.flush();
    heap_caps_malloc_extmem_enable(4096);
  } else {
    Serial.println("PSRAM not available - using internal RAM only");
    Serial.flush();
  }
  
  // Init display
  Serial.println("Initializing display...");
  Serial.flush();

  // CRITICAL: Watchy 3.0 uses custom SPI pins. MUST init SPI before display!
  Serial.println("Starting SPI...");
  Serial.flush();
  SPI.begin(WATCHY_V3_SCK, WATCHY_V3_MISO, WATCHY_V3_MOSI, WATCHY_V3_SS);
  delay(100);
  
  Serial.println("Initializing display...");
  Serial.flush();
  
  bool displayOk = false;
  try {
    initDisplay();
    Serial.println("Display initialized!");
    Serial.flush();
    displayOk = true;
  } catch (...) {
    Serial.println("ERROR: Display init failed in try block!");
    Serial.flush();
  }
  
  if (displayOk) {
    try {
      showMessage("READY", "", "Press UP");
      Serial.println("Display message shown!");
      Serial.flush();
    } catch (...) {
      Serial.println("ERROR: Display message failed!");
      Serial.flush();
    }
  } else {
    Serial.println("WARNING: Display not initialized, continuing anyway...");
    Serial.flush();
  }
  
  Serial.println("=================================");
  Serial.println("Ready! Waiting for button press...");
  Serial.println("UP button = Start BLE");
  Serial.println("DOWN button = Stop logging");
  Serial.println("=================================");
  Serial.flush();
}

void loop() {
  // Heartbeat to show loop is running
  static unsigned long lastBeat = 0;
  if (millis() - lastBeat > 5000) {
    Serial.println("[Loop running...]");
    Serial.flush();
    lastBeat = millis();
  }
  
  // Check for button press
  uint64_t btn = checkButtons();
  
  if (btn & UP_BTN_MASK) {
    Serial.println("!!! UP BUTTON PRESSED !!!");
    Serial.flush();
    if (!isLogging) {
      startBLE();
      showMessage("READY", "", "for BLE");
    } else {
      updateHRDisplay(true);
    }
  }
  
  if (btn & DOWN_BTN_MASK) {
    Serial.println("!!! DOWN BUTTON PRESSED !!!");
    Serial.flush();
    if (isLogging) {
      stopLogging();
      showMessage("STOPPED", "Press UP", "to restart");
    }
  }
  
  delay(50);
}