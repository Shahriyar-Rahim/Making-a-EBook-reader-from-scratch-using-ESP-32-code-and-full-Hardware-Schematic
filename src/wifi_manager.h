#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

struct WiFiScanResult {
    String  ssid;
    int32_t rssi;
    bool    secured;
};

// ─────────────────────────────────────────────
//  Cross-core connect request queue. The Settings UI (Core 1 / LVGL)
//  posts requests here after the user enters a password; the network
//  task (Core 0) drains this queue and calls wifi_connect_sta(),
//  which blocks for up to ~15s — safe there, never on the UI core.
// ─────────────────────────────────────────────
struct WiFiConnectRequest {
    char ssid[33];
    char pass[65];
};
extern QueueHandle_t wifi_connect_request_queue;   // created lazily by the UI on first use

// Starts the local AP (always on, for the Control Deck dashboard) and
// attempts to also join a previously-saved home/office network in
// STA mode (AP+STA coexistence) so internet-dependent features
// (AI, NTP) work without dropping the local dashboard.
void wifi_manager_init();

// Call periodically (e.g. every 30s from the network task) to retry
// a saved connection if STA is not currently connected.
void wifi_manager_tick();

std::vector<WiFiScanResult> wifi_scan_networks();
bool wifi_connect_sta(const String& ssid, const String& password);
void wifi_forget_saved();
bool wifi_has_internet();   // true if STA is connected (best-effort proxy)
String wifi_get_sta_ssid();

// NTP
void   ntp_sync_now();
bool   ntp_get_time(int& hour, int& minute, int& second);
String ntp_get_date_string();

#endif // WIFI_MANAGER_H