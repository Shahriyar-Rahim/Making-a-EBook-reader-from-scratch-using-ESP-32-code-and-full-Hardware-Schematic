#ifndef CAMERA_ENGINE_H
#define CAMERA_ENGINE_H

#include <Arduino.h>
#include <functional>
#include <vector>

// ─────────────────────────────────────────────
//  Requires an OV2640/OV5640-class camera module wired to the
//  ESP32-S3 camera-capable GPIOs (e.g. Freenove/ESP32-S3-CAM boards,
//  or a breakout wired to GPIO 4-18 range per your board's pinout).
//  This module is compiled only if CAMERA_MODULE_ENABLED is defined
//  in platformio.ini build_flags, since not all builds have the
//  hardware fitted.
// ─────────────────────────────────────────────

using OCRCallback = std::function<void(bool success, const String& text)>;

// Call once from setup() if the camera is physically present.
bool camera_init();
bool camera_is_available();

// Captures a JPEG frame and saves it to /Photos/IMG_<timestamp>.jpg
// Returns the saved path, or "" on failure.
String camera_capture_to_sd();

// Returns list of saved photo paths in /Photos, newest first.
std::vector<String> camera_list_gallery();

// Full scan pipeline: capture → (optional) perspective/edge crop →
// upload to cloud OCR (Gemini Vision / OpenAI Vision) → callback with
// recognized text. Runs the network call on Core 0; call from there.
void camera_scan_document(OCRCallback callback);

// QR / barcode scan from a live preview frame. Returns decoded string
// or "" if nothing found in the current frame — call in a polling loop
// while the QR-scan tab is open.
String camera_scan_qr_frame();

// Visual AI: capture + send to vision model with a custom question
// (e.g. "What is this circuit?" or "Solve this math problem").
void camera_visual_ai_query(const String& question, OCRCallback callback);

#endif // CAMERA_ENGINE_H