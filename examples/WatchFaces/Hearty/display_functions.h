#ifndef DISPLAY_FUNCTIONS_H
#define DISPLAY_FUNCTIONS_H

#include "settings.h"
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>

// Forward declarations for globals
extern GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display;
extern volatile int latestHR;
extern volatile bool hrReceived;
extern unsigned long lastDisplayUpdate;
extern int lastDisplayedHR;
extern void printMemoryStatus();

inline void initDisplay() {
  display.init(DISPLAY_INIT_BAUD, true, DISPLAY_INIT_RESET_DELAY, DISPLAY_INIT_PARTIAL);
  display.setRotation(0);
  display.setTextColor(GxEPD_BLACK);
}

inline void showMessage(String line1, String line2 = "", String line3 = "") {
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

inline void updateHRDisplay(bool forceUpdate = false) {
  unsigned long now = millis();
  
  if (forceUpdate || 
      (hrReceived && latestHR != lastDisplayedHR) ||
      ((now - lastDisplayUpdate) > DISPLAY_UPDATE_INTERVAL_MS)) {
    
    /* s_printf("Display update: HR=%d\n", latestHR) */
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
    
    /* s_println("Display done") */
    /* s_printMemoryStatus() */
  }
}

#endif

