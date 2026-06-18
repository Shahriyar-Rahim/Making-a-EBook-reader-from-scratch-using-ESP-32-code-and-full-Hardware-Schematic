// ═══════════════════════════════════════════════════════════════════
//  camera_engine.cpp  —  OV2640 camera capture + cloud OCR / Vision AI
//
//  This entire file compiles to a no-op stub unless CAMERA_MODULE_ENABLED
//  is defined in platformio.ini build_flags, since the base hardware
//  (per your veroboard) does not include a camera. Wire one up and add
//  the flag when ready — no other code changes are needed, the rest of
//  the firmware checks camera_is_available() before using these calls.
// ═══════════════════════════════════════════════════════════════════
#include "camera_engine.h"
#include <SD.h>
#include <Preferences.h>
#include <algorithm>  // std::sort (gallery newest-first ordering)

extern SemaphoreHandle_t sd_mutex;

#ifdef CAMERA_MODULE_ENABLED

#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <base64.h>
#include "ai_engine.h"   // reuse ai_is_configured / provider plumbing

// ── Camera pin map ──────────────────────────────────────────────────
// Adjust to match your actual module wiring. These defaults follow the
// common ESP32-S3-EYE / Freenove WROVER-CAM pin convention; re-pin as
// needed for a custom breakout on your veroboard.
#define CAM_PIN_PWDN     -1
#define CAM_PIN_RESET    -1
#define CAM_PIN_XCLK     15
#define CAM_PIN_SIOD     4
#define CAM_PIN_SIOC     5
#define CAM_PIN_D7       16
#define CAM_PIN_D6       17
#define CAM_PIN_D5       18
#define CAM_PIN_D4       12
#define CAM_PIN_D3       10
#define CAM_PIN_D2       8
#define CAM_PIN_D1       9
#define CAM_PIN_D0       11
#define CAM_PIN_VSYNC    6
#define CAM_PIN_HREF     7
#define CAM_PIN_PCLK     13

static bool camera_ready = false;

bool camera_init() {
    camera_config_t config = {};
    config.ledc_channel = LEDC_CHANNEL_2;
    config.ledc_timer   = LEDC_TIMER_2;
    config.pin_d0 = CAM_PIN_D0;  config.pin_d1 = CAM_PIN_D1;
    config.pin_d2 = CAM_PIN_D2;  config.pin_d3 = CAM_PIN_D3;
    config.pin_d4 = CAM_PIN_D4;  config.pin_d5 = CAM_PIN_D5;
    config.pin_d6 = CAM_PIN_D6;  config.pin_d7 = CAM_PIN_D7;
    config.pin_xclk     = CAM_PIN_XCLK;
    config.pin_pclk     = CAM_PIN_PCLK;
    config.pin_vsync    = CAM_PIN_VSYNC;
    config.pin_href     = CAM_PIN_HREF;
    config.pin_sccb_sda = CAM_PIN_SIOD;
    config.pin_sccb_scl = CAM_PIN_SIOC;
    config.pin_pwdn      = CAM_PIN_PWDN;
    config.pin_reset     = CAM_PIN_RESET;
    config.xclk_freq_hz  = 20000000;
    config.pixel_format  = PIXFORMAT_JPEG;
    config.frame_size    = FRAMESIZE_VGA;     // 640x480 — good OCR balance
    config.jpeg_quality  = 10;                // lower = better quality
    config.fb_count      = 2;
    config.fb_location   = CAMERA_FB_IN_PSRAM;
    config.grab_mode     = CAMERA_GRAB_LATEST;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("[Camera] init failed: 0x%x\n", err);
        camera_ready = false;
        return false;
    }
    camera_ready = true;
    Serial.println("[Camera] Initialized OK.");
    return true;
}

bool camera_is_available() { return camera_ready; }

String camera_capture_to_sd() {
    if (!camera_ready) return "";

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) return "";

    if (!SD.exists("/Photos")) SD.mkdir("/Photos");

    String path = "/Photos/IMG_" + String(millis()) + ".jpg";

    if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        File f = SD.open(path, FILE_WRITE);
        if (f) {
            f.write(fb->buf, fb->len);
            f.close();
        } else {
            path = "";
        }
        xSemaphoreGive(sd_mutex);
    } else {
        path = "";
    }

    esp_camera_fb_return(fb);
    return path;
}

std::vector<String> camera_list_gallery() {
    std::vector<String> out;
    if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return out;

    File dir = SD.open("/Photos");
    if (dir && dir.isDirectory()) {
        File entry = dir.openNextFile();
        while (entry) {
            if (!entry.isDirectory()) out.push_back(String("/Photos/") + entry.name());
            entry = dir.openNextFile();
        }
        dir.close();
    }
    xSemaphoreGive(sd_mutex);

    // Newest first (filenames are millis()-based, so reverse-sort works)
    std::sort(out.begin(), out.end(), [](const String& a, const String& b) { return a > b; });
    return out;
}

// ── Cloud Vision OCR (Gemini Vision multimodal) ─────────────────────
static bool send_image_to_vision_api(const uint8_t* jpg_data, size_t jpg_len,
                                      const String& prompt_text,
                                      String& out_text, String& out_error) {
    if (!ai_is_configured()) {
        out_error = "AI is not configured. Add an API key in Settings > AI Assistant.";
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        out_error = "No internet connection — connect to Wi-Fi with internet access first.";
        return false;
    }

    String b64 = base64::encode(jpg_data, jpg_len);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient https;

    // Gemini Vision endpoint reused from ai_engine's key (Gemini supports
    // multimodal input on the same generateContent endpoint).
    Preferences prefs;
    prefs.begin("ai_cfg", true);
    String api_key = prefs.getString("api_key", "");
    prefs.end();

    if (api_key.length() == 0) {
        out_error = "No API key configured.";
        return false;
    }

    String url = "https://generativelanguage.googleapis.com/v1beta/models/"
                 "gemini-2.0-flash:generateContent?key=" + api_key;

    if (!https.begin(client, url)) {
        out_error = "Failed to open HTTPS connection.";
        return false;
    }
    https.addHeader("Content-Type", "application/json");
    https.setTimeout(30000);

    DynamicJsonDocument doc(jpg_len * 4 / 3 + 2048);
    JsonArray parts = doc["contents"][0]["parts"].to<JsonArray>();

    JsonObject text_part = parts.add<JsonObject>();
    text_part["text"] = prompt_text;

    JsonObject image_part = parts.add<JsonObject>();
    JsonObject inline_data = image_part["inline_data"].to<JsonObject>();
    inline_data["mime_type"] = "image/jpeg";
    inline_data["data"]      = b64;

    String body;
    serializeJson(doc, body);

    int code = https.POST(body);
    if (code != 200) {
        out_error = "Vision API error (HTTP " + String(code) + ")";
        https.end();
        return false;
    }

    String resp = https.getString();
    https.end();

    DynamicJsonDocument rdoc(16384);
    if (deserializeJson(rdoc, resp)) {
        out_error = "Failed to parse vision response.";
        return false;
    }
    const char* text = rdoc["candidates"][0]["content"]["parts"][0]["text"];
    if (!text) {
        out_error = "Empty vision response (possibly blocked).";
        return false;
    }
    out_text = String(text);
    return true;
}

void camera_scan_document(OCRCallback callback) {
    if (!camera_ready) {
        callback(false, "Camera not initialized.");
        return;
    }
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        callback(false, "Failed to capture frame.");
        return;
    }

    String text, error;
    bool ok = send_image_to_vision_api(
        fb->buf, fb->len,
        "Extract all readable text from this photographed book/document page, "
        "preserving paragraph breaks. Output plain text only, no commentary.",
        text, error);

    esp_camera_fb_return(fb);
    callback(ok, ok ? text : error);
}

void camera_visual_ai_query(const String& question, OCRCallback callback) {
    if (!camera_ready) {
        callback(false, "Camera not initialized.");
        return;
    }
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        callback(false, "Failed to capture frame.");
        return;
    }

    String text, error;
    bool ok = send_image_to_vision_api(fb->buf, fb->len, question, text, error);

    esp_camera_fb_return(fb);
    callback(ok, ok ? text : error);
}

// QR scanning from raw frames needs a decoder lib (e.g. quirc). Wiring
// quirc against the camera framebuffer is straightforward but adds a
// dependency; left as a documented hook so the rest of the UI can be
// built against a stable signature.
String camera_scan_qr_frame() {
    if (!camera_ready) return "";
    // TODO: integrate `quirc` library here:
    //   camera_fb_t* fb = esp_camera_fb_get();
    //   quirc_decode on fb->buf (convert JPEG->grayscale first), return payload
    //   esp_camera_fb_return(fb);
    return "";
}

#else  // ─── CAMERA_MODULE_ENABLED not defined: stub implementation ───

bool camera_init()            { Serial.println("[Camera] Module disabled in build."); return false; }
bool camera_is_available()    { return false; }
String camera_capture_to_sd() { return ""; }
std::vector<String> camera_list_gallery() { return {}; }
void camera_scan_document(OCRCallback callback) {
    callback(false, "Camera module not enabled in this build. Add a camera and "
                     "set -D CAMERA_MODULE_ENABLED in platformio.ini.");
}
String camera_scan_qr_frame() { return ""; }
void camera_visual_ai_query(const String&, OCRCallback callback) {
    callback(false, "Camera module not enabled in this build.");
}

#endif // CAMERA_MODULE_ENABLED