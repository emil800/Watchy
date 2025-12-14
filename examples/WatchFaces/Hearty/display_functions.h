#ifndef DISPLAY_FUNCTIONS_H
#define DISPLAY_FUNCTIONS_H

#include "settings.h"
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include "images.h"

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
    display.setCursor(40, 20);
    display.print(line1);
    if (line2.length() > 0) {
      display.setCursor(40, 50);
      display.print(line2);
    }
    if (line3.length() > 0) {
      display.setCursor(40, 80);
      display.print(line3);
    }
  } while (display.nextPage());
}

void drawBatteryIcon(int x, int y) {
  // Dimensions
  int w = 30;
  int h = 14;
   
  // Get percentage 
  uint8_t percentage = getBatteryPercentage();

  // Draw the Outline (Body)
  display.drawRect(x, y, w, h, GxEPD_BLACK);
  
  // Draw the Terminal (Little nub on the right)
  display.fillRect(x + w, y + 4, 2, 6, GxEPD_BLACK);
  
  // Calculate Fill Width based on percentage
  // We leave a 2px margin inside the box
  int maxFillWidth = w - 4;
  int fillWidth = (maxFillWidth * percentage) / 100;
  
  // Draw the Fill (Level)
  if (fillWidth > 0) {
    display.fillRect(x + 2, y + 2, fillWidth, h - 4, GxEPD_BLACK);
  }

  // Print text percentage next to it
  display.setCursor(x + w + 5, y);
  display.print(percentage);
  display.print("%");
}

inline void updateHRDisplay(bool forceUpdate = false) {
  unsigned long now = millis();
  uint8_t percentage = getBatteryPercentage();
  const unsigned char* batteryPercentIcon = getBatteryBitmap(percentage);
  
  if (forceUpdate || 
      (hrReceived && latestHR != lastDisplayedHR) ||
      ((now - lastDisplayUpdate) > DISPLAY_UPDATE_INTERVAL_MS)) {
    
    /* s_printf("Display update: HR=%d\n", latestHR) */
    /* s_printMemoryStatus() */
    
    display.setPartialWindow(40, 0, 150, 100);

    display.firstPage();
    do {
      display.fillScreen(GxEPD_WHITE);

      // -- Battery Icon (Top Right) --
      display.drawBitmap(110, 20, batteryPercentIcon, 35, 16, GxEPD_BLACK);

      // -- Heart Icon (Middle Left) --
      // Your bitmap is 32x32.
      display.drawBitmap(40, 65, bitmap_heart, 32, 32, GxEPD_BLACK);

      // Draw text 
      display.setFont(&FreeMonoBold9pt7b);
      display.setTextColor(GxEPD_BLACK);

      // -- Battery Percentage --
      display.setCursor(150, 32); // To the right of the icon
      display.setTextSize(1);
      display.print(percentage);
      display.print("%");

      // -- HR Number --
      display.setCursor(85, 90);
      display.setTextSize(2);    // Large Text
      if (hrReceived) {
        display.print(latestHR);
      } else {
        display.print("--");
      }

      // -- BPM Label --
      display.setTextSize(1);    // Small Text
      display.print(" BPM");

    } while (display.nextPage());

    lastDisplayedHR = latestHR;
    lastDisplayUpdate = now;
    /* s_println("Display done") */
    /* s_printMemoryStatus() */
  }
}

inline void drawWave() {
  // Draw static wave
  display.drawBitmap(0, 100, bitmap_wave, 193, 64, GxEPD_BLACK);
}

#endif

