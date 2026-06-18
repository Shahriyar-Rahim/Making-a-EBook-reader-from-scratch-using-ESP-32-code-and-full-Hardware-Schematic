// ═══════════════════════════════════════════════════════════════════
//  network.cpp  —  Core-0 network task body: AP+STA Wi-Fi (via
//  wifi_manager), BLE note receiver, web dashboard, and draining the
//  UI's Wi-Fi connect-request queue.
//
//  OTA note: firmware updates are handled either by the SD-card
//  bootloader in main.cpp or by the /ota upload endpoint below.
//  This file must never include <WebServer.h> or <ElegantOTA.h> — both
//  pull in an unscoped C enum (http_method) whose bare names
//  (HTTP_GET, HTTP_POST, ...) collide with ESPAsyncWebServer.h's own
//  unscoped WebRequestMethod enum. Two unscoped enums sharing
//  enumerator names in one translation unit is a hard redefinition error.
// ═══════════════════════════════════════════════════════════════════
#include "network.h"
#include "ui_engine.h"
#include "wifi_manager.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <Update.h>
#include <SD.h>

// Note: ElegantOTA is intentionally NOT included here.
// ElegantOTA 2.x includes WebServer.h -> HTTP_Method.h which defines
// HTTP_GET, HTTP_POST etc. as a plain C enum. ESPAsyncWebServer also
// defines these identically as WebRequestMethod enum members. The
// two definitions conflict at compile time (redeclaration error).
// ElegantOTA 3.x dropped AsyncWebServer support. The workaround used
// here is a minimal manual OTA endpoint implemented directly with
// AsyncWebServer's upload API — same result, no dependency conflict.

AsyncWebServer server(80);

#define SERVICE_UUID        "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"

// BLE Inbound Note Stream
class InboundNoteReceiver: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        std::string payload = pCharacteristic->getValue();
        if (payload.length() > 0) {
            if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                File notebook = SD.open("/notes.txt", FILE_APPEND);
                if (notebook) {
                    notebook.printf("[BLE Note]: %s\n", payload.c_str());
                    notebook.close();
                    Serial.println("[BLE] Note appended via BLE stream.");
                }
                xSemaphoreGive(sd_mutex);
            }
        }
    }
};

// ─────────────────────────────────────────────
//  OTA upload state (lives on Core 0 only)
// ─────────────────────────────────────────────
static bool ota_in_progress = false;

static void handle_ota_upload(AsyncWebServerRequest* request,
                               const String& filename,
                               size_t index, uint8_t* data,
                               size_t len, bool final) {
    if (index == 0) {
        Serial.printf("[OTA] Upload start: %s\n", filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            Update.printError(Serial);
        }
        ota_in_progress = true;
    }

    if (Update.write(data, len) != len) {
        Update.printError(Serial);
    }

    if (final) {
        if (Update.end(true)) {
            Serial.printf("[OTA] Upload complete: %u bytes. Rebooting...\n", index + len);
            post_toast("OTA done — rebooting");
            delay(500);
            ESP.restart();
        } else {
            Update.printError(Serial);
            post_toast("OTA failed — check serial");
        }
        ota_in_progress = false;
    }
}

void init_network_subsystems() {
    // 1. Wi-Fi: AP always-on + best-effort STA reconnect to a saved network.
    //    wifi_manager_init() starts the AP and kicks off a saved-credential
    //    join attempt; wifi_manager_tick() (called below in the loop)
    //    retries periodically and handles NTP sync once connected.
    wifi_manager_init();
    post_network_status(true, false);   // AP is up immediately

    // 2. BLE note receiver
    BLEDevice::init("ESP32_Reader_Workspace");
    BLEServer *pServer = BLEDevice::createServer();
    BLEService *pService = pServer->createService(SERVICE_UUID);
    BLECharacteristic *pChar = pService->createCharacteristic(
                                CHARACTERISTIC_UUID,
                                BLECharacteristic::PROPERTY_WRITE
                               );
    pChar->setCallbacks(new InboundNoteReceiver());
    pService->start();
    pServer->getAdvertising()->start();

    post_network_status(true, true);

    // 3. Web dashboard
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Control Deck</title>";
        html += "<style>body{background:#0b0f19;color:#e2e8f0;font-family:sans-serif;padding:30px;}";
        html += ".card{background:#1e293b;padding:25px;border-radius:12px;margin-bottom:20px;box-shadow:0 4px 10px rgba(0,0,0,0.3);}";
        html += "input[type=number],textarea{background:#0f172a;color:#fff;border:1px solid #475569;padding:12px;border-radius:6px;width:100%;box-sizing:border-box;}";
        html += "button{background:#2563eb;color:#fff;border:none;padding:12px 20px;border-radius:6px;cursor:pointer;font-weight:bold;margin-top:10px;}";
        html += "button:hover{background:#1d4ed8;}</style></head><body>";
        html += "<h2>Workspace Hardware Management Portal</h2>";

        html += "<div class='card'><h3>Status</h3>";
        html += "<p>STA Network: " + (WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String("Not connected")) + "</p>";
        html += "<p>Battery: " + String(battery_percentage) + "%" + (is_charging ? " (charging)" : "") + "</p>";
        html += "</div>";

        html += "<div class='card'><h3>System Settings</h3>";
        html += "<form action='/update_sys' method='POST'>";
        html += "Overcharge Protection Threshold (%): <input type='number' name='limit' value='" + String(charge_limit_threshold) + "' min='50' max='100'><br><br>";
        html += "Backlight Brightness (%): <input type='number' name='bright' value='" + String(global_brightness) + "' min='10' max='100'><br><br>";
        html += "<button type='submit'>Sync System Profiles</button></form></div>";

        html += "<div class='card'><h3>Text Note Capture</h3>";
        html += "<form action='/save_note' method='POST'>";
        html += "<textarea name='note_content' rows='5' placeholder='Type or paste quotes here...'></textarea>";
        html += "<button type='submit'>Save to SD Card</button></form></div>";

        html += "<div class='card'><h3>Library</h3>";
        html += "<p>Upload .txt or .epub files to /Books/ via a card reader on the SD card.</p>";
        html += "</div>";
        html += "<div class='card'><h3>&#128260; OTA Firmware Update</h3>";
        html += "<p>Upload a compiled .bin firmware file and the device will reboot when done.</p>";
        html += "<form method='POST' action='/ota' enctype='multipart/form-data'>";
        html += "<input type='file' name='firmware' accept='.bin'>";
        html += "<button type='submit'>Flash Firmware</button></form></div>";

        html += "</body></html>";
        request->send(200, "text/html", html);
    });

    server.on("/update_sys", HTTP_POST, [](AsyncWebServerRequest *request){
        if (request->hasParam("limit", true)) {
            charge_limit_threshold = request->getParam("limit", true)->value().toInt();
        }
        if (request->hasParam("bright", true)) {
            int target_b = request->getParam("bright", true)->value().toInt();
            set_display_brightness(target_b);
        }
        request->redirect("/");
    });

    server.on("/save_note", HTTP_POST, [](AsyncWebServerRequest *request){
        if (request->hasParam("note_content", true)) {
            String txt = request->getParam("note_content", true)->value();
            if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                File f = SD.open("/notes.txt", FILE_APPEND);
                if (f) { f.printf("[Web Note]: %s\n", txt.c_str()); f.close(); }
                xSemaphoreGive(sd_mutex);
            }
        }
        request->redirect("/");
    });

    // Manual OTA endpoint — no ElegantOTA dependency
    server.on("/ota", HTTP_POST,
        [](AsyncWebServerRequest *request){ request->send(200, "text/plain", "OTA triggered"); },
        handle_ota_upload
    );

    server.begin();

    Serial.println("[Network] Subsystems initialized.");
}

void handle_network_comms() {
    // Retry/maintain the STA connection and run NTP sync when it lands
    wifi_manager_tick();

    // Drain Wi-Fi connect requests submitted from the Settings UI
    // (created lazily by reader_ui.cpp on first password-modal submit).
    if (wifi_connect_request_queue) {
        WiFiConnectRequest req;
        if (xQueueReceive(wifi_connect_request_queue, &req, 0) == pdTRUE) {
            bool ok = wifi_connect_sta(String(req.ssid), String(req.pass));
            post_toast(ok ? "Wi-Fi connected" : "Wi-Fi connection failed");
            post_network_status(true, true);
        }
    }
}