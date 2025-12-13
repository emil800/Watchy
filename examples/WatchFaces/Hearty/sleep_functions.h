#ifndef SLEEP_FUNCTIONS_H
#define SLEEP_FUNCTIONS_H

#include "settings.h"

#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "display_functions.h"

// Forward declarations for globals
extern GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display;
extern NimBLEClient* globalClient;

extern WebServer server;
extern bool keepWebserverAlive;

inline void goToDeepSleep() {
  /* s_println("Going to deep sleep...") */

  // Hibernate display to save power
  display.hibernate();
  
  // Clean up BLE if still active
  if (globalClient != nullptr) {
    if (globalClient->isConnected()) {
      globalClient->disconnect();
    }
    NimBLEDevice::deleteClient(globalClient);
    globalClient = nullptr;
  }
  NimBLEDevice::deinit(true);
  
  // Clean up WiFi if still active
  if (keepWebserverAlive) {
    server.stop();
    WiFi.mode(WIFI_OFF);
  }
  
  // Show message on display
  showMessage("Sleeping...", "Press button", "to wake");
  delay(1000);
  
  // Configure wake sources for ESP32-S3
  // Wake on button press
  esp_sleep_enable_ext1_wakeup(
      BTN_PIN_MASK,
      ESP_EXT1_WAKEUP_ANY_LOW);
  
  // Configure button pins for RTC domain
  rtc_gpio_set_direction((gpio_num_t)UP_BTN_PIN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en((gpio_num_t)UP_BTN_PIN);
  rtc_gpio_set_direction((gpio_num_t)DOWN_BTN_PIN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en((gpio_num_t)DOWN_BTN_PIN);
  rtc_gpio_set_direction((gpio_num_t)BACK_BTN_PIN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en((gpio_num_t)BACK_BTN_PIN);
  rtc_gpio_set_direction((gpio_num_t)MENU_BTN_PIN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en((gpio_num_t)MENU_BTN_PIN);
  
  // Wake on USB plug/unplug (optional)
  rtc_gpio_set_direction((gpio_num_t)USB_DET_PIN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en((gpio_num_t)USB_DET_PIN);
  bool usbPlugged = (digitalRead(USB_DET_PIN) == 1);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)USB_DET_PIN, usbPlugged ? LOW : HIGH);
  
  /* s_println("Entering deep sleep...") */
  delay(100);
  
  // Enter deep sleep
  esp_deep_sleep_start();
}

#endif