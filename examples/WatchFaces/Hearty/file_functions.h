#ifndef FILE_FUNCTIONS_H
#define FILE_FUNCTIONS_H

#include "settings.h"

#include <LittleFS.h>


struct LogEntry {
  unsigned long timestamp;
  uint8_t hr;
  #if ENABLE_RR_INTERVALS
    uint8_t rrCount;
    uint16_t rrIntervals[4];
  #endif
};


// Forward declarations for globals

// Global Buffer Variables
extern LogEntry logBuffer[LOG_BUFFER_SIZE];
extern uint16_t logBufferIndex;

#if ENABLE_RR_INTERVALS
extern uint16_t latestRRIntervals[4];
extern uint8_t rrIntervalCount;
#endif

inline void flushLogBuffer() {
  if (logBufferIndex == 0) return; // Nothing to write

  File file = LittleFS.open(HR_LOG_FILE_NAME, FILE_APPEND);
  if (file) {
    // Loop through the RAM buffer and write everything
    for (int i = 0; i < logBufferIndex; i++) {
      
      #if ENABLE_RR_INTERVALS
        if (logBuffer[i].rrCount > 0) {
          // Format: timestamp,hr,rr1,rr2,rr3...
          file.printf("%lu,%d,", logBuffer[i].timestamp, logBuffer[i].hr);
          for (int j = 0; j < logBuffer[i].rrCount; j++) {
            file.printf("%d", logBuffer[i].rrIntervals[j]);
            if (j < logBuffer[i].rrCount - 1) file.print(",");
          }
          file.println();
        } else {
          file.printf("%lu,%d,\n", logBuffer[i].timestamp, logBuffer[i].hr);
        }
      #else
        // Simple Format: timestamp,hr
        file.printf("%lu,%d\n", logBuffer[i].timestamp, logBuffer[i].hr);
      #endif
    }
    
    file.close(); // Close only ONCE after writing
    /* s_printf("Flushed %d readings to disk.\n", logBufferIndex) */
    
    // Reset the buffer
    logBufferIndex = 0;
  } else {
    /* s_printls("Failed to open log file for flushing") */
  }
}

inline void logHRToFile(uint8_t a_hr, uint8_t a_rrIntervalCount, uint16_t * ap_rr) {
  // Save data to the current slot in the Buffer
  logBuffer[logBufferIndex].timestamp = millis();
  logBuffer[logBufferIndex].hr = a_hr;

  #if ENABLE_RR_INTERVALS
    // Copy RR intervals safely
    uint8_t countToCopy = a_rrIntervalCount; 
    if (countToCopy > 4) countToCopy = 4; // Safety cap
    
    logBuffer[logBufferIndex].rrCount = countToCopy;
    for (int i = 0; i < countToCopy; i++) {
      logBuffer[logBufferIndex].rrIntervals[i] = ap_rr[i];
    }
  #endif

  // Increment Counter
  logBufferIndex++;

  // Check if Buffer is full
  if (logBufferIndex >= LOG_BUFFER_SIZE) {
    flushLogBuffer();
  }
}

#if 0
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
#endif


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

