#ifndef UTILS_FUNCTIONS_H
#define UTILS_FUNCTIONS_H

#include "settings.h"
#include "esp_heap_caps.h"

inline uint64_t checkButtons() {
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


uint8_t getBatteryPercentage() {
  // Read Analog Value
  int rawValue = analogRead(BATTERY_PIN);
  
  // Convert to Voltage
  // 3.3V ref and voltage divider
  float voltage = (rawValue / 4095.0) * 3.3 * BAT_VOLTAGE_DIV; 
  
  // Convert to Percentage
  int percentage = map(voltage * 100, MIN_VOLTAGE * 100, MAX_VOLTAGE * 100, 0, 100);
  
  // Clamp results
  if (percentage > 100) percentage = 100;
  if (percentage < 0) percentage = 0;
  
  return (uint8_t)percentage;
}

#endif

