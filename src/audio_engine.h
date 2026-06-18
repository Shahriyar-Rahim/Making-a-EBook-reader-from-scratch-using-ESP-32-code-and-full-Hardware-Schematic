#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

#include <Arduino.h>
#include <vector>
#include <functional>

// ─────────────────────────────────────────────
//  I2S Pin Map — INMP441 (mic, I2S_NUM_0) + MAX98357A (amp, I2S_NUM_1)
//  Adjust freely; these default to GPIOs not already used elsewhere
//  in ui_engine.h / camera_engine.cpp. Mic and speaker use SEPARATE
//  I2S peripherals (ESP32-S3 has two), so both can be wired without
//  needing a shared-bus switch.
// ─────────────────────────────────────────────

// INMP441 (or similar I2S MEMS mic) — read-only, I2S_NUM_0
#define MIC_I2S_BCLK_PIN   19   // SCK  (bit clock)
#define MIC_I2S_WS_PIN     20   // WS   (L/R clock, word select)
#define MIC_I2S_DATA_PIN   21   // SD   (serial data out from mic)

// MAX98357A (or similar I2S amp + speaker) — write-only, I2S_NUM_1
#define SPK_I2S_BCLK_PIN   35   // BCLK
#define SPK_I2S_WS_PIN     36   // LRC  (word select)
#define SPK_I2S_DATA_PIN   37   // DIN  (serial data into amp)
// MAX98357A's SD/shutdown pin can be tied to 3V3 (always on) or to a
// spare GPIO if you want software mute; not required for this module.

// ─────────────────────────────────────────────
//  Voice note metadata (one entry per recording on SD)
// ─────────────────────────────────────────────
struct VoiceNote {
    String   filename;      // e.g. /VoiceNotes/VN_1718000000.wav
    uint32_t duration_sec;
    uint32_t size_bytes;
};

using AudioDoneCallback = std::function<void(bool success)>;

// ─────────────────────────────────────────────
//  Public API
// ─────────────────────────────────────────────

// Call once from setup(). Configures both I2S peripherals.
// Safe to call even if the hardware isn't wired yet — failures are
// logged and audio_is_available() will report false.
bool audio_init();
bool audio_is_available();

// Starts recording from the mic to a new WAV file under /VoiceNotes.
// Non-blocking: spins up nothing extra — call audio_record_tick()
// periodically (e.g. every loop iteration) while recording is active
// to pull samples off the I2S DMA buffer and write them to SD.
bool   audio_record_start();
void   audio_record_tick();     // call repeatedly while recording
String audio_record_stop();     // returns saved file path, or "" on failure
bool   audio_is_recording();
uint32_t audio_record_elapsed_sec();

// Playback — blocking call intended for Core 0 (network/audio task),
// never the LVGL/UI core, since it streams the whole file via I2S.
// callback fires once playback finishes or fails.
void audio_play_file(const String& filepath, AudioDoneCallback callback);
void audio_stop_playback();     // request early stop from another task
bool audio_is_playing();
bool audio_is_paused();
uint32_t audio_playback_elapsed_sec();
uint32_t audio_playback_total_sec();
String audio_playback_filename();
void audio_request_pause_playback();
void audio_request_resume_playback();
void audio_request_seek_playback(uint32_t seconds);

// Library management
std::vector<VoiceNote> audio_list_voice_notes();
bool audio_delete_voice_note(const String& filepath);

// ─────────────────────────────────────────────
//  Cross-core request API
//  The Voice Notes UI lives on Core 1 (LVGL), but I2S record/playback
//  block for the length of the action — these calls just enqueue a
//  request; audio_task_tick() (called from the Core 0 network/audio
//  task, see main.cpp) services the queue and performs the actual
//  blocking work.
// ─────────────────────────────────────────────
void audio_request_record_start();
void audio_request_record_stop();
void audio_request_playback(const String& filepath);

// Called periodically from Core 0. Drains the request queue: starts/
// stops recording, runs playback to completion, and posts a toast +
// library-refresh UI event when an action finishes.
void audio_task_tick();

#endif // AUDIO_ENGINE_H