#ifndef BLE_FUNCTIONS_H
#define BLE_FUNCTIONS_H

#include "settings.h"
#include <NimBLEDevice.h>
#include "display_functions.h"
#include "file_functions.h"
#include "utils_functions.h"
#include "sleep_functions.h"

// Forward declarations for globals
extern volatile int latestHR;
extern volatile bool hrReceived;
extern bool isLogging;
extern unsigned long lastDisplayUpdate;
extern NimBLEClient* globalClient;
extern int lastDisplayedHR;
extern NimBLEUUID serviceUUID;
extern NimBLEUUID charUUID;

#if ENABLE_RR_INTERVALS
extern float latestRRIntervals[4];
extern int rrIntervalCount;
#endif

inline void notifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
  if (length > 0) {
    uint8_t flags = pData[0];
    int hr = 0;
    int offset = 0;
    
    // Parse heart rate
    if (flags & 0x01) {
      // HR is uint16
      if (length >= 3) {
        hr = pData[1] | (pData[2] << 8);
        offset = 3;
      }
    } else {
      // HR is uint8
      if (length >= 2) {
        hr = pData[1];
        offset = 2;
      }
    }
    
    // Parse RR intervals if present
    #if ENABLE_RR_INTERVALS
    rrIntervalCount = 0;
    if (flags & 0x10) {  // Bit 4: RR intervals present
      // RR intervals are in 1/1024 second units
      while (offset + 1 < length && rrIntervalCount < 4) {
        uint16_t raw_rr = pData[offset] | (pData[offset + 1] << 8);
        // Convert from 1/1024s units to milliseconds
        latestRRIntervals[rrIntervalCount] = (raw_rr / 1024.0) * 1000.0;
        rrIntervalCount++;
        offset += 2;
      }
      
      if (rrIntervalCount > 0) {
        /* s_printf("RR intervals: ") */
        for (int i = 0; i < rrIntervalCount; i++) {
          /* s_printf("%.1fms", latestRRIntervals[i]) */
          if (i < rrIntervalCount - 1){
            /* s_print(", ") */
          }
        }
        /* s_println() */
      }
    }
    #endif
    
    if (hr >= HR_MIN_VALID && hr <= HR_MAX_VALID) {
      latestHR = hr;
      hrReceived = true;
      /* s_printf("HR: %d \n", hr) */
      
      // Log to file if enabled
      logHRToFile(hr);
    }
  }
}

inline void runLoggingLoop() {
  /* s_println("=== Logging Loop ===") */
  isLogging = true;
  lastDisplayUpdate = millis();
  
  showMessage("Logging...", "UP=Refresh", "DOWN=Stop");
  delay(2000);
  
  while (isLogging) {
    // Check connection first
    if (globalClient == nullptr || !globalClient->isConnected()) {
      /* s_println("Lost connection") */
      isLogging = false;
      break;
    }
    
    // Check buttons BEFORE doing display update (faster response)
    uint64_t btn = checkButtons();
    if (btn & DOWN_BTN_MASK) {
      /* s_println("Stop button pressed in logging loop") */
      isLogging = false;
      break; // Exit immediately
    } else if (btn & UP_BTN_MASK) {
      /* s_println("Refresh") */
      updateHRDisplay(true);
    }
    
    // Only update display if enough time has passed
    unsigned long now = millis();
    if ((now - lastDisplayUpdate) > DISPLAY_UPDATE_INTERVAL_MS){
      updateHRDisplay(false);
    }
    
    // Shorter delay for more responsive button checking
    delay(100);
    yield();
    
    // Double-check isLogging flag (in case it was set from main loop)
    if (!isLogging) {
      /* s_println("isLogging set to false, exiting") */
      break;
    }
  }
}

inline void stopLogging() {
  /* s_println("stopLogging() called") */
  
  // Always set flag first
  isLogging = false;
  
  // Cleanup BLE connection
  if (globalClient != nullptr) {
    if (globalClient->isConnected()) {
      /* s_println("Disconnecting BLE client...") */
      globalClient->disconnect();
      delay(300);
    }
    /* s_println("Deleting BLE client...") */
    NimBLEDevice::deleteClient(globalClient);
    globalClient = nullptr;
  }

  NimBLEDevice::deinit(true);
  /* s_println("BLE stopped") */
  printMemoryStatus();
  
  // Check if we should go to sleep (only if WiFi is also not active)
  extern bool keepWebserverAlive;
  if (!keepWebserverAlive) {
    delay(2000); // Give time to see "STOPPED" message
    goToDeepSleep();
  }
}

inline void startBLE() {
  /* s_println("=== Starting NimBLE ===") */
  /* s_printMemoryStatus() */
  
  esp_task_wdt_deinit();
  setCpuFrequencyMhz(240);
  
  hrReceived = false;
  latestHR = 0;
  
  NimBLEDevice::init(BLE_DEVICE_NAME);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  
  NimBLEDevice::setSecurityAuth(false, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  
  /* s_printMemoryStatus() */
  
  /* s_println("Scanning...") */
  NimBLEScan* pScan = NimBLEDevice::getScan();
  pScan->setActiveScan(BLE_SCAN_ACTIVE);
  pScan->setInterval(BLE_SCAN_INTERVAL_MS);
  pScan->setWindow(BLE_SCAN_WINDOW_MS);
  
  // getResults(duration) does a blocking scan and returns results
  /* s_printf("Starting scan (%d ms)...\n", BLE_SCAN_DURATION_MS) */

  NimBLEScanResults results = pScan->getResults(BLE_SCAN_DURATION_MS, false);
  
  /* s_printf("Scan complete. Found %d devices\n", results.getCount()) */
  
  // Find HR device and connect
  bool deviceFound = false;
  for (int i = 0; i < results.getCount(); i++) {
    const NimBLEAdvertisedDevice* device = results.getDevice(i);
    /* s_printf("Checking device %d: %s\n", i, device->getAddress().toString().c_str()) */
    
    if (device->haveServiceUUID() && device->isAdvertisingService(serviceUUID)) {
      /* s_printf("Found HR device: %s\n", device->getAddress().toString().c_str()) */
      deviceFound = true;
      
      // Connect immediately when found
      NimBLEClient* pClient = NimBLEDevice::createClient();
      
      delay(500);
      
      // connect() expects a pointer to NimBLEAdvertisedDevice
      if (!pClient->connect(device)) {
        /* s_println("Connect fail") */
        NimBLEDevice::deleteClient(pClient);
        NimBLEDevice::deinit(true);
        showMessage("Connect", "failed");
        delay(2000);
        return;
      }
      
      pScan->clearResults();
      
      /* s_println("Connected!") */
      /* s_printMemoryStatus() */
      
      delay(1000);
      NimBLERemoteService* pSvc = pClient->getService(serviceUUID);
      
      if (pSvc == nullptr) {
        /* s_println("No service") */
        pClient->disconnect();
        NimBLEDevice::deleteClient(pClient);
        NimBLEDevice::deinit(true);
        showMessage("Service", "not found");
        delay(2000);
        return;
      }
      
      NimBLERemoteCharacteristic* pChar = pSvc->getCharacteristic(charUUID);
      
      if (pChar == nullptr) {
        /* s_println("No char") */
        pClient->disconnect();
        NimBLEDevice::deleteClient(pClient);
        NimBLEDevice::deinit(true);
        showMessage("Char", "not found");
        delay(2000);
        return;
      }
      
      if (pChar->canNotify()) {
        if (pChar->subscribe(true, notifyCallback)) {
          /* s_println("Subscribed") */
          /* s_printMemoryStatus() */
          
          globalClient = pClient;
          
          int waitCount = 0;
          while (!hrReceived && waitCount < HR_WAIT_TIMEOUT_SEC) {
            delay(1000);
            waitCount++;
          }
          
          if (!hrReceived) {
            /* s_println("No HR data") */
            showMessage("No HR data", "Check sensor");
            delay(3000);
          }
          
          runLoggingLoop();
          
          // Cleanup after loop
          stopLogging();
          
        } else {
          /* s_println("Subscribe fail") */
          pClient->disconnect();
          NimBLEDevice::deleteClient(pClient);
          NimBLEDevice::deinit(true);
          showMessage("Subscribe", "failed");
          delay(2000);
        }
      } else {
        /* s_println("No notify") */
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
    /* s_println("No HR device found") */
    pScan->clearResults();
    NimBLEDevice::deinit(true);
    showMessage("No HR device", "found");
    delay(2000);
    return;
  }
}

#endif

