// ═══════════════════════════════════════════════════════════════════
//  wifi_manager.cpp  —  AP+STA coexistence, saved-network reconnect,
//  and NTP time sync for the Workbench Reader.
// ═══════════════════════════════════════════════════════════════════
#include "wifi_manager.h"
#include <WiFi.h>
#include <Preferences.h>
#include <time.h>

static const char* AP_SSID = "ESP32_Workbench";
static const char* AP_PASS = "workbench1234";

static bool sta_connecting = false;
static unsigned long last_retry_attempt = 0;
static const unsigned long RETRY_INTERVAL_MS = 30000;

void wifi_manager_init() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.printf("[WiFi] AP started: %s\n", AP_SSID);

    Preferences prefs;
    prefs.begin("wifi_cfg", true);
    String saved_ssid = prefs.getString("ssid", "");
    String saved_pass = prefs.getString("pass", "");
    prefs.end();

    if (saved_ssid.length() > 0) {
        Serial.printf("[WiFi] Attempting saved STA connection to %s...\n", saved_ssid.c_str());
        WiFi.begin(saved_ssid.c_str(), saved_pass.c_str());
        sta_connecting = true;
    }
}

void wifi_manager_tick() {
    if (WiFi.status() == WL_CONNECTED) {
        if (sta_connecting) {
            sta_connecting = false;
            Serial.printf("[WiFi] STA connected: %s (%s)\n",
                WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
            ntp_sync_now();
        }
        return;
    }

    unsigned long now = millis();
    if (now - last_retry_attempt < RETRY_INTERVAL_MS) return;
    last_retry_attempt = now;

    Preferences prefs;
    prefs.begin("wifi_cfg", true);
    String saved_ssid = prefs.getString("ssid", "");
    String saved_pass = prefs.getString("pass", "");
    prefs.end();

    if (saved_ssid.length() > 0) {
        WiFi.begin(saved_ssid.c_str(), saved_pass.c_str());
        sta_connecting = true;
    }
}

std::vector<WiFiScanResult> wifi_scan_networks() {
    std::vector<WiFiScanResult> out;
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {
        WiFiScanResult r;
        r.ssid    = WiFi.SSID(i);
        r.rssi    = WiFi.RSSI(i);
        r.secured = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
        out.push_back(r);
    }
    WiFi.scanDelete();
    return out;
}

bool wifi_connect_sta(const String& ssid, const String& password) {
    WiFi.begin(ssid.c_str(), password.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        delay(250);
    }

    bool ok = (WiFi.status() == WL_CONNECTED);
    if (ok) {
        Preferences prefs;
        prefs.begin("wifi_cfg", false);
        prefs.putString("ssid", ssid);
        prefs.putString("pass", password);
        prefs.end();
        ntp_sync_now();
    }
    return ok;
}

void wifi_forget_saved() {
    Preferences prefs;
    prefs.begin("wifi_cfg", false);
    prefs.clear();
    prefs.end();
    WiFi.disconnect();
}

bool wifi_has_internet() { return WiFi.status() == WL_CONNECTED; }

String wifi_get_sta_ssid() {
    return (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : "";
}

// ─────────────────────────────────────────────
//  NTP
// ─────────────────────────────────────────────
void ntp_sync_now() {
    if (WiFi.status() != WL_CONNECTED) return;
    configTime(6 * 3600, 0, "pool.ntp.org", "time.google.com");  // default: UTC+6 (Bangladesh)
    Serial.println("[NTP] Sync requested.");
}

bool ntp_get_time(int& hour, int& minute, int& second) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 100)) return false;
    hour   = timeinfo.tm_hour;
    minute = timeinfo.tm_min;
    second = timeinfo.tm_sec;
    return true;
}

String ntp_get_date_string() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 100)) return "----/--/--";
    char buf[16];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
        timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
    return String(buf);
}