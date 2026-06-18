#ifndef UI_ENGINE_H
#define UI_ENGINE_H

#include <lvgl.h>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

// ─────────────────────────────────────────────
//  GPIO Pin Definitions
// ─────────────────────────────────────────────
#define TFT_BL_PIN       2   // PWM backlight
#define TOUCH_CS_PIN     4
#define TOUCH_IRQ_PIN    5
#define BATTERY_ADC_PIN  6   // Voltage-divider mid-point to GPIO 6 (ADC1_CH5)
#define CHARGE_STAT_PIN  7   // TP4056 CHRG pin → LOW = charging, HIGH = done/idle
#define BUZZER_PIN       8   // Passive piezo buzzer (optional, tie to GND if unused)

// ─────────────────────────────────────────────
//  UI Event System (cross-core safe queue)
// ─────────────────────────────────────────────
typedef enum {
    UI_EVT_BATTERY_UPDATE = 0,
    UI_EVT_NETWORK_STATUS,
    UI_EVT_SHOW_TOAST,
    UI_EVT_TIME_UPDATE,
    UI_EVT_AI_RESULT,        // AI response ready — show in modal
    UI_EVT_OCR_RESULT,       // OCR/camera result ready
    UI_EVT_LIBRARY_REFRESH,  // SD library scan finished
} UIEventType;

typedef struct {
    UIEventType type;
    int         val1;       // battery % / wifi bool / hour / ai success bool
    int         val2;       // charging  / ble bool  / minute
    char        str[48];    // toast message
    String*     heap_str;   // heap-allocated long text (AI/OCR results) — freed after use
} UIEventMsg;

extern QueueHandle_t ui_event_queue;

// ─────────────────────────────────────────────
//  Global State (declared here, defined in ui_engine.cpp)
// ─────────────────────────────────────────────
extern int   global_brightness;
extern int   battery_percentage;
extern bool  is_charging;
extern bool  overcharge_protection_enabled;
extern int   charge_limit_threshold;
extern bool  wifi_connected;
extern bool  ble_active;
extern bool  is_screen_locked;
extern bool  sd_card_ok;

// Active draw-tool colour (RGB565 packed) — set by colour picker
extern lv_color_t active_draw_color;
extern uint8_t    active_brush_size;    // 1–8 px radius

// ─────────────────────────────────────────────
//  Reader Theme & Font System
// ─────────────────────────────────────────────
typedef enum {
    THEME_DARK = 0,
    THEME_LIGHT,
    THEME_SEPIA,
    THEME_OLED_BLACK,
} ReaderTheme;

typedef enum {
    FONT_SMALL = 0,
    FONT_MEDIUM,
    FONT_LARGE,
    FONT_DYSLEXIC,   // uses a monospaced fallback font for letter clarity
} ReaderFontSize;

extern ReaderTheme    active_reader_theme;
extern ReaderFontSize active_reader_font;

// FreeRTOS resource guards
extern SemaphoreHandle_t lvgl_mutex;
extern SemaphoreHandle_t sd_mutex;

// ─────────────────────────────────────────────
//  Public API
// ─────────────────────────────────────────────
void init_ui_engine();

// Called from loop() to drain the cross-core event queue
void process_ui_events();

// Direct calls (must hold lvgl_mutex or be called from Core-1 LVGL context)
void set_display_brightness(int percentage);
void toggle_screen_lock();

// Convenience: post events from any core / any task
void post_battery_update(int percentage, bool charging);
void post_network_status(bool wifi, bool ble);
void post_toast(const char* msg);
void post_ai_result(bool success, const String& text);   // shows AI modal
void post_library_refresh();

// Canvas helpers
void canvas_clear();
bool canvas_save_bmp();   // saves /canvas.bmp to SD, returns success

// Reader helpers — called by library tab / reader tab
void reader_open_book(const String& filepath);
void reader_close_book();
void reader_render_current_page();
void library_rebuild_list();
void apply_reader_theme(ReaderTheme theme);
void apply_reader_font(ReaderFontSize size);

// ─────────────────────────────────────────────
//  Shared on-screen keyboard
//  One keyboard overlay is created once in init_ui_engine() and reused
//  by every textarea in the project (Notes, AI chat, Wi-Fi password,
//  AI API key, OCR result, ...). Call keyboard_attach(ta) once per
//  textarea right after creating it — this wires the textarea's
//  FOCUSED/DEFOCUSED events to show/hide the shared keyboard and
//  points the keyboard at that textarea while it's focused. No need
//  to call anything else; the keyboard manages its own visibility.
// ─────────────────────────────────────────────
void keyboard_attach(lv_obj_t* textarea);

#endif // UI_ENGINE_H