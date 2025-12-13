#ifndef FILE_FUNCTIONS_H
#define FILE_FUNCTIONS_H

#include "settings.h"

#include <LittleFS.h>

// Forward declarations for globals
#if ENABLE_RR_INTERVALS
extern float latestRRIntervals[4];
extern int rrIntervalCount;
#endif

inline void logHRToFile(int hr) {
  File file = LittleFS.open(HR_LOG_FILE_NAME, FILE_APPEND);
  if (file) {
    unsigned long timestamp = millis();
    
    #if ENABLE_RR_INTERVALS
    // Log with RR intervals
    if (rrIntervalCount > 0) {
      file.printf("%lu,%d,", timestamp, hr);
      for (int i = 0; i < rrIntervalCount; i++) {
        file.printf("%.1f", latestRRIntervals[i]);
        if (i < rrIntervalCount - 1) file.print(",");
      }
      file.println();
    } else {
      // No RR intervals, log without them
      file.printf("%lu,%d,\n", timestamp, hr);
    }
    #else
    // Log without RR intervals
    file.printf("%lu,%d\n", timestamp, hr);
    #endif
    
    file.close();
    /* s_printf("Logged HR: %d BPM", hr) */
    #if ENABLE_RR_INTERVALS
    if (rrIntervalCount > 0) {
      /* s_printf(" with %d RR intervals", rrIntervalCount) */
    }
    #endif
    /* s_println(" to file") */
  } else {
    /* s_println("Failed to open log file") */
  }
}

inline void dumpLogFile() {
  /* s_println("\n=== Log File Contents ===") */
  File file = LittleFS.open(HR_LOG_FILE_NAME, "r");
  if (file) {
    /* s_printf("File size: %d bytes\n", file.size()) */
    while (file.available()) {
      String line = file.readStringUntil('\n');
      /* s_println(line) */
    }
    file.close();
    /* s_println("=== End of File ===") */
  } else {
    /* s_println("Failed to open log file") */
  }
}

#endif

