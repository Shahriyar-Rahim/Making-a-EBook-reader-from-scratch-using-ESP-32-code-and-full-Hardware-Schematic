// ═══════════════════════════════════════════════════════════════════
//  audio_engine.cpp  —  I2S mic recording (INMP441-class) to WAV on SD,
//  and I2S speaker playback (MAX98357A-class) of WAV files.
//
//  Design notes:
//   - Recording streams samples off the I2S DMA buffer in small chunks
//     (audio_record_tick()) rather than buffering the whole clip in
//     RAM, since voice notes can run long and PSRAM is shared with
//     the canvas/book/AI buffers elsewhere in this firmware.
//   - The WAV header is written with a placeholder size first, then
//     patched (seek + rewrite) once recording stops and the real
//     length is known — standard trick for streamed WAV capture.
//   - Playback is a blocking loop reading WAV data and feeding it to
//     I2S_NUM_1; call it from Core 0 (the network/audio task), not
//     from the LVGL core, since it can run for the length of the clip.
// ═══════════════════════════════════════════════════════════════════
#include "audio_engine.h"
#include "ui_engine.h"   // post_toast / post_library_refresh
#include <driver/i2s.h>
#include <SD.h>
#include <algorithm>  // std::sort (voice note newest-first ordering)

extern SemaphoreHandle_t sd_mutex;

// ─────────────────────────────────────────────
//  I2S configuration constants
// ─────────────────────────────────────────────
#define SAMPLE_RATE       16000   // 16kHz mono — plenty for voice, keeps files small
#define BITS_PER_SAMPLE   16
#define MIC_DMA_BUF_LEN   512     // samples per DMA buffer

static bool mic_ready = false;
static bool spk_ready = false;

static bool is_recording = false;
static File record_file;
static uint32_t record_data_bytes = 0;
static uint32_t record_start_ms   = 0;

static volatile bool stop_playback_requested = false;
static volatile bool is_playing = false;

// ─────────────────────────────────────────────
//  WAV header (44 bytes, PCM, mono, 16-bit)
// ─────────────────────────────────────────────
#pragma pack(push, 1)
struct WavHeader {
    char     riff[4]       = {'R','I','F','F'};
    uint32_t chunk_size     = 0;            // file size - 8, patched on close
    char     wave[4]        = {'W','A','V','E'};
    char     fmt[4]         = {'f','m','t',' '};
    uint32_t fmt_size       = 16;
    uint16_t audio_format   = 1;            // PCM
    uint16_t num_channels   = 1;            // mono
    uint32_t sample_rate    = SAMPLE_RATE;
    uint32_t byte_rate      = SAMPLE_RATE * 1 * (BITS_PER_SAMPLE / 8);
    uint16_t block_align     = 1 * (BITS_PER_SAMPLE / 8);
    uint16_t bits_per_sample = BITS_PER_SAMPLE;
    char     data[4]        = {'d','a','t','a'};
    uint32_t data_size      = 0;            // patched on close
};
#pragma pack(pop)

// ─────────────────────────────────────────────
//  Init
// ─────────────────────────────────────────────
bool audio_init() {
    // ── Mic (I2S_NUM_0, RX only) ──
    i2s_config_t mic_cfg = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,  // INMP441 outputs 24-bit in a 32-bit frame
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = MIC_DMA_BUF_LEN,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };
    i2s_pin_config_t mic_pins = {
        .bck_io_num   = MIC_I2S_BCLK_PIN,
        .ws_io_num    = MIC_I2S_WS_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = MIC_I2S_DATA_PIN
    };

    esp_err_t err1 = i2s_driver_install(I2S_NUM_0, &mic_cfg, 0, nullptr);
    esp_err_t err2 = (err1 == ESP_OK) ? i2s_set_pin(I2S_NUM_0, &mic_pins) : err1;
    mic_ready = (err2 == ESP_OK);
    if (!mic_ready) {
        Serial.printf("[Audio] Mic I2S init failed: 0x%x\n", err2);
    }

    // ── Speaker (I2S_NUM_1, TX only) ──
    i2s_config_t spk_cfg = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = MIC_DMA_BUF_LEN,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };
    i2s_pin_config_t spk_pins = {
        .bck_io_num   = SPK_I2S_BCLK_PIN,
        .ws_io_num    = SPK_I2S_WS_PIN,
        .data_out_num = SPK_I2S_DATA_PIN,
        .data_in_num  = I2S_PIN_NO_CHANGE
    };

    esp_err_t err3 = i2s_driver_install(I2S_NUM_1, &spk_cfg, 0, nullptr);
    esp_err_t err4 = (err3 == ESP_OK) ? i2s_set_pin(I2S_NUM_1, &spk_pins) : err3;
    spk_ready = (err4 == ESP_OK);
    if (!spk_ready) {
        Serial.printf("[Audio] Speaker I2S init failed: 0x%x\n", err4);
    }

    if (mic_ready || spk_ready) {
        if (!SD.exists("/VoiceNotes")) SD.mkdir("/VoiceNotes");
    }

    Serial.printf("[Audio] Mic %s, Speaker %s.\n",
        mic_ready ? "ready" : "unavailable",
        spk_ready ? "ready" : "unavailable");

    return mic_ready || spk_ready;
}

bool audio_is_available() { return mic_ready && spk_ready; }

// ─────────────────────────────────────────────
//  Recording
// ─────────────────────────────────────────────
bool audio_record_start() {
    if (!mic_ready) {
        Serial.println("[Audio] Cannot record — mic not initialized.");
        return false;
    }
    if (is_recording) return false;

    if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return false;

    String path = "/VoiceNotes/VN_" + String(millis()) + ".wav";
    record_file = SD.open(path, FILE_WRITE);
    if (!record_file) {
        xSemaphoreGive(sd_mutex);
        return false;
    }

    // Write placeholder header — patched on stop with real sizes.
    WavHeader hdr;
    record_file.write((uint8_t*)&hdr, sizeof(hdr));

    record_data_bytes = 0;
    record_start_ms   = millis();
    is_recording       = true;

    // Clear any stale samples sitting in the DMA buffer from before
    i2s_zero_dma_buffer(I2S_NUM_0);

    xSemaphoreGive(sd_mutex);
    Serial.printf("[Audio] Recording started: %s\n", path.c_str());
    return true;
}

void audio_record_tick() {
    if (!is_recording) return;

    // Pull one DMA-buffer's worth of 32-bit mic frames, convert to
    // 16-bit PCM (INMP441 puts the 24-bit sample left-justified in the
    // top of the 32-bit word — shift down and truncate to 16-bit), and
    // stream straight to SD instead of accumulating in RAM.
    static int32_t raw_buf[MIC_DMA_BUF_LEN];
    size_t bytes_read = 0;

    esp_err_t err = i2s_read(I2S_NUM_0, raw_buf, sizeof(raw_buf), &bytes_read, 0);
    if (err != ESP_OK || bytes_read == 0) return;

    int samples = bytes_read / sizeof(int32_t);
    static int16_t pcm_buf[MIC_DMA_BUF_LEN];
    for (int i = 0; i < samples; i++) {
        pcm_buf[i] = (int16_t)(raw_buf[i] >> 14);  // 32-bit frame -> 16-bit PCM
    }

    if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        size_t written = record_file.write((uint8_t*)pcm_buf, samples * sizeof(int16_t));
        record_data_bytes += written;
        xSemaphoreGive(sd_mutex);
    }
}

String audio_record_stop() {
    if (!is_recording) return "";
    is_recording = false;

    String saved_path;
    if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        saved_path = record_file.name();

        // Patch the WAV header now that we know the real data length.
        uint32_t file_len  = sizeof(WavHeader) + record_data_bytes;
        uint32_t chunk_size = file_len - 8;

        record_file.seek(4);
        record_file.write((uint8_t*)&chunk_size, 4);

        record_file.seek(sizeof(WavHeader) - 4);
        record_file.write((uint8_t*)&record_data_bytes, 4);

        record_file.close();
        xSemaphoreGive(sd_mutex);
    }

    Serial.printf("[Audio] Recording stopped: %s (%lu bytes audio)\n",
        saved_path.c_str(), (unsigned long)record_data_bytes);
    return saved_path;
}

bool audio_is_recording() { return is_recording; }

uint32_t audio_record_elapsed_sec() {
    if (!is_recording) return 0;
    return (millis() - record_start_ms) / 1000;
}

// ─────────────────────────────────────────────
//  Playback
// ─────────────────────────────────────────────
void audio_play_file(const String& filepath, AudioDoneCallback callback) {
    if (!spk_ready) {
        if (callback) callback(false);
        return;
    }
    if (is_playing) {
        if (callback) callback(false);
        return;
    }

    if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        if (callback) callback(false);
        return;
    }
    File f = SD.open(filepath, FILE_READ);
    xSemaphoreGive(sd_mutex);

    if (!f) {
        if (callback) callback(false);
        return;
    }

    // Skip the 44-byte WAV header — this module only plays the mono
    // 16-bit PCM files it records itself, so no format negotiation.
    f.seek(44);

    is_playing = true;
    stop_playback_requested = false;

    static int16_t buf[1024];
    bool ok = true;

    while (f.available() && !stop_playback_requested) {
        size_t got;
        if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            got = f.read((uint8_t*)buf, sizeof(buf));
            xSemaphoreGive(sd_mutex);
        } else {
            got = 0;
        }
        if (got == 0) break;

        size_t bytes_written = 0;
        esp_err_t err = i2s_write(I2S_NUM_1, buf, got, &bytes_written, pdMS_TO_TICKS(500));
        if (err != ESP_OK) { ok = false; break; }
    }

    if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        f.close();
        xSemaphoreGive(sd_mutex);
    }

    i2s_zero_dma_buffer(I2S_NUM_1);
    is_playing = false;

    if (callback) callback(ok && !stop_playback_requested);
}

void audio_stop_playback() { stop_playback_requested = true; }
bool audio_is_playing()    { return is_playing; }

// ─────────────────────────────────────────────
//  Voice note library
// ─────────────────────────────────────────────
std::vector<VoiceNote> audio_list_voice_notes() {
    std::vector<VoiceNote> out;
    if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return out;

    if (!SD.exists("/VoiceNotes")) SD.mkdir("/VoiceNotes");

    File dir = SD.open("/VoiceNotes");
    if (dir && dir.isDirectory()) {
        File entry = dir.openNextFile();
        while (entry) {
            if (!entry.isDirectory()) {
                VoiceNote vn;
                vn.filename    = String("/VoiceNotes/") + entry.name();
                vn.size_bytes  = entry.size();
                // WAV byte_rate is fixed (16kHz, mono, 16-bit) = 32000 B/s
                uint32_t data_bytes = (vn.size_bytes > 44) ? (vn.size_bytes - 44) : 0;
                vn.duration_sec = data_bytes / (SAMPLE_RATE * 2);
                out.push_back(vn);
            }
            entry = dir.openNextFile();
        }
        dir.close();
    }
    xSemaphoreGive(sd_mutex);

    std::sort(out.begin(), out.end(),
        [](const VoiceNote& a, const VoiceNote& b) { return a.filename > b.filename; });
    return out;
}

bool audio_delete_voice_note(const String& filepath) {
    if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    bool ok = SD.remove(filepath);
    xSemaphoreGive(sd_mutex);
    return ok;
}

// ─────────────────────────────────────────────
//  Cross-core request queue
//  The Voice Notes UI (Core 1 / LVGL) only ever enqueues a request;
//  audio_task_tick(), called from the Core 0 network/audio task,
//  performs the actual blocking I2S/SD work and posts UI feedback
//  via post_toast()/post_library_refresh() (declared in ui_engine.h,
//  included at the top of this file).
// ─────────────────────────────────────────────
enum class AudioReqType { RECORD_START, RECORD_STOP, PLAYBACK };

struct AudioRequest {
    AudioReqType type;
    char         path[64];   // used only for PLAYBACK
};

static QueueHandle_t audio_request_queue = nullptr;

static void ensure_queue() {
    if (!audio_request_queue) {
        audio_request_queue = xQueueCreate(4, sizeof(AudioRequest));
    }
}

void audio_request_record_start() {
    ensure_queue();
    AudioRequest req = {};
    req.type = AudioReqType::RECORD_START;
    xQueueSend(audio_request_queue, &req, 0);
}

void audio_request_record_stop() {
    ensure_queue();
    AudioRequest req = {};
    req.type = AudioReqType::RECORD_STOP;
    xQueueSend(audio_request_queue, &req, 0);
}

void audio_request_playback(const String& filepath) {
    ensure_queue();
    AudioRequest req = {};
    req.type = AudioReqType::PLAYBACK;
    strncpy(req.path, filepath.c_str(), sizeof(req.path) - 1);
    xQueueSend(audio_request_queue, &req, 0);
}

void audio_task_tick() {
    // Keep streaming mic samples to SD while a recording is active,
    // regardless of whether a new request arrived this tick.
    if (is_recording) {
        audio_record_tick();
    }

    if (!audio_request_queue) return;

    AudioRequest req;
    if (xQueueReceive(audio_request_queue, &req, 0) != pdTRUE) return;

    switch (req.type) {
        case AudioReqType::RECORD_START: {
            bool ok = audio_record_start();
            post_toast(ok ? "Recording started" : "Failed to start recording (check mic)");
            break;
        }
        case AudioReqType::RECORD_STOP: {
            String path = audio_record_stop();
            post_toast(path.length() ? "Voice note saved" : "Recording save failed");
            post_library_refresh();   // library tab listens to the same refresh signal;
                                       // the Voice tab rebuilds its own list on next open
            break;
        }
        case AudioReqType::PLAYBACK: {
            String path = String(req.path);
            audio_play_file(path, [](bool success) {
                post_toast(success ? "Playback finished" : "Playback failed");
            });
            break;
        }
    }
}