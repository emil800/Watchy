#ifndef WIFI_FUNCTIONS_H
#define WIFI_FUNCTIONS_H

#include "settings.h"

#include <WiFi.h>
#include <WebServer.h>
#include "display_functions.h"
#include <LittleFS.h>
#include "sleep_functions.h"

// Forward declarations for globals
extern WebServer server;
extern bool keepWebserverAlive;
extern bool isLogging;

inline void startWebServer() {
  /* s_println("Starting WiFi web server...") */
  

  if (!LittleFS.begin(true)) {
    /* s_println("LittleFS mount failed for web server") */
    return;
  }
  
  IPAddress ip;
  
  // Connect to existing WiFi network
  WiFi.mode(WIFI_STA);
  /* s_printf("Connecting to WiFi: %s\n", WIFI_SSID) */

  // Configure static IP
  IPAddress local_IP STATIC_IP_ADDRESS;
  IPAddress gateway STATIC_GATEWAY;
  IPAddress subnet STATIC_SUBNET;
  IPAddress primaryDNS(8, 8, 8, 8);  // Google DNS (optional)
  
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS)) {
    /* s_println("WiFi static IP config failed!") */
  } else {
    /* s_printf("Static IP configured: %d.%d.%d.%d\n", 
                 local_IP[0], local_IP[1], local_IP[2], local_IP[3]) */
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    ip = WiFi.localIP();
    /* s_println("\nWiFi connected!") */
    /* s_printf("IP address: %s\n", ip.toString().c_str()) */
  } else {
    /* s_println("\nWiFi connection failed!") */
    showMessage("WiFi Failed", "Check credentials");
    return;
  }
  #if ACCESS_POINT_ENABLED
  // Create Access Point mode
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
  ip = WiFi.softAPIP();
  /* s_printf("WiFi AP started: %s\n", ip.toString().c_str()) */
  #endif
  
  server.on("/", HTTP_GET, []() {
    String html = "<!DOCTYPE html><html><head><title>Watchy HR Data</title></head><body>";
    html += "<h1>Watchy Heart Rate Data</h1>";
    html += "<p><a href='/download'>Download CSV File</a></p>";
    html += "<p><a href='/delete'>Delete Log File</a></p>";
    html += "<p><a href='/info'>File Info</a></p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
  });
  
  server.on("/download", HTTP_GET, []() {
    File file = LittleFS.open(HR_LOG_FILE_NAME, "r");
    if (file) {
      server.sendHeader("Content-Type", "text/csv");
      server.sendHeader("Content-Disposition", "attachment; filename=hr_log.csv");
      server.streamFile(file, "text/csv");
      file.close();
    } else {
      server.send(404, "text/plain", "File not found");
    }
  });
  
  server.on("/delete", HTTP_GET, []() {
    if (LittleFS.remove(HR_LOG_FILE_NAME)) {
      server.send(200, "text/html", "<h1>File Deleted</h1><p><a href='/'>Back</a></p>");
      DEBUG_PRINTLN("Log file deleted via web");
      DEBUG_FLUSH();
    } else {
      server.send(500, "text/plain", "Failed to delete file");
    }
  });
  
  server.on("/info", HTTP_GET, []() {
    File file = LittleFS.open(HR_LOG_FILE_NAME, "r");
    if (file) {
      size_t fileSize = file.size();
      file.close();
      String html = "<!DOCTYPE html><html><head><title>File Info</title></head><body>";
      html += "<h1>Log File Info</h1>";
      html += "<p>File: " + String(HR_LOG_FILE_NAME) + "</p>";
      html += "<p>Size: " + String(fileSize) + " bytes</p>";
      html += "<p><a href='/'>Back</a></p>";
      html += "</body></html>";
      server.send(200, "text/html", html);
    } else {
      server.send(404, "text/plain", "File not found");
    }
  });
  
  server.begin();
  keepWebserverAlive = true;
  
  char ipStr[20];
  sprintf(ipStr, "%s", ip.toString().c_str());
  showMessage("WiFi ACTIVE", ipStr, "w=download");
  
  /* s_println("Web server started. Visit http://") */
  /* s_println(ip.toString()) */
}

#endif

