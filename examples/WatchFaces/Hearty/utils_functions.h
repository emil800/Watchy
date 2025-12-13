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

#endif

