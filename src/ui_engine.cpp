// ═══════════════════════════════════════════════════════════════════
//  ui_engine.cpp  —  Full LVGL UI for ESP32-S3 Workbench Assistant
//  Features: Tab navigation · Smooth canvas with undo · BMP save
//            Colour/brush picker · Toast overlay · Lock screen
//            Real ADC battery read · System info tab · Animations
// ═══════════════════════════════════════════════════════════════════
#include "ui_engine.h"
#include "reader_ui.h"
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SD.h>
#include <Preferences.h>
#include <driver/ledc.h>
#include <esp_adc_cal.h>

// ─────────────────────────────────────────────
//  Hardware objects
// ─────────────────────────────────────────────
XPT2046_Touchscreen ts(TOUCH_CS_PIN, TOUCH_IRQ_PIN);
Preferences          prefs;

// ─────────────────────────────────────────────
//  Global state
// ─────────────────────────────────────────────
int          global_brightness          = 80;
static int   saved_brightness           = 80;
int          battery_percentage         = 0;
bool         is_charging                = false;
bool         overcharge_protection_enabled = true;
int          charge_limit_threshold     = 80;
bool         wifi_connected             = false;
bool         ble_active                 = false;
bool         is_screen_locked           = false;
bool         sd_card_ok                 = false;
lv_color_t   active_draw_color;
uint8_t      active_brush_size          = 2;

SemaphoreHandle_t lvgl_mutex     = NULL;
SemaphoreHandle_t sd_mutex       = NULL;
QueueHandle_t     ui_event_queue = NULL;

// ─────────────────────────────────────────────
//  Canvas undo ring-buffer (3 snapshots in PSRAM)
// ─────────────────────────────────────────────
#define CANVAS_W   280
#define CANVAS_H   360
#define UNDO_DEPTH 3

static lv_color_t* canvas_buffer   = nullptr;
static lv_color_t* undo_stack[UNDO_DEPTH];
static int         undo_head       = 0;   // next write slot
static int         undo_count      = 0;
static lv_point_t  last_draw_pt    = {-1, -1};

// ─────────────────────────────────────────────
//  LVGL widget handles
// ─────────────────────────────────────────────
static lv_obj_t* scr_main        = nullptr;
static lv_obj_t* header_bar      = nullptr;
static lv_obj_t* battery_bar     = nullptr;
static lv_obj_t* battery_label   = nullptr;
static lv_obj_t* wifi_icon       = nullptr;
static lv_obj_t* ble_icon        = nullptr;
static lv_obj_t* sd_icon         = nullptr;
static lv_obj_t* time_label      = nullptr;
static lv_obj_t* tabview         = nullptr;
static lv_obj_t* tab_draw        = nullptr;
static lv_obj_t* tab_notes       = nullptr;
static lv_obj_t* tab_sys         = nullptr;
static lv_obj_t* canvas          = nullptr;
static lv_obj_t* notes_textarea  = nullptr;
static lv_obj_t* sys_battery_arc = nullptr;
static lv_obj_t* sys_info_label  = nullptr;
static lv_obj_t* toast_label     = nullptr;
static lv_obj_t* lock_overlay    = nullptr;
static lv_obj_t* shared_keyboard = nullptr;
static lv_timer_t* toast_timer   = nullptr;
static lv_obj_t* color_btns[6]   = {};    // preset colour swatches

// Colour palette for the draw tool
static const lv_color_t PALETTE[] = {
    { .full = 0xFFFF },  // White
    { .full = 0x001F },  // Blue
    { .full = 0xF800 },  // Red
    { .full = 0x07E0 },  // Green
    { .full = 0xFFE0 },  // Yellow
    { .full = 0x0000 },  // Black (eraser)
};
static const char* PALETTE_NAMES[] = { "W","B","R","G","Y","E" };

// ─────────────────────────────────────────────
//  Forward declarations (internal)
// ─────────────────────────────────────────────
static void build_header();
static void build_tab_draw();
static void build_tab_notes();
static void build_tab_sys();
static void build_lock_overlay();
static void build_shared_keyboard();
static void canvas_event_cb(lv_event_t* e);
static void color_btn_cb(lv_event_t* e);
static void brush_slider_cb(lv_event_t* e);
static void clear_btn_cb(lv_event_t* e);
static void undo_btn_cb(lv_event_t* e);
static void save_btn_cb(lv_event_t* e);
static void note_save_btn_cb(lv_event_t* e);
static void toast_timer_cb(lv_timer_t* t);
static void my_touchpad_read(lv_indev_drv_t* drv, lv_indev_data_t* data);
static void push_undo_snapshot();
static void draw_pixel_brush(lv_obj_t* cnv, int cx, int cy);

// ─────────────────────────────────────────────
//  ADC battery voltage reader
// ─────────────────────────────────────────────
static void read_battery_adc() {
    // 10-sample oversampled average
    uint32_t raw = 0;
    for (int i = 0; i < 10; i++) {
        raw += analogRead(BATTERY_ADC_PIN);
        delayMicroseconds(200);
    }
    raw /= 10;

    // Voltage divider: 100kΩ / 100kΩ → Vbat = Vadc × 2
    // ESP32-S3 ADC reference ≈ 3.3 V, 12-bit
    float vadc  = (raw / 4095.0f) * 3.3f;
    float vbat  = vadc * 2.0f;

    // LiPo: 3.0V = 0%, 4.2V = 100%
    int pct = (int)(((vbat - 3.0f) / (4.2f - 3.0f)) * 100.0f);
    pct = constrain(pct, 0, 100);

    bool charging = (digitalRead(CHARGE_STAT_PIN) == LOW);
    post_battery_update(pct, charging);
}

// ─────────────────────────────────────────────
//  Buzzer feedback (non-blocking, single tick)
// ─────────────────────────────────────────────
static void buzz(uint32_t freq_hz, uint32_t ms) {
    if (BUZZER_PIN < 0) return;
    ledcSetup(1, freq_hz, 8);
    ledcAttachPin(BUZZER_PIN, 1);
    ledcWrite(1, 128);
    delay(ms);
    ledcWrite(1, 0);
    ledcDetachPin(BUZZER_PIN);
}

// ─────────────────────────────────────────────
//  Brightness
// ─────────────────────────────────────────────
void set_display_brightness(int percentage) {
    global_brightness = constrain(percentage, 0, 100);
    int duty = map(global_brightness, 0, 100, 0, 255);
    ledcWrite(0, duty);
    // Persist
    prefs.begin("workbench", false);
    prefs.putInt("brightness", global_brightness);
    prefs.end();
}

// ─────────────────────────────────────────────
//  Lock / Unlock screen
// ─────────────────────────────────────────────
void toggle_screen_lock() {
    is_screen_locked = !is_screen_locked;

    if (is_screen_locked) {
        saved_brightness = global_brightness;
        set_display_brightness(10);  // dim, not off — user can still see button hint

        if (lock_overlay) {
            lv_obj_clear_flag(lock_overlay, LV_OBJ_FLAG_HIDDEN);
            // Fade-in animation
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
            lv_anim_set_var(&a, lock_overlay);
            lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
            lv_anim_set_time(&a, 350);
            lv_anim_start(&a);
        }
        buzz(1200, 40);
        Serial.println("[UI] Screen Locked.");
    } else {
        set_display_brightness(saved_brightness);

        if (lock_overlay) {
            // Fade-out animation, then hide
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
            lv_anim_set_var(&a, lock_overlay);
            lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
            lv_anim_set_time(&a, 300);
            lv_anim_set_deleted_cb(&a, [](lv_anim_t* a2) {
                lv_obj_add_flag((lv_obj_t*)a2->var, LV_OBJ_FLAG_HIDDEN);
            });
            lv_anim_start(&a);
        }
        buzz(1800, 30);
        Serial.println("[UI] Screen Unlocked.");
    }
}

// ─────────────────────────────────────────────
//  Toast notification
// ─────────────────────────────────────────────
static void toast_timer_cb(lv_timer_t* t) {
    if (toast_label) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
        lv_anim_set_var(&a, toast_label);
        lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
        lv_anim_set_time(&a, 300);
        lv_anim_set_deleted_cb(&a, [](lv_anim_t* a2) {
            lv_obj_add_flag((lv_obj_t*)a2->var, LV_OBJ_FLAG_HIDDEN);
        });
        lv_anim_start(&a);
    }
    lv_timer_del(t);
    toast_timer = nullptr;
}

static void show_toast_internal(const char* msg) {
    if (!toast_label) return;
    lv_label_set_text(toast_label, msg);
    lv_obj_clear_flag(toast_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(toast_label, LV_OPA_COVER, 0);

    if (toast_timer) lv_timer_del(toast_timer);
    toast_timer = lv_timer_create(toast_timer_cb, 2200, nullptr);
    lv_timer_set_repeat_count(toast_timer, 1);
}

// ─────────────────────────────────────────────
//  Cross-core event posting helpers
// ─────────────────────────────────────────────
void post_battery_update(int percentage, bool charging) {
    UIEventMsg msg = {};
    msg.type = UI_EVT_BATTERY_UPDATE;
    msg.val1 = percentage;
    msg.val2 = charging ? 1 : 0;
    xQueueSend(ui_event_queue, &msg, 0);
}

void post_network_status(bool wifi, bool ble) {
    UIEventMsg msg = {};
    msg.type = UI_EVT_NETWORK_STATUS;
    msg.val1 = wifi ? 1 : 0;
    msg.val2 = ble  ? 1 : 0;
    xQueueSend(ui_event_queue, &msg, 0);
}

void post_toast(const char* msg_str) {
    UIEventMsg msg = {};
    msg.type = UI_EVT_SHOW_TOAST;
    strncpy(msg.str, msg_str, sizeof(msg.str) - 1);
    msg.heap_str = nullptr;
    xQueueSend(ui_event_queue, &msg, 0);
}

void post_ai_result(bool success, const String& text) {
    UIEventMsg msg = {};
    msg.type = UI_EVT_AI_RESULT;
    msg.val1 = success ? 1 : 0;
    // AI/OCR responses can be long — heap-allocate rather than truncate
    // into the fixed 48-byte str[] buffer. Freed after consumption in
    // process_ui_events().
    msg.heap_str = new String(text);
    if (xQueueSend(ui_event_queue, &msg, pdMS_TO_TICKS(200)) != pdTRUE) {
        delete msg.heap_str;  // queue full — avoid leaking
    }
}

void post_library_refresh() {
    UIEventMsg msg = {};
    msg.type = UI_EVT_LIBRARY_REFRESH;
    msg.heap_str = nullptr;
    xQueueSend(ui_event_queue, &msg, 0);
}

// Called from loop() on Core 1, already inside lvgl_mutex
void process_ui_events() {
    UIEventMsg msg;
    while (xQueueReceive(ui_event_queue, &msg, 0) == pdTRUE) {
        switch (msg.type) {

        case UI_EVT_BATTERY_UPDATE: {
            battery_percentage = msg.val1;
            is_charging        = (msg.val2 == 1);

            // Overcharge protection
            if (is_charging && overcharge_protection_enabled &&
                battery_percentage >= charge_limit_threshold) {
                is_charging = false;
                lv_label_set_text_fmt(battery_label,
                    LV_SYMBOL_STOP " %d%%", charge_limit_threshold);
                lv_obj_set_style_bg_color(battery_bar,
                    lv_color_make(34, 197, 94), LV_PART_INDICATOR);
            } else if (is_charging) {
                static bool anim_tog = false;
                anim_tog = !anim_tog;
                lv_label_set_text_fmt(battery_label,
                    "%s %d%%", anim_tog ? LV_SYMBOL_CHARGE : LV_SYMBOL_POWER,
                    battery_percentage);
                lv_obj_set_style_bg_color(battery_bar,
                    lv_color_make(234, 179, 8), LV_PART_INDICATOR);
            } else {
                lv_label_set_text_fmt(battery_label,
                    LV_SYMBOL_BATTERY_FULL " %d%%", battery_percentage);

                lv_color_t bat_col;
                if      (battery_percentage > 60) bat_col = lv_color_make(34, 197, 94);
                else if (battery_percentage > 25) bat_col = lv_color_make(234, 179, 8);
                else                              bat_col = lv_color_make(239, 68, 68);
                lv_obj_set_style_bg_color(battery_bar, bat_col, LV_PART_INDICATOR);
            }
            lv_bar_set_value(battery_bar, battery_percentage, LV_ANIM_ON);

            // Sync system-info tab arc
            if (sys_battery_arc) {
                lv_arc_set_value(sys_battery_arc, battery_percentage);
            }
            break;
        }

        case UI_EVT_NETWORK_STATUS: {
            wifi_connected = (msg.val1 == 1);
            ble_active     = (msg.val2 == 1);
            lv_obj_set_style_text_color(wifi_icon,
                wifi_connected ? lv_color_make(34,197,94) : lv_color_make(100,116,139), 0);
            lv_obj_set_style_text_color(ble_icon,
                ble_active     ? lv_color_make(56,189,248) : lv_color_make(100,116,139), 0);
            break;
        }

        case UI_EVT_SHOW_TOAST:
            show_toast_internal(msg.str);
            break;

        case UI_EVT_TIME_UPDATE:
            if (time_label) {
                lv_label_set_text_fmt(time_label, "%02d:%02d", msg.val1, msg.val2);
            }
            break;

        case UI_EVT_AI_RESULT:
        case UI_EVT_OCR_RESULT: {
            bool success = (msg.val1 == 1);
            String text = msg.heap_str ? *msg.heap_str : String("(empty)");
            reader_ui_handle_ai_result(success, text);
            if (msg.heap_str) delete msg.heap_str;
            break;
        }

        case UI_EVT_LIBRARY_REFRESH:
            reader_ui_handle_library_refresh();
            break;
        }
    }
}

// ─────────────────────────────────────────────
//  Touch input driver
// ─────────────────────────────────────────────
static void my_touchpad_read(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    if (is_screen_locked) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    if (ts.touched()) {
        TS_Point p = ts.getPoint();
        data->point.x = (int16_t)map(p.x, 200, 3800, 0, 320);
        data->point.y = (int16_t)map(p.y, 200, 3800, 0, 480);
        data->state   = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

// ─────────────────────────────────────────────
//  Canvas helpers
// ─────────────────────────────────────────────
static void push_undo_snapshot() {
    size_t sz = LV_CANVAS_BUF_SIZE_TRUE_COLOR(CANVAS_W, CANVAS_H);
    memcpy(undo_stack[undo_head], canvas_buffer, sz);
    undo_head = (undo_head + 1) % UNDO_DEPTH;
    if (undo_count < UNDO_DEPTH) undo_count++;
}

static void draw_pixel_brush(lv_obj_t* cnv, int cx, int cy) {
    // Filled circle brush — radius = active_brush_size
    int r = (int)active_brush_size;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx*dx + dy*dy <= r*r) {
                int px = cx + dx;
                int py = cy + dy;
                if (px >= 0 && px < CANVAS_W && py >= 0 && py < CANVAS_H) {
                    lv_canvas_set_px_color(cnv, px, py, active_draw_color);
                }
            }
        }
    }
}

// Draw a filled line between two points (Bresenham + brush)
static void draw_line_brush(lv_obj_t* cnv, lv_point_t p0, lv_point_t p1) {
    int dx = abs(p1.x - p0.x), sx = p0.x < p1.x ? 1 : -1;
    int dy = abs(p1.y - p0.y), sy = p0.y < p1.y ? 1 : -1;
    int err = (dx > dy ? dx : -dy) / 2;
    while (true) {
        draw_pixel_brush(cnv, p0.x, p0.y);
        if (p0.x == p1.x && p0.y == p1.y) break;
        int e2 = err;
        if (e2 > -dx) { err -= dy; p0.x += sx; }
        if (e2 <  dy) { err += dx; p0.y += sy; }
    }
}

static void canvas_event_cb(lv_event_t* e) {
    if (is_screen_locked) return;

    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t*       obj  = lv_event_get_target(e);
    lv_indev_t*     indev = lv_indev_get_act();
    if (!indev) return;

    lv_point_t act_point;
    lv_indev_get_point(indev, &act_point);

    lv_point_t cp;
    cp.x = act_point.x - obj->coords.x1;
    cp.y = act_point.y - obj->coords.y1;
    cp.x = constrain(cp.x, 0, CANVAS_W - 1);
    cp.y = constrain(cp.y, 0, CANVAS_H - 1);

    if (code == LV_EVENT_PRESSED) {
        push_undo_snapshot();        // snapshot before stroke begins
        last_draw_pt = cp;
        draw_pixel_brush(obj, cp.x, cp.y);
    } else if (code == LV_EVENT_PRESSING) {
        if (last_draw_pt.x >= 0) {
            draw_line_brush(obj, last_draw_pt, cp);
        }
        last_draw_pt = cp;
    } else if (code == LV_EVENT_RELEASED) {
        last_draw_pt = {-1, -1};
    }
}

void canvas_clear() {
    if (!canvas) return;
    push_undo_snapshot();
    lv_canvas_fill_bg(canvas, lv_color_make(10, 10, 15), LV_OPA_COVER);
}

bool canvas_save_bmp() {
    if (!sd_card_ok || !canvas_buffer) return false;

    if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;

    File f = SD.open("/canvas.bmp", FILE_WRITE);
    if (!f) { xSemaphoreGive(sd_mutex); return false; }

    // BMP header for 24-bit uncompressed
    uint32_t row_bytes  = (CANVAS_W * 3 + 3) & ~3;  // 4-byte aligned
    uint32_t pixel_data = 54;
    uint32_t file_size  = pixel_data + row_bytes * CANVAS_H;

    // File header
    uint8_t fh[14] = {};
    fh[0] = 'B'; fh[1] = 'M';
    fh[2] = file_size & 0xFF; fh[3] = (file_size >> 8) & 0xFF;
    fh[4] = (file_size >> 16) & 0xFF; fh[5] = (file_size >> 24) & 0xFF;
    fh[10] = pixel_data;
    f.write(fh, 14);

    // DIB header (BITMAPINFOHEADER)
    uint8_t dib[40] = {};
    dib[0] = 40;
    dib[4] = CANVAS_W & 0xFF; dib[5] = (CANVAS_W >> 8) & 0xFF;
    // Negative height → top-down rows
    int32_t neg_h = -CANVAS_H;
    memcpy(&dib[8], &neg_h, 4);
    dib[12] = 1; dib[14] = 24;
    dib[20] = row_bytes * CANVAS_H;
    f.write(dib, 40);

    // Pixel rows
    uint8_t row_buf[row_bytes];
    for (int y = 0; y < CANVAS_H; y++) {
        for (int x = 0; x < CANVAS_W; x++) {
            lv_color_t c = canvas_buffer[y * CANVAS_W + x];
            // RGB565 → RGB888
            row_buf[x*3 + 0] = (c.ch.blue  << 3) | (c.ch.blue  >> 2);  // B
            row_buf[x*3 + 1] = (c.ch.green << 2) | (c.ch.green >> 4);  // G
            row_buf[x*3 + 2] = (c.ch.red   << 3) | (c.ch.red   >> 2);  // R
        }
        // Pad to 4-byte boundary
        for (uint32_t p = CANVAS_W * 3; p < row_bytes; p++) row_buf[p] = 0;
        f.write(row_buf, row_bytes);
    }
    f.close();
    xSemaphoreGive(sd_mutex);
    return true;
}

// ─────────────────────────────────────────────
//  Button callbacks
// ─────────────────────────────────────────────
static void color_btn_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    active_draw_color = PALETTE[idx];
    // Highlight selected button
    for (int i = 0; i < 6; i++) {
        lv_obj_set_style_border_width(color_btns[i], (i == idx) ? 3 : 1, 0);
        lv_obj_set_style_border_color(color_btns[i],
            (i == idx) ? lv_color_make(56,189,248) : lv_color_make(71,85,105), 0);
    }
    buzz(1600, 20);
}

static void brush_slider_cb(lv_event_t* e) {
    lv_obj_t* slider = lv_event_get_target(e);
    active_brush_size = (uint8_t)lv_slider_get_value(slider);
}

static void clear_btn_cb(lv_event_t* e) {
    canvas_clear();
    post_toast("Canvas cleared");
}

static void undo_btn_cb(lv_event_t* e) {
    if (undo_count == 0 || !canvas || !canvas_buffer) {
        post_toast("Nothing to undo");
        return;
    }
    // Pop the last snapshot
    undo_head = (undo_head - 1 + UNDO_DEPTH) % UNDO_DEPTH;
    undo_count--;
    size_t sz = LV_CANVAS_BUF_SIZE_TRUE_COLOR(CANVAS_W, CANVAS_H);
    memcpy(canvas_buffer, undo_stack[undo_head], sz);
    lv_obj_invalidate(canvas);
    post_toast("Undo");
}

static void save_btn_cb(lv_event_t* e) {
    if (canvas_save_bmp()) {
        post_toast("Saved: /canvas.bmp");
    } else {
        post_toast("Save failed — check SD");
    }
}

static void note_save_btn_cb(lv_event_t* e) {
    if (!notes_textarea) return;
    const char* txt = lv_textarea_get_text(notes_textarea);
    if (!txt || strlen(txt) == 0) {
        post_toast("Note is empty");
        return;
    }
    if (!sd_card_ok) {
        post_toast("SD card not ready");
        return;
    }
    if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        File f = SD.open("/notes.txt", FILE_APPEND);
        if (f) {
            f.printf("[TFT Note]: %s\n", txt);
            f.close();
            lv_textarea_set_text(notes_textarea, "");
            post_toast("Note saved to SD");
        } else {
            post_toast("SD write error");
        }
        xSemaphoreGive(sd_mutex);
    }
}

// ─────────────────────────────────────────────
//  Header bar
// ─────────────────────────────────────────────
static void build_header() {
    header_bar = lv_obj_create(scr_main);
    lv_obj_set_size(header_bar, 320, 36);
    lv_obj_align(header_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header_bar, lv_color_make(10, 15, 30), 0);
    lv_obj_set_style_border_width(header_bar, 0, 0);
    lv_obj_set_style_pad_all(header_bar, 4, 0);
    lv_obj_set_style_radius(header_bar, 0, 0);
    lv_obj_clear_flag(header_bar, LV_OBJ_FLAG_SCROLLABLE);

    // Battery bar (left side)
    battery_bar = lv_bar_create(header_bar);
    lv_obj_set_size(battery_bar, 48, 10);
    lv_obj_align(battery_bar, LV_ALIGN_LEFT_MID, 2, 0);
    lv_bar_set_range(battery_bar, 0, 100);
    lv_obj_set_style_bg_color(battery_bar, lv_color_make(30, 41, 59), LV_PART_MAIN);
    lv_obj_set_style_bg_color(battery_bar, lv_color_make(56, 189, 248), LV_PART_INDICATOR);
    lv_obj_set_style_radius(battery_bar, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(battery_bar, 3, LV_PART_INDICATOR);

    // Battery label
    battery_label = lv_label_create(header_bar);
    lv_label_set_text(battery_label, "-- %");
    lv_obj_align_to(battery_label, battery_bar, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
    lv_obj_set_style_text_font(battery_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(battery_label, lv_color_make(148, 163, 184), 0);

    // Time label (centre)
    time_label = lv_label_create(header_bar);
    lv_label_set_text(time_label, "--:--");
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(time_label, lv_color_make(226, 232, 240), 0);

    // Network icons (right side)
    ble_icon = lv_label_create(header_bar);
    lv_label_set_text(ble_icon, "BLE");
    lv_obj_align(ble_icon, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_set_style_text_font(ble_icon, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(ble_icon, lv_color_make(100, 116, 139), 0);

    wifi_icon = lv_label_create(header_bar);
    lv_label_set_text(wifi_icon, "WiFi");
    lv_obj_align_to(wifi_icon, ble_icon, LV_ALIGN_OUT_LEFT_MID, -6, 0);
    lv_obj_set_style_text_font(wifi_icon, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(wifi_icon, lv_color_make(100, 116, 139), 0);

    sd_icon = lv_label_create(header_bar);
    lv_label_set_text(sd_icon, "SD");
    lv_obj_align_to(sd_icon, wifi_icon, LV_ALIGN_OUT_LEFT_MID, -6, 0);
    lv_obj_set_style_text_font(sd_icon, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(sd_icon,
        sd_card_ok ? lv_color_make(34, 197, 94) : lv_color_make(239, 68, 68), 0);
}

// ─────────────────────────────────────────────
//  Tab: Draw
// ─────────────────────────────────────────────
static void build_tab_draw() {
    lv_obj_set_style_pad_all(tab_draw, 4, 0);
    lv_obj_set_style_bg_color(tab_draw, lv_color_make(8, 12, 22), 0);
    lv_obj_clear_flag(tab_draw, LV_OBJ_FLAG_SCROLLABLE);

    // ── Canvas ─────────────────────────────────
    size_t canvas_sz = LV_CANVAS_BUF_SIZE_TRUE_COLOR(CANVAS_W, CANVAS_H);
    canvas_buffer    = (lv_color_t*)ps_malloc(canvas_sz);

    // Allocate undo snapshots in PSRAM
    for (int i = 0; i < UNDO_DEPTH; i++) {
        undo_stack[i] = (lv_color_t*)ps_malloc(canvas_sz);
    }

    canvas = lv_canvas_create(tab_draw);
    lv_canvas_set_buffer(canvas, canvas_buffer, CANVAS_W, CANVAS_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_size(canvas, CANVAS_W, CANVAS_H);
    lv_obj_align(canvas, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_canvas_fill_bg(canvas, lv_color_make(10, 10, 15), LV_OPA_COVER);
    lv_obj_set_style_border_color(canvas, lv_color_make(30, 41, 59), 0);
    lv_obj_set_style_border_width(canvas, 1, 0);
    lv_obj_add_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(canvas, canvas_event_cb, LV_EVENT_ALL, nullptr);

    // ── Tool panel (right strip) ───────────────
    lv_obj_t* tool_panel = lv_obj_create(tab_draw);
    lv_obj_set_size(tool_panel, 36, CANVAS_H);
    lv_obj_align(tool_panel, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(tool_panel, lv_color_make(15, 23, 42), 0);
    lv_obj_set_style_border_color(tool_panel, lv_color_make(30, 41, 59), 0);
    lv_obj_set_style_pad_all(tool_panel, 3, 0);
    lv_obj_set_style_pad_row(tool_panel, 4, 0);
    lv_obj_set_flex_flow(tool_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tool_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(tool_panel, LV_OBJ_FLAG_SCROLLABLE);

    // Colour swatches
    for (int i = 0; i < 6; i++) {
        color_btns[i] = lv_btn_create(tool_panel);
        lv_obj_set_size(color_btns[i], 28, 28);
        lv_obj_set_style_bg_color(color_btns[i], PALETTE[i], 0);
        lv_obj_set_style_border_color(color_btns[i], lv_color_make(71, 85, 105), 0);
        lv_obj_set_style_border_width(color_btns[i], 1, 0);
        lv_obj_set_style_pad_all(color_btns[i], 0, 0);
        lv_obj_set_style_radius(color_btns[i], 4, 0);

        lv_obj_t* lbl = lv_label_create(color_btns[i]);
        lv_label_set_text(lbl, PALETTE_NAMES[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_8, 0);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(color_btns[i], color_btn_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
    // Highlight first colour by default
    active_draw_color = PALETTE[0];
    lv_obj_set_style_border_width(color_btns[0], 3, 0);
    lv_obj_set_style_border_color(color_btns[0], lv_color_make(56, 189, 248), 0);

    // Brush-size slider (vertical)
    lv_obj_t* brush_slider = lv_slider_create(tool_panel);
    lv_obj_set_size(brush_slider, 12, 60);
    lv_slider_set_range(brush_slider, 1, 8);
    lv_slider_set_value(brush_slider, active_brush_size, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(brush_slider, lv_color_make(30, 41, 59), LV_PART_MAIN);
    lv_obj_set_style_bg_color(brush_slider, lv_color_make(56, 189, 248), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(brush_slider, lv_color_make(148, 163, 184), LV_PART_KNOB);
    lv_obj_add_event_cb(brush_slider, brush_slider_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    // Action buttons
    struct { const char* lbl; lv_event_cb_t cb; } btns[] = {
        { LV_SYMBOL_TRASH,  clear_btn_cb },
        { LV_SYMBOL_PREV,   undo_btn_cb  },
        { LV_SYMBOL_SAVE,   save_btn_cb  },
    };
    for (auto& b : btns) {
        lv_obj_t* btn = lv_btn_create(tool_panel);
        lv_obj_set_size(btn, 28, 28);
        lv_obj_set_style_bg_color(btn, lv_color_make(30, 41, 59), 0);
        lv_obj_set_style_border_color(btn, lv_color_make(56, 189, 248), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, b.lbl);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, b.cb, LV_EVENT_CLICKED, nullptr);
    }
}

// ─────────────────────────────────────────────
//  Tab: Notes
// ─────────────────────────────────────────────
static void build_tab_notes() {
    lv_obj_set_style_pad_all(tab_notes, 6, 0);
    lv_obj_set_style_bg_color(tab_notes, lv_color_make(8, 12, 22), 0);
    lv_obj_clear_flag(tab_notes, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(tab_notes);
    lv_label_set_text(title, LV_SYMBOL_LIST "  Quick Notes");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_make(56, 189, 248), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* hint = lv_label_create(tab_notes);
    lv_label_set_text(hint, "Type a note, then tap Save to write to SD.");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_make(100, 116, 139), 0);
    lv_obj_align_to(hint, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

    notes_textarea = lv_textarea_create(tab_notes);
    lv_obj_set_size(notes_textarea, 308, 290);
    lv_obj_align_to(notes_textarea, hint, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    lv_textarea_set_placeholder_text(notes_textarea, "Write your note here...");
    lv_obj_set_style_bg_color(notes_textarea, lv_color_make(15, 23, 42), 0);
    lv_obj_set_style_text_color(notes_textarea, lv_color_make(226, 232, 240), 0);
    lv_obj_set_style_border_color(notes_textarea, lv_color_make(30, 41, 59), 0);
    lv_obj_set_style_text_font(notes_textarea, &lv_font_montserrat_12, 0);
    keyboard_attach(notes_textarea);

    lv_obj_t* save_btn = lv_btn_create(tab_notes);
    lv_obj_set_size(save_btn, 308, 38);
    lv_obj_align_to(save_btn, notes_textarea, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    lv_obj_set_style_bg_color(save_btn, lv_color_make(37, 99, 235), 0);
    lv_obj_set_style_radius(save_btn, 6, 0);
    lv_obj_t* save_lbl = lv_label_create(save_btn);
    lv_label_set_text(save_lbl, LV_SYMBOL_SAVE "  Save to SD Card");
    lv_obj_set_style_text_font(save_lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(save_lbl);
    lv_obj_add_event_cb(save_btn, note_save_btn_cb, LV_EVENT_CLICKED, nullptr);
}

// ─────────────────────────────────────────────
//  Tab: System Info
// ─────────────────────────────────────────────
static void build_tab_sys() {
    lv_obj_set_style_pad_all(tab_sys, 8, 0);
    lv_obj_set_style_bg_color(tab_sys, lv_color_make(8, 12, 22), 0);
    lv_obj_set_style_pad_row(tab_sys, 0, 0);

    lv_obj_t* title = lv_label_create(tab_sys);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS "  System");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_make(56, 189, 248), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    // Battery arc gauge
    sys_battery_arc = lv_arc_create(tab_sys);
    lv_obj_set_size(sys_battery_arc, 100, 100);
    lv_arc_set_rotation(sys_battery_arc, 135);
    lv_arc_set_bg_angles(sys_battery_arc, 0, 270);
    lv_arc_set_range(sys_battery_arc, 0, 100);
    lv_arc_set_value(sys_battery_arc, battery_percentage);
    lv_obj_set_style_arc_color(sys_battery_arc, lv_color_make(34, 197, 94), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(sys_battery_arc, lv_color_make(30, 41, 59), LV_PART_MAIN);
    lv_obj_set_style_arc_width(sys_battery_arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(sys_battery_arc, 10, LV_PART_MAIN);
    lv_arc_set_mode(sys_battery_arc, LV_ARC_MODE_NORMAL);
    lv_obj_remove_style(sys_battery_arc, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(sys_battery_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(sys_battery_arc, LV_ALIGN_TOP_RIGHT, 0, 20);

    lv_obj_t* arc_lbl = lv_label_create(tab_sys);
    lv_label_set_text(arc_lbl, "Battery");
    lv_obj_set_style_text_font(arc_lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(arc_lbl, lv_color_make(100, 116, 139), 0);
    lv_obj_align_to(arc_lbl, sys_battery_arc, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);

    // Info block
    sys_info_label = lv_label_create(tab_sys);
    lv_obj_set_size(sys_info_label, 180, LV_SIZE_CONTENT);
    lv_obj_align_to(sys_info_label, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 12);
    lv_obj_set_style_text_font(sys_info_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(sys_info_label, lv_color_make(148, 163, 184), 0);

    char info_buf[280];
    snprintf(info_buf, sizeof(info_buf),
        "CPU:   ESP32-S3 @ 240 MHz\n"
        "Flash: 16 MB (QIO)\n"
        "PSRAM: 8 MB (OPI)\n"
        "Free Heap: %lu KB\n"
        "Free PSRAM: %lu KB\n"
        "SD Card: %s\n"
        "Display: 320x480 ILI\n"
        "Charge Limit: %d%%\n"
        "Brightness: %d%%\n"
        "Overchg Prot: %s",
        ESP.getFreeHeap()      / 1024,
        ESP.getFreePsram()     / 1024,
        sd_card_ok ? "OK" : "Not found",
        charge_limit_threshold,
        global_brightness,
        overcharge_protection_enabled ? "ON" : "OFF"
    );
    lv_label_set_text(sys_info_label, info_buf);

    // Brightness slider
    lv_obj_t* br_lbl = lv_label_create(tab_sys);
    lv_label_set_text(br_lbl, "Brightness");
    lv_obj_set_style_text_font(br_lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(br_lbl, lv_color_make(100, 116, 139), 0);
    lv_obj_align(br_lbl, LV_ALIGN_TOP_LEFT, 0, 168);

    lv_obj_t* br_slider = lv_slider_create(tab_sys);
    lv_obj_set_size(br_slider, 200, 14);
    lv_obj_align_to(br_slider, br_lbl, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
    lv_slider_set_range(br_slider, 10, 100);
    lv_slider_set_value(br_slider, global_brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(br_slider, lv_color_make(30, 41, 59), LV_PART_MAIN);
    lv_obj_set_style_bg_color(br_slider, lv_color_make(234, 179, 8), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(br_slider, lv_color_make(255, 255, 255), LV_PART_KNOB);
    lv_obj_add_event_cb(br_slider, [](lv_event_t* e) {
        lv_obj_t* s = lv_event_get_target(e);
        set_display_brightness((int)lv_slider_get_value(s));
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    // Charge-limit slider
    lv_obj_t* cl_lbl = lv_label_create(tab_sys);
    lv_label_set_text_fmt(cl_lbl, "Charge Limit: %d%%", charge_limit_threshold);
    lv_obj_set_style_text_font(cl_lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(cl_lbl, lv_color_make(100, 116, 139), 0);
    lv_obj_align_to(cl_lbl, br_slider, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 12);

    lv_obj_t* cl_slider = lv_slider_create(tab_sys);
    lv_obj_set_size(cl_slider, 200, 14);
    lv_obj_align_to(cl_slider, cl_lbl, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
    lv_slider_set_range(cl_slider, 50, 100);
    lv_slider_set_value(cl_slider, charge_limit_threshold, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(cl_slider, lv_color_make(30, 41, 59), LV_PART_MAIN);
    lv_obj_set_style_bg_color(cl_slider, lv_color_make(34, 197, 94), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(cl_slider, lv_color_make(255, 255, 255), LV_PART_KNOB);
    lv_obj_add_event_cb(cl_slider, [](lv_event_t* e) {
        lv_obj_t* s = lv_event_get_target(e);
        charge_limit_threshold = (int)lv_slider_get_value(s);
        // Save to NVS
        prefs.begin("workbench", false);
        prefs.putInt("charge_limit", charge_limit_threshold);
        prefs.end();
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    // Overcharge toggle
    lv_obj_t* oc_row = lv_obj_create(tab_sys);
    lv_obj_set_size(oc_row, 310, 34);
    lv_obj_align_to(oc_row, cl_slider, LV_ALIGN_OUT_BOTTOM_LEFT, -4, 10);
    lv_obj_set_style_bg_color(oc_row, lv_color_make(15, 23, 42), 0);
    lv_obj_set_style_border_color(oc_row, lv_color_make(30, 41, 59), 0);
    lv_obj_set_style_radius(oc_row, 6, 0);
    lv_obj_set_style_pad_all(oc_row, 6, 0);
    lv_obj_clear_flag(oc_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* oc_lbl = lv_label_create(oc_row);
    lv_label_set_text(oc_lbl, "Overcharge Protection");
    lv_obj_set_style_text_font(oc_lbl, &lv_font_montserrat_10, 0);
    lv_obj_align(oc_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* oc_sw = lv_switch_create(oc_row);
    lv_obj_align(oc_sw, LV_ALIGN_RIGHT_MID, 0, 0);
    if (overcharge_protection_enabled) lv_obj_add_state(oc_sw, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(oc_sw, lv_color_make(34, 197, 94), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(oc_sw, [](lv_event_t* e) {
        lv_obj_t* sw = lv_event_get_target(e);
        overcharge_protection_enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
        prefs.begin("workbench", false);
        prefs.putBool("oc_prot", overcharge_protection_enabled);
        prefs.end();
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    // Reboot button
    lv_obj_t* reboot_btn = lv_btn_create(tab_sys);
    lv_obj_set_size(reboot_btn, 310, 34);
    lv_obj_align_to(reboot_btn, oc_row, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    lv_obj_set_style_bg_color(reboot_btn, lv_color_make(127, 29, 29), 0);
    lv_obj_set_style_radius(reboot_btn, 6, 0);
    lv_obj_t* rb_lbl = lv_label_create(reboot_btn);
    lv_label_set_text(rb_lbl, LV_SYMBOL_REFRESH "  Reboot System");
    lv_obj_set_style_text_font(rb_lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(rb_lbl);
    lv_obj_add_event_cb(reboot_btn, [](lv_event_t* e) {
        post_toast("Rebooting...");
        lv_timer_create([](lv_timer_t*) { ESP.restart(); }, 1000, nullptr);
    }, LV_EVENT_CLICKED, nullptr);
}

// ─────────────────────────────────────────────
//  Lock overlay
// ─────────────────────────────────────────────
static void build_lock_overlay() {
    lock_overlay = lv_obj_create(scr_main);
    lv_obj_set_size(lock_overlay, 320, 480);
    lv_obj_set_pos(lock_overlay, 0, 0);
    lv_obj_set_style_bg_color(lock_overlay, lv_color_make(5, 8, 18), 0);
    lv_obj_set_style_bg_opa(lock_overlay, LV_OPA_90, 0);  // LVGL only defines OPA_10..OPA_90 in steps of 10, no OPA_95
    lv_obj_set_style_border_width(lock_overlay, 0, 0);
    lv_obj_set_style_radius(lock_overlay, 0, 0);
    lv_obj_set_style_opa(lock_overlay, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(lock_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(lock_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(lock_overlay);

    lv_obj_t* icon = lv_label_create(lock_overlay);
    lv_label_set_text(icon, LV_SYMBOL_EYE_CLOSE);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(icon, lv_color_make(56, 189, 248), 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t* msg = lv_label_create(lock_overlay);
    lv_label_set_text(msg, "Screen Locked\nShort press button to wake");
    lv_obj_set_style_text_font(msg, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(msg, lv_color_make(100, 116, 139), 0);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(msg, LV_ALIGN_CENTER, 0, 40);
}

// ─────────────────────────────────────────────
//  Toast overlay
// ─────────────────────────────────────────────
static void build_toast() {
    toast_label = lv_label_create(scr_main);
    lv_obj_set_style_bg_color(toast_label, lv_color_make(30, 41, 59), 0);
    lv_obj_set_style_bg_opa(toast_label, LV_OPA_90, 0);
    lv_obj_set_style_text_color(toast_label, lv_color_make(226, 232, 240), 0);
    lv_obj_set_style_text_font(toast_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_pad_hor(toast_label, 12, 0);
    lv_obj_set_style_pad_ver(toast_label, 6, 0);
    lv_obj_set_style_radius(toast_label, 20, 0);
    lv_obj_set_style_border_color(toast_label, lv_color_make(56, 189, 248), 0);
    lv_obj_set_style_border_width(toast_label, 1, 0);
    lv_obj_align(toast_label, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_opa(toast_label, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(toast_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(toast_label);
}

// ─────────────────────────────────────────────
//  Shared on-screen keyboard
//  One instance, reused by every textarea in the project. Hidden by
//  default; shown and pointed at whichever textarea gets focused.
//
//  Note: LVGL's keyboard forwards LV_EVENT_READY (Ok/checkmark) and
//  LV_EVENT_CANCEL (Close) to its currently-bound TEXTAREA, not to the
//  keyboard object itself — so all four events (FOCUSED, DEFOCUSED,
//  READY, CANCEL) are handled in one callback attached per-textarea,
//  matching LVGL's own documented keyboard example, rather than
//  attaching a separate handler to the keyboard widget.
// ─────────────────────────────────────────────
static void build_shared_keyboard() {
    shared_keyboard = lv_keyboard_create(lv_layer_top());
    lv_obj_set_size(shared_keyboard, 320, 170);
    lv_obj_align(shared_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(shared_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(shared_keyboard);
}

static void keyboard_textarea_event_cb(lv_event_t* e) {
    if (!shared_keyboard) return;
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t*       ta   = lv_event_get_target(e);

    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(shared_keyboard, ta);
        lv_obj_clear_flag(shared_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(shared_keyboard);

    } else if (code == LV_EVENT_DEFOCUSED) {
        // Only hide if this textarea was the one the keyboard was
        // pointed at — otherwise switching focus directly between two
        // textareas would flash the keyboard closed and reopen it.
        if (lv_keyboard_get_textarea(shared_keyboard) == ta) {
            lv_keyboard_set_textarea(shared_keyboard, nullptr);
            lv_obj_add_flag(shared_keyboard, LV_OBJ_FLAG_HIDDEN);
        }

    } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        // Ok/Close pressed on the keyboard. Drop the textarea's focus
        // state for visual correctness (removes the focus highlight),
        // and hide the keyboard directly here rather than depending on
        // clear_state() to cascade into a DEFOCUSED event, since LVGL
        // doesn't guarantee that side effect for a programmatic state
        // change.
        lv_obj_clear_state(ta, LV_STATE_FOCUSED);
        lv_keyboard_set_textarea(shared_keyboard, nullptr);
        lv_obj_add_flag(shared_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

void keyboard_attach(lv_obj_t* textarea) {
    lv_obj_add_event_cb(textarea, keyboard_textarea_event_cb, LV_EVENT_ALL, nullptr);
}

// ─────────────────────────────────────────────
//  Main init
// ─────────────────────────────────────────────
void init_ui_engine() {
    // Mutexes and queues
    lvgl_mutex      = xSemaphoreCreateMutex();
    sd_mutex        = xSemaphoreCreateMutex();
    ui_event_queue  = xQueueCreate(20, sizeof(UIEventMsg));

    // Load persisted settings
    prefs.begin("workbench", true);
    global_brightness          = prefs.getInt("brightness",   80);
    charge_limit_threshold     = prefs.getInt("charge_limit", 80);
    overcharge_protection_enabled = prefs.getBool("oc_prot",  true);
    prefs.end();

    // ADC config for battery
    analogSetAttenuation(ADC_11db);   // 0–3.3 V range

    // Touch
    ts.begin();
    ts.setRotation(1);

    // Backlight PWM
    ledcSetup(0, 5000, 8);
    ledcAttachPin(TFT_BL_PIN, 0);
    set_display_brightness(global_brightness);

    // Input driver
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    // Main screen
    scr_main = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr_main, lv_color_make(5, 8, 18), 0);
    lv_obj_set_style_pad_all(scr_main, 0, 0);
    lv_scr_load(scr_main);

    build_header();

    // Tabview — sits below header, above bottom tab bar
    // 480 - 36 (header) - 40 (tab bar) = 404 usable height
    tabview = lv_tabview_create(scr_main, LV_DIR_BOTTOM, 38);
    lv_obj_set_size(tabview, 320, 480 - 36);
    lv_obj_align(tabview, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_set_style_bg_color(tabview, lv_color_make(8, 12, 22), 0);
    lv_obj_set_style_bg_color(
        lv_tabview_get_tab_btns(tabview), lv_color_make(10, 15, 30), 0);
    lv_obj_set_style_border_side(
        lv_tabview_get_tab_btns(tabview), LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(
        lv_tabview_get_tab_btns(tabview), lv_color_make(30, 41, 59), 0);
    lv_obj_set_style_text_color(
        lv_tabview_get_tab_btns(tabview), lv_color_make(148, 163, 184), 0);
    lv_obj_set_style_text_color(
        lv_tabview_get_tab_btns(tabview), lv_color_make(56, 189, 248),
        LV_PART_ITEMS | LV_STATE_CHECKED);

    tab_draw  = lv_tabview_add_tab(tabview, LV_SYMBOL_EDIT "  Draw");
    tab_notes = lv_tabview_add_tab(tabview, LV_SYMBOL_LIST "  Notes");
    tab_sys   = lv_tabview_add_tab(tabview, LV_SYMBOL_SETTINGS "  Sys");

    build_tab_draw();
    build_tab_notes();
    build_tab_sys();

    // Library / Reader / AI Study / Camera / Settings tabs (reader_ui.cpp)
    reader_ui_init(tabview);

    build_lock_overlay();
    build_toast();
    build_shared_keyboard();

    // Boot-up fade-in
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
    lv_anim_set_var(&a, scr_main);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, 600);
    lv_anim_start(&a);

    // Periodic ADC battery read timer (every 5 seconds)
    lv_timer_create([](lv_timer_t*) {
        read_battery_adc();
    }, 5000, nullptr);

    Serial.println("[UI] Engine initialized.");
    post_toast("Workbench Online");
}