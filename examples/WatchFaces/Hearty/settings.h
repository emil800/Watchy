#ifndef SETTINGS_H
#define SETTINGS_H

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

// Button Masks
#define MENU_BTN_MASK (BIT64(7))
#define BACK_BTN_MASK (BIT64(6))
#define UP_BTN_MASK   (BIT64(0))
#define DOWN_BTN_MASK (BIT64(8))
#define ACC_INT_MASK  (BIT64(14))
#define BTN_PIN_MASK  MENU_BTN_MASK|BACK_BTN_MASK|UP_BTN_MASK|DOWN_BTN_MASK

// BLE Configuration
#define BLE_SERVICE_UUID 0x180D  // Heart Rate Service
#define BLE_CHAR_UUID    0x2A37  // Heart Rate Measurement Characteristic
#define BLE_DEVICE_NAME   "Watchy-HR"

// BLE Scan Settings
#define BLE_SCAN_DURATION_MS  5000  // Scan duration in milliseconds
#define BLE_SCAN_INTERVAL_MS  100   // Scan interval in milliseconds
#define BLE_SCAN_WINDOW_MS    99    // Scan window in milliseconds
#define BLE_SCAN_ACTIVE       true  // Use active scanning

// Display Configuration
#define DISPLAY_UPDATE_INTERVAL_MS  5000  // Minimum time between display updates (ms)
#define DISPLAY_INIT_BAUD           115200
#define DISPLAY_INIT_RESET_DELAY    2
#define DISPLAY_INIT_PARTIAL        false

// HR Data Settings
#define HR_MIN_VALID  1   // Minimum valid heart rate
#define HR_MAX_VALID  255 // Maximum valid heart rate
#define HR_WAIT_TIMEOUT_SEC 10  // Timeout waiting for first HR reading
#define ENABLE_RR_INTERVALS true  // Set to true to log RR intervals (heart rate variability data)

// File Logging Configuration
#define HR_LOG_FILE_NAME "/hr_log.csv"  // File name for heart rate log

// WiFi Web Server Configuration (for downloading log file)
// Option 1: Connect to existing WiFi network
#define WIFI_SSID "Trololololo"   // Your WiFi network name
#define WIFI_PASSWORD "Peterpan555" // Your WiFi password
// Static IP Configuration (optional, leave 0,0,0,0 for DHCP)
#define STATIC_IP_ENABLED true
#define STATIC_IP_ADDRESS {192, 168, 0, 40}
#define STATIC_GATEWAY {192, 168, 0, 1}
#define STATIC_SUBNET {255, 255, 255, 0}

// Option 2: Access Point mode (fallback)
#define WIFI_AP_SSID "Watchy-HR"    // WiFi Access Point name (if WIFI_MODE_STATION is false)
#define WIFI_AP_PASS "12345678"     // WiFi Access Point password (min 8 chars)

// Deep Sleep Configuration
#define ENABLE_DEEP_SLEEP true  // Set to true to enable deep sleep when idle

#endif