// ═══════════════════════════════════════════════════════════════════
//  reader_ui.cpp  —  Library, Reader, AI Study, Camera, and Settings
//  tabs for the Workbench device. Built as a companion module to
//  ui_engine.cpp so the original draw/notes/system scope stays clean.
// ═══════════════════════════════════════════════════════════════════
#include "reader_ui.h"
#include "book_engine.h"
#include "ai_engine.h"
#include "camera_engine.h"
#include "wifi_manager.h"
#include "audio_engine.h"
#include <Preferences.h>
#include <SD.h>
#include <vector>

// ─────────────────────────────────────────────
//  Reader theme/font globals (declared extern in ui_engine.h)
// ─────────────────────────────────────────────
ReaderTheme    active_reader_theme = THEME_DARK;
ReaderFontSize active_reader_font  = FONT_MEDIUM;

// ─────────────────────────────────────────────
//  Tab handles owned by this module
// ─────────────────────────────────────────────
static lv_obj_t* tabview_ref      = nullptr;
static lv_obj_t* tab_library      = nullptr;
static lv_obj_t* tab_reader       = nullptr;
static lv_obj_t* tab_ai_study     = nullptr;
static lv_obj_t* tab_camera       = nullptr;
static lv_obj_t* tab_voice        = nullptr;
static lv_obj_t* tab_settings     = nullptr;

static lv_obj_t* library_list     = nullptr;
static lv_obj_t* library_empty_lbl= nullptr;

static lv_obj_t* reader_title_lbl = nullptr;
static lv_obj_t* reader_text_lbl  = nullptr;
static lv_obj_t* reader_page_lbl  = nullptr;
static lv_obj_t* reader_progress_bar = nullptr;
static lv_obj_t* reader_container = nullptr;

static lv_obj_t* ai_modal         = nullptr;
static lv_obj_t* ai_modal_text    = nullptr;
static lv_obj_t* ai_modal_spinner = nullptr;
static lv_obj_t* ai_chat_input    = nullptr;
static lv_obj_t* ai_chat_history_lbl = nullptr;

static lv_obj_t* camera_preview_lbl = nullptr;
static lv_obj_t* camera_result_ta   = nullptr;

static lv_obj_t* voice_status_lbl   = nullptr;
static lv_obj_t* voice_record_btn   = nullptr;
static lv_obj_t* voice_record_btn_lbl = nullptr;
static lv_obj_t* voice_list         = nullptr;
static lv_timer_t* voice_record_timer = nullptr;
static String     voice_currently_playing;

static lv_obj_t* settings_wifi_list = nullptr;
static lv_obj_t* settings_ai_key_ta = nullptr;

static String    current_book_path;
static String    ai_chat_history_text;

// Tab indices within tabview_ref, recorded once tabs are created in
// reader_ui_init() so other handlers can jump to a tab by index.
uint16_t g_reader_tab_index  = 0;
uint16_t g_library_tab_index = 0;

// Reading-time tracking
static uint32_t reading_session_start_ms = 0;

// ─────────────────────────────────────────────
//  Forward declarations
// ─────────────────────────────────────────────
static void build_tab_library();
static void build_tab_reader();
static void build_tab_ai_study();
static void build_tab_camera();
static void build_tab_voice();
static void build_tab_settings();
static void build_ai_modal();
static void render_current_page_ui();
static void voice_rebuild_list();
static void show_ai_modal(const String& title);
static void hide_ai_modal();

// ─────────────────────────────────────────────
//  Theme color tables
// ─────────────────────────────────────────────
struct ThemeColors { lv_color_t bg, text, accent, dim; };

static ThemeColors get_theme_colors(ReaderTheme t) {
    switch (t) {
        case THEME_LIGHT:
            return { lv_color_make(250,250,248), lv_color_make(20,20,20),
                      lv_color_make(37,99,235),   lv_color_make(140,140,140) };
        case THEME_SEPIA:
            return { lv_color_make(241,228,200), lv_color_make(59,43,29),
                      lv_color_make(146,98,55),   lv_color_make(150,120,90) };
        case THEME_OLED_BLACK:
            return { lv_color_make(0,0,0),        lv_color_make(200,200,200),
                      lv_color_make(56,189,248),  lv_color_make(90,90,90) };
        case THEME_DARK:
        default:
            return { lv_color_make(10,14,24),    lv_color_make(226,232,240),
                      lv_color_make(56,189,248),  lv_color_make(100,116,139) };
    }
}

static const lv_font_t* get_reader_font(ReaderFontSize s) {
    switch (s) {
        case FONT_SMALL:    return &lv_font_montserrat_12;
        case FONT_LARGE:    return &lv_font_montserrat_18;
        case FONT_DYSLEXIC: return &lv_font_montserrat_16;  // monospaced-ish fallback
        case FONT_MEDIUM:
        default:            return &lv_font_montserrat_14;
    }
}

static int get_font_px_w(ReaderFontSize s) {
    // Rough average glyph width for word-wrap math (Montserrat is proportional,
    // so this is an estimate tuned per size for an 8px average character).
    switch (s) {
        case FONT_SMALL:    return 7;
        case FONT_LARGE:    return 10;
        case FONT_DYSLEXIC: return 9;
        case FONT_MEDIUM:
        default:            return 8;
    }
}

static int get_font_px_h(ReaderFontSize s) {
    switch (s) {
        case FONT_SMALL:    return 16;
        case FONT_LARGE:    return 24;
        case FONT_DYSLEXIC: return 22;
        case FONT_MEDIUM:
        default:            return 19;
    }
}

void apply_reader_theme(ReaderTheme theme) {
    active_reader_theme = theme;
    Preferences p;
    p.begin("reader_cfg", false);
    p.putInt("theme", (int)theme);
    p.end();

    if (reader_container) {
        ThemeColors c = get_theme_colors(theme);
        lv_obj_set_style_bg_color(reader_container, c.bg, 0);
        lv_obj_set_style_text_color(reader_text_lbl, c.text, 0);
        lv_obj_set_style_text_color(reader_title_lbl, c.accent, 0);
        lv_obj_set_style_text_color(reader_page_lbl, c.dim, 0);
    }
}

void apply_reader_font(ReaderFontSize size) {
    active_reader_font = size;
    Preferences p;
    p.begin("reader_cfg", false);
    p.putInt("font", (int)size);
    p.end();

    if (reader_text_lbl) {
        lv_obj_set_style_text_font(reader_text_lbl, get_reader_font(size), 0);
    }
    // Re-paginate at the new font metrics if a book is open
    if (current_book_path.length() > 0) {
        reader_ui_open_book(current_book_path);
    }
}

// ─────────────────────────────────────────────
//  Library tab
// ─────────────────────────────────────────────
static void library_item_clicked_cb(lv_event_t* e) {
    const char* path = (const char*)lv_event_get_user_data(e);
    reader_ui_open_book(String(path));
}

void library_rebuild_list() {
    std::vector<LibraryEntry> entries = scan_library();

    if (!library_list) return;
    lv_obj_clean(library_list);

    if (entries.empty()) {
        lv_obj_t* lbl = lv_label_create(library_list);
        lv_label_set_text(lbl,
            "No books found.\n\n"
            "Copy .txt or .epub files into\n"
            "/Books/ on the SD card, then\n"
            "reopen this tab.");
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(lbl, lv_color_make(100,116,139), 0);
        lv_obj_center(lbl);
        return;
    }

    for (auto& entry : entries) {
        lv_obj_t* row = lv_obj_create(library_list);
        lv_obj_set_size(row, LV_PCT(100), 56);
        lv_obj_set_style_bg_color(row, lv_color_make(15,23,42), 0);
        lv_obj_set_style_border_color(row, lv_color_make(30,41,59), 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_pad_all(row, 8, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

        // Heap-allocate the path string for the lifetime of the button —
        // freed implicitly is NOT safe, so we intentionally leak a small
        // string per row (rows are rebuilt rarely; acceptable trade-off
        // on a device with no dynamic library churn at runtime).
        char* path_copy = strdup(entry.filename.c_str());
        lv_obj_add_event_cb(row, library_item_clicked_cb, LV_EVENT_CLICKED, path_copy);

        lv_obj_t* icon = lv_label_create(row);
        lv_label_set_text(icon, entry.format == BookFormat::EPUB ?
            LV_SYMBOL_FILE : LV_SYMBOL_EDIT);
        lv_obj_set_style_text_color(icon, lv_color_make(56,189,248), 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t* title_lbl = lv_label_create(row);
        lv_label_set_text(title_lbl, entry.title.c_str());
        lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(title_lbl, lv_color_make(226,232,240), 0);
        lv_obj_align_to(title_lbl, icon, LV_ALIGN_OUT_RIGHT_TOP, 8, -2);
        lv_label_set_long_mode(title_lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(title_lbl, 200);

        lv_obj_t* sub_lbl = lv_label_create(row);
        char sub_buf[48];
        const char* fmt_str = (entry.format == BookFormat::EPUB) ? "EPUB" : "TXT";
        snprintf(sub_buf, sizeof(sub_buf), "%s · %.0f%% read · %u KB",
            fmt_str, entry.percent_read, (unsigned)(entry.size_bytes / 1024));
        lv_label_set_text(sub_lbl, sub_buf);
        lv_obj_set_style_text_font(sub_lbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(sub_lbl, lv_color_make(100,116,139), 0);
        lv_obj_align_to(sub_lbl, title_lbl, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

        // Tiny progress bar
        lv_obj_t* pbar = lv_bar_create(row);
        lv_obj_set_size(pbar, 60, 6);
        lv_obj_align(pbar, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_bar_set_range(pbar, 0, 100);
        lv_bar_set_value(pbar, (int)entry.percent_read, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(pbar, lv_color_make(30,41,59), LV_PART_MAIN);
        lv_obj_set_style_bg_color(pbar, lv_color_make(34,197,94), LV_PART_INDICATOR);
        lv_obj_set_style_radius(pbar, 3, LV_PART_MAIN);
        lv_obj_set_style_radius(pbar, 3, LV_PART_INDICATOR);
    }
}

static void build_tab_library() {
    lv_obj_set_style_bg_color(tab_library, lv_color_make(8,12,22), 0);
    lv_obj_set_style_pad_all(tab_library, 6, 0);
    lv_obj_clear_flag(tab_library, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(tab_library);
    lv_label_set_text(title, LV_SYMBOL_DIRECTORY "  Library");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_make(56,189,248), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* refresh_btn = lv_btn_create(tab_library);
    lv_obj_set_size(refresh_btn, 30, 28);
    lv_obj_align(refresh_btn, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(refresh_btn, lv_color_make(30,41,59), 0);
    lv_obj_t* rlbl = lv_label_create(refresh_btn);
    lv_label_set_text(rlbl, LV_SYMBOL_REFRESH);
    lv_obj_center(rlbl);
    lv_obj_add_event_cb(refresh_btn, [](lv_event_t* e) {
        library_rebuild_list();
        post_toast("Library refreshed");
    }, LV_EVENT_CLICKED, nullptr);

    library_list = lv_obj_create(tab_library);
    lv_obj_set_size(library_list, LV_PCT(100), 360);
    lv_obj_align_to(library_list, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    lv_obj_set_style_bg_opa(library_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(library_list, 0, 0);
    lv_obj_set_style_pad_all(library_list, 0, 0);
    lv_obj_set_flex_flow(library_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(library_list, 6, 0);

    library_rebuild_list();
}

// ─────────────────────────────────────────────
//  Reader tab
// ─────────────────────────────────────────────
static void render_current_page_ui() {
    if (!reader_text_lbl) return;

    int total = book_get_page_count();
    int cur   = book_get_current_page();

    lv_label_set_text(reader_title_lbl, book_get_title().c_str());
    lv_label_set_text(reader_text_lbl, book_get_page_text(cur).c_str());

    float pct = (total > 1) ? (100.0f * cur / (total - 1)) : 100.0f;
    char page_buf[48];

    if (book_get_chapter_count() > 0) {
        snprintf(page_buf, sizeof(page_buf), "Ch %d/%d  ·  Pg %d/%d  ·  %.0f%%",
            book_get_current_chapter() + 1, book_get_chapter_count(),
            cur + 1, total, pct);
    } else {
        snprintf(page_buf, sizeof(page_buf), "Page %d / %d  ·  %.0f%%", cur + 1, total, pct);
    }
    lv_label_set_text(reader_page_lbl, page_buf);
    lv_bar_set_value(reader_progress_bar, (int)pct, LV_ANIM_ON);
}

void reader_render_current_page() { render_current_page_ui(); }

void reader_ui_open_book(const String& filepath) {
    // Save progress on whatever was previously open
    if (current_book_path.length() > 0) {
        progress_save();
    }

    int viewport_w = 300;   // reader text area width in px (320 - margins)
    int viewport_h = 330;   // reader text area height in px

    int fpw = get_font_px_w(active_reader_font);
    int fph = get_font_px_h(active_reader_font);

    bool ok = book_open(filepath, viewport_w, viewport_h, fpw, fph);
    if (!ok) {
        post_toast("Failed to open book — unsupported or corrupt file");
        return;
    }

    current_book_path = filepath;
    reading_session_start_ms = millis();

    render_current_page_ui();

    // Switch to the Reader tab
    if (tabview_ref) lv_tabview_set_act(tabview_ref, g_reader_tab_index, LV_ANIM_ON);

    post_toast(("Opened: " + book_get_title()).c_str());
}

void reader_close_book() {
    if (current_book_path.length() > 0) {
        progress_save();
        book_close();
        current_book_path = "";
    }
}

static void reader_next_page_cb(lv_event_t* e) {
    if (book_next_page()) {
        render_current_page_ui();
    } else {
        post_toast("End of book");
    }
}

static void reader_prev_page_cb(lv_event_t* e) {
    if (book_prev_page()) {
        render_current_page_ui();
    } else {
        post_toast("Start of book");
    }
}

static void reader_bookmark_cb(lv_event_t* e) {
    progress_save();
    post_toast("Bookmarked current page");
}

static void build_tab_reader() {
    ThemeColors c = get_theme_colors(active_reader_theme);

    lv_obj_set_style_bg_color(tab_reader, c.bg, 0);
    lv_obj_set_style_pad_all(tab_reader, 0, 0);
    lv_obj_clear_flag(tab_reader, LV_OBJ_FLAG_SCROLLABLE);

    reader_container = tab_reader;

    // Top bar: title + chapter nav
    reader_title_lbl = lv_label_create(tab_reader);
    lv_label_set_text(reader_title_lbl, "No book open");
    lv_obj_set_style_text_font(reader_title_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(reader_title_lbl, c.accent, 0);
    lv_label_set_long_mode(reader_title_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(reader_title_lbl, 300);
    lv_obj_align(reader_title_lbl, LV_ALIGN_TOP_MID, 0, 4);

    // Reading text area (tap zones: left third = prev, right third = next)
    reader_text_lbl = lv_label_create(tab_reader);
    lv_label_set_text(reader_text_lbl, "Open a book from the Library tab to begin reading.");
    lv_obj_set_style_text_font(reader_text_lbl, get_reader_font(active_reader_font), 0);
    lv_obj_set_style_text_color(reader_text_lbl, c.text, 0);
    lv_obj_set_width(reader_text_lbl, 300);
    lv_label_set_long_mode(reader_text_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_align(reader_text_lbl, LV_ALIGN_TOP_MID, 0, 30);

    // Invisible tap zones for page turning (covers most of the reading area)
    lv_obj_t* tap_left = lv_obj_create(tab_reader);
    lv_obj_set_size(tap_left, 100, 330);
    lv_obj_align(tap_left, LV_ALIGN_TOP_LEFT, 0, 30);
    lv_obj_set_style_bg_opa(tap_left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tap_left, 0, 0);
    lv_obj_add_event_cb(tap_left, reader_prev_page_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* tap_right = lv_obj_create(tab_reader);
    lv_obj_set_size(tap_right, 100, 330);
    lv_obj_align(tap_right, LV_ALIGN_TOP_RIGHT, 0, 30);
    lv_obj_set_style_bg_opa(tap_right, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tap_right, 0, 0);
    lv_obj_add_event_cb(tap_right, reader_next_page_cb, LV_EVENT_CLICKED, nullptr);

    // Bottom bar: progress + page number + bookmark
    reader_progress_bar = lv_bar_create(tab_reader);
    lv_obj_set_size(reader_progress_bar, 300, 6);
    lv_obj_align(reader_progress_bar, LV_ALIGN_BOTTOM_MID, 0, -36);
    lv_bar_set_range(reader_progress_bar, 0, 100);
    lv_obj_set_style_bg_color(reader_progress_bar, c.dim, LV_PART_MAIN);
    lv_obj_set_style_bg_color(reader_progress_bar, c.accent, LV_PART_INDICATOR);

    reader_page_lbl = lv_label_create(tab_reader);
    lv_label_set_text(reader_page_lbl, "");
    lv_obj_set_style_text_font(reader_page_lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(reader_page_lbl, c.dim, 0);
    lv_obj_align(reader_page_lbl, LV_ALIGN_BOTTOM_LEFT, 4, -6);

    lv_obj_t* bookmark_btn = lv_btn_create(tab_reader);
    lv_obj_set_size(bookmark_btn, 28, 22);
    lv_obj_align(bookmark_btn, LV_ALIGN_BOTTOM_RIGHT, -4, -6);
    lv_obj_set_style_bg_color(bookmark_btn, lv_color_make(30,41,59), 0);
    lv_obj_t* bm_lbl = lv_label_create(bookmark_btn);
    lv_label_set_text(bm_lbl, LV_SYMBOL_BOOKMARK);
    lv_obj_set_style_text_font(bm_lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(bm_lbl);
    lv_obj_add_event_cb(bookmark_btn, reader_bookmark_cb, LV_EVENT_CLICKED, nullptr);
}

// ─────────────────────────────────────────────
//  AI Modal (overlay shown during/after an AI request)
// ─────────────────────────────────────────────
static void build_ai_modal() {
    ai_modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(ai_modal, 300, 380);
    lv_obj_align(ai_modal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(ai_modal, lv_color_make(15,23,42), 0);
    lv_obj_set_style_border_color(ai_modal, lv_color_make(56,189,248), 0);
    lv_obj_set_style_border_width(ai_modal, 2, 0);
    lv_obj_set_style_radius(ai_modal, 10, 0);
    lv_obj_set_style_shadow_width(ai_modal, 20, 0);
    lv_obj_set_style_shadow_color(ai_modal, lv_color_make(0,0,0), 0);
    lv_obj_add_flag(ai_modal, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* title = lv_label_create(ai_modal);
    lv_label_set_text(title, LV_SYMBOL_IMAGE "  AI Assistant");
    lv_obj_set_style_text_color(title, lv_color_make(56,189,248), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 8);

    lv_obj_t* close_btn = lv_btn_create(ai_modal);
    lv_obj_set_size(close_btn, 26, 26);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, -6, 6);
    lv_obj_set_style_bg_color(close_btn, lv_color_make(127,29,29), 0);
    lv_obj_t* x_lbl = lv_label_create(close_btn);
    lv_label_set_text(x_lbl, LV_SYMBOL_CLOSE);
    lv_obj_center(x_lbl);
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) { hide_ai_modal(); }, LV_EVENT_CLICKED, nullptr);

    ai_modal_spinner = lv_spinner_create(ai_modal, 1000, 80);
    lv_obj_set_size(ai_modal_spinner, 40, 40);
    lv_obj_align(ai_modal_spinner, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_arc_color(ai_modal_spinner, lv_color_make(56,189,248), LV_PART_INDICATOR);

    ai_modal_text = lv_label_create(ai_modal);
    lv_label_set_text(ai_modal_text, "Thinking...");
    lv_obj_set_width(ai_modal_text, 280);
    lv_label_set_long_mode(ai_modal_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(ai_modal_text, lv_color_make(226,232,240), 0);
    lv_obj_set_style_text_font(ai_modal_text, &lv_font_montserrat_12, 0);
    lv_obj_align(ai_modal_text, LV_ALIGN_TOP_LEFT, 8, 40);
}

static void show_ai_modal(const String& loading_msg) {
    if (!ai_modal) return;
    lv_obj_clear_flag(ai_modal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ai_modal_spinner, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(ai_modal_text, loading_msg.c_str());
    lv_obj_set_y(ai_modal_text, 50);
}

static void hide_ai_modal() {
    if (ai_modal) lv_obj_add_flag(ai_modal, LV_OBJ_FLAG_HIDDEN);
}

void reader_ui_handle_ai_result(bool success, const String& text) {
    if (!ai_modal) return;
    lv_obj_add_flag(ai_modal_spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_y(ai_modal_text, 16);

    String display = success ? text : (String(LV_SYMBOL_WARNING) + " " + text);
    lv_label_set_text(ai_modal_text, display.c_str());

    if (success) {
        // Append to running chat history if this was a chat turn
        ai_chat_history_text += "\nAssistant: " + text;
    }
}

void reader_ui_handle_library_refresh() {
    library_rebuild_list();
    voice_rebuild_list();
}

// ─────────────────────────────────────────────
//  AI Study tab
// ─────────────────────────────────────────────
static void ai_action_cb(lv_event_t* e) {
    AIMode mode = (AIMode)(intptr_t)lv_event_get_user_data(e);

    if (current_book_path.length() == 0) {
        post_toast("Open a book first (Library tab)");
        return;
    }
    if (!ai_is_configured()) {
        post_toast("Add an AI API key in Settings first");
        return;
    }

    show_ai_modal("Contacting AI — this may take a few seconds...");

    String chapter_ctx = book_get_context_window(3000, 3000);

    AICallback cb = [](bool success, const String& result) {
        post_ai_result(success, result);
    };

    switch (mode) {
        case AIMode::SUMMARIZE:
            ai_summarize_page(book_get_page_text(book_get_current_page()), cb);
            break;
        case AIMode::STUDY_MCQ:
            ai_generate_mcqs(chapter_ctx, 5, cb);
            break;
        case AIMode::STUDY_SHORT_Q:
            ai_generate_short_questions(chapter_ctx, 5, cb);
            break;
        case AIMode::STUDY_VIVA:
            ai_generate_viva_questions(chapter_ctx, 5, cb);
            break;
        case AIMode::NOTE_EXTRACT:
            ai_extract_notes(chapter_ctx, cb);
            break;
        default:
            break;
    }
}

static void ai_chat_send_cb(lv_event_t* e) {
    if (!ai_chat_input) return;
    const char* q = lv_textarea_get_text(ai_chat_input);
    if (!q || strlen(q) == 0) return;

    if (current_book_path.length() == 0) {
        post_toast("Open a book first (Library tab)");
        return;
    }
    if (!ai_is_configured()) {
        post_toast("Add an AI API key in Settings first");
        return;
    }

    show_ai_modal("Thinking about your question...");
    String question = String(q);
    String chapter_ctx = book_get_context_window(3000, 3000);
    String history_copy = ai_chat_history_text;

    ai_chat_history_text += "\nUser: " + question;
    lv_textarea_set_text(ai_chat_input, "");

    ai_chat_with_book(chapter_ctx, history_copy, question, [](bool success, const String& result) {
        post_ai_result(success, result);
    });
}

static void build_tab_ai_study() {
    lv_obj_set_style_bg_color(tab_ai_study, lv_color_make(8,12,22), 0);
    lv_obj_set_style_pad_all(tab_ai_study, 6, 0);

    lv_obj_t* title = lv_label_create(tab_ai_study);
    lv_label_set_text(title, LV_SYMBOL_IMAGE "  AI Study Tools");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_make(56,189,248), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* hint = lv_label_create(tab_ai_study);
    lv_label_set_text(hint, "Runs on the currently open book/page.");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hint, lv_color_make(100,116,139), 0);
    lv_obj_align_to(hint, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

    struct { const char* label; AIMode mode; } actions[] = {
        { LV_SYMBOL_IMAGE   "  Summarize Page",      AIMode::SUMMARIZE     },
        { LV_SYMBOL_LIST    "  Extract Notes",       AIMode::NOTE_EXTRACT  },
        { LV_SYMBOL_EDIT    "  Generate MCQs",       AIMode::STUDY_MCQ     },
        { LV_SYMBOL_KEYBOARD"  Short Questions",     AIMode::STUDY_SHORT_Q },
        { LV_SYMBOL_AUDIO   "  Viva Questions",      AIMode::STUDY_VIVA    },
    };

    lv_obj_t* prev = hint;
    for (auto& a : actions) {
        lv_obj_t* btn = lv_btn_create(tab_ai_study);
        lv_obj_set_size(btn, LV_PCT(100), 38);
        lv_obj_align_to(btn, prev, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
        lv_obj_set_style_bg_color(btn, lv_color_make(30,41,59), 0);
        lv_obj_set_style_border_color(btn, lv_color_make(56,189,248), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, a.label);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);
        lv_obj_add_event_cb(btn, ai_action_cb, LV_EVENT_CLICKED, (void*)(intptr_t)a.mode);
        prev = btn;
    }

    // Chat with book section
    lv_obj_t* chat_title = lv_label_create(tab_ai_study);
    lv_label_set_text(chat_title, LV_SYMBOL_AUDIO "  Ask the Book");
    lv_obj_set_style_text_font(chat_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(chat_title, lv_color_make(56,189,248), 0);
    lv_obj_align_to(chat_title, prev, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 12);

    ai_chat_input = lv_textarea_create(tab_ai_study);
    lv_obj_set_size(ai_chat_input, 230, 36);
    lv_obj_align_to(ai_chat_input, chat_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
    lv_textarea_set_one_line(ai_chat_input, true);
    lv_textarea_set_placeholder_text(ai_chat_input, "e.g. What's the main idea?");
    lv_obj_set_style_bg_color(ai_chat_input, lv_color_make(15,23,42), 0);
    lv_obj_set_style_text_color(ai_chat_input, lv_color_make(226,232,240), 0);
    keyboard_attach(ai_chat_input);

    lv_obj_t* send_btn = lv_btn_create(tab_ai_study);
    lv_obj_set_size(send_btn, 70, 36);
    lv_obj_align_to(send_btn, ai_chat_input, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    lv_obj_set_style_bg_color(send_btn, lv_color_make(37,99,235), 0);
    lv_obj_t* send_lbl = lv_label_create(send_btn);
    lv_label_set_text(send_lbl, "Ask");
    lv_obj_center(send_lbl);
    lv_obj_add_event_cb(send_btn, ai_chat_send_cb, LV_EVENT_CLICKED, nullptr);
}

// ─────────────────────────────────────────────
//  Camera tab
// ─────────────────────────────────────────────
static void camera_scan_cb(lv_event_t* e) {
    if (!camera_is_available()) {
        post_toast("No camera module detected");
        return;
    }
    show_ai_modal("Capturing and reading text from page...");
    camera_scan_document([](bool success, const String& text) {
        post_ai_result(success, text);
        if (success && camera_result_ta) {
            lv_textarea_set_text(camera_result_ta, text.c_str());
        }
    });
}

static void camera_save_note_cb(lv_event_t* e) {
    if (!camera_result_ta) return;
    const char* txt = lv_textarea_get_text(camera_result_ta);
    if (!txt || strlen(txt) == 0) {
        post_toast("Nothing to save");
        return;
    }
    if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        File f = SD.open("/notes.txt", FILE_APPEND);
        if (f) {
            f.printf("[Scanned Note]: %s\n", txt);
            f.close();
            post_toast("Saved scanned text to notes");
        }
        xSemaphoreGive(sd_mutex);
    }
}

static void build_tab_camera() {
    lv_obj_set_style_bg_color(tab_camera, lv_color_make(8,12,22), 0);
    lv_obj_set_style_pad_all(tab_camera, 6, 0);

    lv_obj_t* title = lv_label_create(tab_camera);
    lv_label_set_text(title, LV_SYMBOL_IMAGE "  Camera & OCR");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_make(56,189,248), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    camera_preview_lbl = lv_label_create(tab_camera);
    lv_label_set_text(camera_preview_lbl, camera_is_available() ?
        "Camera ready. Point at a book page and tap Scan." :
        LV_SYMBOL_WARNING " No camera module detected.\n\n"
        "Wire an OV2640 module and add\n"
        "-D CAMERA_MODULE_ENABLED\n"
        "to platformio.ini build_flags.");
    lv_obj_set_style_text_font(camera_preview_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(camera_preview_lbl, lv_color_make(148,163,184), 0);
    lv_obj_set_style_text_align(camera_preview_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(camera_preview_lbl, 300);
    lv_label_set_long_mode(camera_preview_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_align_to(camera_preview_lbl, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);

    lv_obj_t* scan_btn = lv_btn_create(tab_camera);
    lv_obj_set_size(scan_btn, 300, 38);
    lv_obj_align_to(scan_btn, camera_preview_lbl, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);
    lv_obj_set_style_bg_color(scan_btn, lv_color_make(37,99,235), 0);
    lv_obj_set_style_radius(scan_btn, 6, 0);
    lv_obj_t* scan_lbl = lv_label_create(scan_btn);
    lv_label_set_text(scan_lbl, LV_SYMBOL_IMAGE "  Scan Page (OCR)");
    lv_obj_center(scan_lbl);
    lv_obj_add_event_cb(scan_btn, camera_scan_cb, LV_EVENT_CLICKED, nullptr);

    camera_result_ta = lv_textarea_create(tab_camera);
    lv_obj_set_size(camera_result_ta, 300, 180);
    lv_obj_align_to(camera_result_ta, scan_btn, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_textarea_set_placeholder_text(camera_result_ta, "Scanned text will appear here...");
    lv_obj_set_style_bg_color(camera_result_ta, lv_color_make(15,23,42), 0);
    lv_obj_set_style_text_color(camera_result_ta, lv_color_make(226,232,240), 0);
    keyboard_attach(camera_result_ta);

    lv_obj_t* save_btn = lv_btn_create(tab_camera);
    lv_obj_set_size(save_btn, 300, 34);
    lv_obj_align_to(save_btn, camera_result_ta, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
    lv_obj_set_style_bg_color(save_btn, lv_color_make(30,41,59), 0);
    lv_obj_set_style_border_color(save_btn, lv_color_make(34,197,94), 0);
    lv_obj_set_style_border_width(save_btn, 1, 0);
    lv_obj_t* save_lbl = lv_label_create(save_btn);
    lv_label_set_text(save_lbl, LV_SYMBOL_SAVE "  Save as Note");
    lv_obj_center(save_lbl);
    lv_obj_add_event_cb(save_btn, camera_save_note_cb, LV_EVENT_CLICKED, nullptr);
}

// ─────────────────────────────────────────────
//  Voice Notes tab — record from mic, play back, manage clips
// ─────────────────────────────────────────────
static void voice_rebuild_list() {
    if (!voice_list) return;
    lv_obj_clean(voice_list);

    std::vector<VoiceNote> notes = audio_list_voice_notes();
    if (notes.empty()) {
        lv_obj_t* lbl = lv_label_create(voice_list);
        lv_label_set_text(lbl, "No voice notes yet.\nTap Record to make one.");
        lv_obj_set_style_text_color(lbl, lv_color_make(100,116,139), 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }

    for (auto& note : notes) {
        lv_obj_t* row = lv_obj_create(voice_list);
        lv_obj_set_size(row, LV_PCT(100), 42);
        lv_obj_set_style_bg_color(row, lv_color_make(15,23,42), 0);
        lv_obj_set_style_border_color(row, lv_color_make(30,41,59), 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* lbl = lv_label_create(row);
        char buf[48];
        int slash = note.filename.lastIndexOf('/');
        String shortname = note.filename.substring(slash + 1);
        snprintf(buf, sizeof(buf), "%s  (%lu:%02lu)",
            shortname.c_str(),
            (unsigned long)(note.duration_sec / 60),
            (unsigned long)(note.duration_sec % 60));
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 6, 0);

        lv_obj_t* play_btn = lv_btn_create(row);
        lv_obj_set_size(play_btn, 30, 28);
        lv_obj_align(play_btn, LV_ALIGN_RIGHT_MID, -38, 0);
        lv_obj_set_style_bg_color(play_btn, lv_color_make(37,99,235), 0);
        lv_obj_t* play_lbl = lv_label_create(play_btn);
        lv_label_set_text(play_lbl, LV_SYMBOL_PLAY);
        lv_obj_center(play_lbl);

        char* path_copy = strdup(note.filename.c_str());
        lv_obj_add_event_cb(play_btn, [](lv_event_t* e) {
            const char* path = (const char*)lv_event_get_user_data(e);
            if (audio_is_playing()) {
                post_toast("Already playing a clip");
                return;
            }
            voice_currently_playing = String(path);
            post_toast(("Playing " + voice_currently_playing).c_str());
            // Playback blocks for the clip duration, so the actual I2S
            // work runs on Core 0 via the request queue in audio_engine.
            audio_request_playback(String(path));
        }, LV_EVENT_CLICKED, path_copy);

        lv_obj_t* del_btn = lv_btn_create(row);
        lv_obj_set_size(del_btn, 30, 28);
        lv_obj_align(del_btn, LV_ALIGN_RIGHT_MID, -2, 0);
        lv_obj_set_style_bg_color(del_btn, lv_color_make(127,29,29), 0);
        lv_obj_t* del_lbl = lv_label_create(del_btn);
        lv_label_set_text(del_lbl, LV_SYMBOL_TRASH);
        lv_obj_center(del_lbl);

        char* path_copy2 = strdup(note.filename.c_str());
        lv_obj_add_event_cb(del_btn, [](lv_event_t* e) {
            const char* path = (const char*)lv_event_get_user_data(e);
            if (audio_delete_voice_note(String(path))) {
                post_toast("Deleted");
                voice_rebuild_list();
            } else {
                post_toast("Delete failed");
            }
        }, LV_EVENT_CLICKED, path_copy2);
    }
}

static void voice_record_tick_cb(lv_timer_t* t) {
    if (!audio_is_recording()) return;
    uint32_t sec = audio_record_elapsed_sec();
    if (voice_status_lbl) {
        lv_label_set_text_fmt(voice_status_lbl, LV_SYMBOL_AUDIO "  Recording... %lu:%02lu",
            (unsigned long)(sec / 60), (unsigned long)(sec % 60));
    }
}

static void voice_record_btn_cb(lv_event_t* e) {
    if (!audio_is_recording()) {
        // Actual I2S start happens via a cross-core request the same
        // way playback does, to keep blocking I2S/SD work off Core 1.
        audio_request_record_start();

        lv_label_set_text(voice_record_btn_lbl, LV_SYMBOL_STOP "  Stop");
        if (!voice_record_timer) {
            voice_record_timer = lv_timer_create(voice_record_tick_cb, 500, nullptr);
        }
    } else {
        audio_request_record_stop();

        lv_label_set_text(voice_record_btn_lbl, LV_SYMBOL_AUDIO "  Record");
        if (voice_record_timer) { lv_timer_del(voice_record_timer); voice_record_timer = nullptr; }
        lv_label_set_text(voice_status_lbl, "Saved. Refreshing list...");
    }
}

static void build_tab_voice() {
    lv_obj_set_style_bg_color(tab_voice, lv_color_make(8,12,22), 0);
    lv_obj_set_style_pad_all(tab_voice, 6, 0);

    lv_obj_t* title = lv_label_create(tab_voice);
    lv_label_set_text(title, LV_SYMBOL_AUDIO "  Voice Notes");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_make(56,189,248), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    voice_status_lbl = lv_label_create(tab_voice);
    lv_label_set_text(voice_status_lbl, audio_is_available() ?
        "Ready to record." :
        LV_SYMBOL_WARNING " Mic/speaker not detected.\nWire an I2S mic + amp and check audio_engine.h pins.");
    lv_obj_set_style_text_font(voice_status_lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(voice_status_lbl, lv_color_make(148,163,184), 0);
    lv_obj_set_width(voice_status_lbl, 300);
    lv_label_set_long_mode(voice_status_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_align_to(voice_status_lbl, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

    voice_record_btn = lv_btn_create(tab_voice);
    lv_obj_set_size(voice_record_btn, 300, 44);
    lv_obj_align_to(voice_record_btn, voice_status_lbl, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_obj_set_style_bg_color(voice_record_btn, lv_color_make(127,29,29), 0);
    lv_obj_set_style_radius(voice_record_btn, 8, 0);
    voice_record_btn_lbl = lv_label_create(voice_record_btn);
    lv_label_set_text(voice_record_btn_lbl, LV_SYMBOL_AUDIO "  Record");
    lv_obj_set_style_text_font(voice_record_btn_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(voice_record_btn_lbl);
    lv_obj_add_event_cb(voice_record_btn, voice_record_btn_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* list_title = lv_label_create(tab_voice);
    lv_label_set_text(list_title, "Saved Notes");
    lv_obj_set_style_text_font(list_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(list_title, lv_color_make(56,189,248), 0);
    lv_obj_align_to(list_title, voice_record_btn, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

    voice_list = lv_obj_create(tab_voice);
    lv_obj_set_size(voice_list, LV_PCT(100), 220);
    lv_obj_align_to(voice_list, list_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
    lv_obj_set_style_bg_opa(voice_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(voice_list, 0, 0);
    lv_obj_set_style_pad_all(voice_list, 0, 0);
    lv_obj_set_flex_flow(voice_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(voice_list, 6, 0);

    voice_rebuild_list();
}

// ─────────────────────────────────────────────
//  Settings tab — Wi-Fi manager + AI provider config + reader prefs
// ─────────────────────────────────────────────

// Cross-core Wi-Fi connect request queue (struct declared in
// wifi_manager.h). network.cpp's task drains this and calls
// wifi_connect_sta() so the blocking 15s connect attempt never
// happens on the LVGL/UI core.
QueueHandle_t wifi_connect_request_queue = nullptr;

static lv_obj_t* wifi_pass_modal   = nullptr;
static lv_obj_t* wifi_pass_input   = nullptr;
static String    wifi_pending_ssid;

static void wifi_pass_submit_cb(lv_event_t* e) {
    const char* pass = lv_textarea_get_text(wifi_pass_input);

    if (!wifi_connect_request_queue) {
        wifi_connect_request_queue = xQueueCreate(2, sizeof(WiFiConnectRequest));
    }
    WiFiConnectRequest req = {};
    strncpy(req.ssid, wifi_pending_ssid.c_str(), sizeof(req.ssid) - 1);
    strncpy(req.pass, pass ? pass : "", sizeof(req.pass) - 1);
    xQueueSend(wifi_connect_request_queue, &req, 0);

    post_toast(("Connecting to " + wifi_pending_ssid + "...").c_str());
    if (wifi_pass_modal) lv_obj_add_flag(wifi_pass_modal, LV_OBJ_FLAG_HIDDEN);
}

static void build_wifi_pass_modal() {
    wifi_pass_modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(wifi_pass_modal, 300, 180);
    lv_obj_align(wifi_pass_modal, LV_ALIGN_CENTER, 0, -60);
    lv_obj_set_style_bg_color(wifi_pass_modal, lv_color_make(15,23,42), 0);
    lv_obj_set_style_border_color(wifi_pass_modal, lv_color_make(56,189,248), 0);
    lv_obj_set_style_border_width(wifi_pass_modal, 2, 0);
    lv_obj_set_style_radius(wifi_pass_modal, 10, 0);
    lv_obj_add_flag(wifi_pass_modal, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* lbl = lv_label_create(wifi_pass_modal);
    lv_label_set_text(lbl, "Enter Wi-Fi Password");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 6, 6);

    wifi_pass_input = lv_textarea_create(wifi_pass_modal);
    lv_obj_set_size(wifi_pass_input, 280, 36);
    lv_obj_align(wifi_pass_input, LV_ALIGN_TOP_LEFT, 6, 30);
    lv_textarea_set_one_line(wifi_pass_input, true);
    lv_textarea_set_password_mode(wifi_pass_input, true);
    lv_obj_set_style_bg_color(wifi_pass_input, lv_color_make(10,14,24), 0);
    keyboard_attach(wifi_pass_input);

    lv_obj_t* connect_btn = lv_btn_create(wifi_pass_modal);
    lv_obj_set_size(connect_btn, 130, 32);
    lv_obj_align(connect_btn, LV_ALIGN_BOTTOM_LEFT, 6, -6);
    lv_obj_set_style_bg_color(connect_btn, lv_color_make(37,99,235), 0);
    lv_obj_t* c_lbl = lv_label_create(connect_btn);
    lv_label_set_text(c_lbl, "Connect");
    lv_obj_center(c_lbl);
    lv_obj_add_event_cb(connect_btn, wifi_pass_submit_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* cancel_btn = lv_btn_create(wifi_pass_modal);
    lv_obj_set_size(cancel_btn, 130, 32);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_RIGHT, -6, -6);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_make(60,60,70), 0);
    lv_obj_t* x_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(x_lbl, "Cancel");
    lv_obj_center(x_lbl);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t* e) {
        lv_obj_add_flag(wifi_pass_modal, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_CLICKED, nullptr);
}

static void wifi_connect_attempt_cb(lv_event_t* e) {
    const char* ssid = (const char*)lv_event_get_user_data(e);
    wifi_pending_ssid = String(ssid);
    if (!wifi_pass_modal) build_wifi_pass_modal();
    lv_textarea_set_text(wifi_pass_input, "");
    lv_obj_clear_flag(wifi_pass_modal, LV_OBJ_FLAG_HIDDEN);
}

static void build_settings_wifi_section(lv_obj_t* parent) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, LV_SYMBOL_WIFI "  Wi-Fi Networks");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl, lv_color_make(56,189,248), 0);

    lv_obj_t* scan_btn = lv_btn_create(parent);
    lv_obj_set_size(scan_btn, 90, 26);
    lv_obj_set_style_bg_color(scan_btn, lv_color_make(30,41,59), 0);
    lv_obj_t* scan_lbl = lv_label_create(scan_btn);
    lv_label_set_text(scan_lbl, "Scan");
    lv_obj_center(scan_lbl);

    settings_wifi_list = lv_obj_create(parent);
    lv_obj_set_size(settings_wifi_list, LV_PCT(100), 120);
    lv_obj_set_flex_flow(settings_wifi_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(settings_wifi_list, 4, 0);
    lv_obj_set_style_bg_opa(settings_wifi_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(settings_wifi_list, 0, 0);

    lv_obj_add_event_cb(scan_btn, [](lv_event_t* e) {
        lv_obj_clean(settings_wifi_list);
        std::vector<WiFiScanResult> nets = wifi_scan_networks();
        for (auto& n : nets) {
            lv_obj_t* row = lv_obj_create(settings_wifi_list);
            lv_obj_set_size(row, LV_PCT(100), 30);
            lv_obj_set_style_bg_color(row, lv_color_make(15,23,42), 0);
            lv_obj_set_style_radius(row, 4, 0);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

            lv_obj_t* nlbl = lv_label_create(row);
            String text = (n.secured ? String(LV_SYMBOL_CLOSE) : String(LV_SYMBOL_OK)) + " " + n.ssid;
            lv_label_set_text(nlbl, text.c_str());
            lv_obj_set_style_text_font(nlbl, &lv_font_montserrat_10, 0);
            lv_obj_align(nlbl, LV_ALIGN_LEFT_MID, 4, 0);

            char* ssid_copy = strdup(n.ssid.c_str());
            lv_obj_add_event_cb(row, wifi_connect_attempt_cb, LV_EVENT_CLICKED, ssid_copy);
        }
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_align_to(scan_btn, lbl, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    lv_obj_align_to(settings_wifi_list, lbl, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
}

static void ai_save_key_cb(lv_event_t* e) {
    if (!settings_ai_key_ta) return;
    const char* key = lv_textarea_get_text(settings_ai_key_ta);
    if (key && strlen(key) > 0) {
        ai_configure(AIProvider::GEMINI, String(key));
        post_toast("AI key saved");
    }
}

static void theme_btn_cb(lv_event_t* e) {
    ReaderTheme t = (ReaderTheme)(intptr_t)lv_event_get_user_data(e);
    apply_reader_theme(t);
    post_toast("Theme applied");
}

static void font_btn_cb(lv_event_t* e) {
    ReaderFontSize f = (ReaderFontSize)(intptr_t)lv_event_get_user_data(e);
    apply_reader_font(f);
    post_toast("Font size applied");
}

static void build_tab_settings() {
    lv_obj_set_style_bg_color(tab_settings, lv_color_make(8,12,22), 0);
    lv_obj_set_style_pad_all(tab_settings, 8, 0);
    lv_obj_set_flex_flow(tab_settings, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tab_settings, 14, 0);

    // Wi-Fi section
    lv_obj_t* wifi_box = lv_obj_create(tab_settings);
    lv_obj_set_size(wifi_box, LV_PCT(100), 190);
    lv_obj_set_style_bg_color(wifi_box, lv_color_make(15,23,42), 0);
    lv_obj_set_style_border_color(wifi_box, lv_color_make(30,41,59), 0);
    lv_obj_set_style_radius(wifi_box, 8, 0);
    lv_obj_set_style_pad_all(wifi_box, 8, 0);
    build_settings_wifi_section(wifi_box);

    // AI provider section
    lv_obj_t* ai_box = lv_obj_create(tab_settings);
    lv_obj_set_size(ai_box, LV_PCT(100), 110);
    lv_obj_set_style_bg_color(ai_box, lv_color_make(15,23,42), 0);
    lv_obj_set_style_border_color(ai_box, lv_color_make(30,41,59), 0);
    lv_obj_set_style_radius(ai_box, 8, 0);
    lv_obj_set_style_pad_all(ai_box, 8, 0);

    lv_obj_t* ai_lbl = lv_label_create(ai_box);
    lv_label_set_text(ai_lbl, LV_SYMBOL_IMAGE "  AI Assistant (Gemini API Key)");
    lv_obj_set_style_text_font(ai_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ai_lbl, lv_color_make(56,189,248), 0);

    settings_ai_key_ta = lv_textarea_create(ai_box);
    lv_obj_set_size(settings_ai_key_ta, LV_PCT(100), 36);
    lv_textarea_set_one_line(settings_ai_key_ta, true);
    lv_textarea_set_password_mode(settings_ai_key_ta, true);
    lv_textarea_set_placeholder_text(settings_ai_key_ta, "AIza...");
    lv_obj_set_style_bg_color(settings_ai_key_ta, lv_color_make(10,14,24), 0);
    lv_obj_align_to(settings_ai_key_ta, ai_lbl, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
    keyboard_attach(settings_ai_key_ta);

    lv_obj_t* save_key_btn = lv_btn_create(ai_box);
    lv_obj_set_size(save_key_btn, 100, 28);
    lv_obj_align_to(save_key_btn, settings_ai_key_ta, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 6);
    lv_obj_set_style_bg_color(save_key_btn, lv_color_make(37,99,235), 0);
    lv_obj_t* sk_lbl = lv_label_create(save_key_btn);
    lv_label_set_text(sk_lbl, "Save Key");
    lv_obj_center(sk_lbl);
    lv_obj_add_event_cb(save_key_btn, ai_save_key_cb, LV_EVENT_CLICKED, nullptr);

    // Reader appearance section
    lv_obj_t* appearance_box = lv_obj_create(tab_settings);
    lv_obj_set_size(appearance_box, LV_PCT(100), 130);
    lv_obj_set_style_bg_color(appearance_box, lv_color_make(15,23,42), 0);
    lv_obj_set_style_border_color(appearance_box, lv_color_make(30,41,59), 0);
    lv_obj_set_style_radius(appearance_box, 8, 0);
    lv_obj_set_style_pad_all(appearance_box, 8, 0);

    lv_obj_t* app_lbl = lv_label_create(appearance_box);
    lv_label_set_text(app_lbl, LV_SYMBOL_SETTINGS "  Reader Appearance");
    lv_obj_set_style_text_font(app_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(app_lbl, lv_color_make(56,189,248), 0);

    lv_obj_t* theme_row = lv_obj_create(appearance_box);
    lv_obj_set_size(theme_row, LV_PCT(100), 32);
    lv_obj_set_style_bg_opa(theme_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(theme_row, 0, 0);
    lv_obj_set_style_pad_all(theme_row, 0, 0);
    lv_obj_align_to(theme_row, app_lbl, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
    lv_obj_set_flex_flow(theme_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(theme_row, 4, 0);

    struct { const char* lbl; ReaderTheme t; } themes[] = {
        { "Dark", THEME_DARK }, { "Light", THEME_LIGHT },
        { "Sepia", THEME_SEPIA }, { "OLED", THEME_OLED_BLACK },
    };
    for (auto& t : themes) {
        lv_obj_t* btn = lv_btn_create(theme_row);
        lv_obj_set_size(btn, 65, 28);
        lv_obj_set_style_bg_color(btn, lv_color_make(30,41,59), 0);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, t.lbl);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, theme_btn_cb, LV_EVENT_CLICKED, (void*)(intptr_t)t.t);
    }

    lv_obj_t* font_row = lv_obj_create(appearance_box);
    lv_obj_set_size(font_row, LV_PCT(100), 32);
    lv_obj_set_style_bg_opa(font_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(font_row, 0, 0);
    lv_obj_set_style_pad_all(font_row, 0, 0);
    lv_obj_align_to(font_row, theme_row, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    lv_obj_set_flex_flow(font_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(font_row, 4, 0);

    struct { const char* lbl; ReaderFontSize f; } fonts[] = {
        { "Small", FONT_SMALL }, { "Med", FONT_MEDIUM },
        { "Large", FONT_LARGE }, { "Dyslexic", FONT_DYSLEXIC },
    };
    for (auto& f : fonts) {
        lv_obj_t* btn = lv_btn_create(font_row);
        lv_obj_set_size(btn, 65, 28);
        lv_obj_set_style_bg_color(btn, lv_color_make(30,41,59), 0);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, f.lbl);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, font_btn_cb, LV_EVENT_CLICKED, (void*)(intptr_t)f.f);
    }
}

// ─────────────────────────────────────────────
//  Public entry points
// ─────────────────────────────────────────────
void reader_ui_init(lv_obj_t* parent_tabview) {
    tabview_ref = parent_tabview;

    // Load persisted reader prefs
    Preferences p;
    p.begin("reader_cfg", true);
    active_reader_theme = (ReaderTheme)p.getInt("theme", (int)THEME_DARK);
    active_reader_font  = (ReaderFontSize)p.getInt("font",  (int)FONT_MEDIUM);
    p.end();

    g_library_tab_index = lv_tabview_get_tab_count(parent_tabview);
    tab_library  = lv_tabview_add_tab(parent_tabview, LV_SYMBOL_DIRECTORY "  Library");
    g_reader_tab_index = lv_tabview_get_tab_count(parent_tabview);
    tab_reader   = lv_tabview_add_tab(parent_tabview, LV_SYMBOL_FILE "  Reader");
    tab_ai_study = lv_tabview_add_tab(parent_tabview, LV_SYMBOL_IMAGE "  AI");
    tab_camera   = lv_tabview_add_tab(parent_tabview, LV_SYMBOL_IMAGE "  Cam");
    tab_voice    = lv_tabview_add_tab(parent_tabview, LV_SYMBOL_AUDIO "  Voice");
    tab_settings = lv_tabview_add_tab(parent_tabview, LV_SYMBOL_SETTINGS "  Wi-Fi/AI");

    build_tab_library();
    build_tab_reader();
    build_tab_ai_study();
    build_tab_camera();
    build_tab_voice();
    build_tab_settings();
    build_ai_modal();

    Serial.println("[ReaderUI] Library/Reader/AI/Camera/Voice/Settings tabs initialized.");
}

void reader_ui_update_clock(int hour, int minute) {
    UIEventMsg msg = {};
    msg.type = UI_EVT_TIME_UPDATE;
    msg.val1 = hour;
    msg.val2 = minute;
    extern QueueHandle_t ui_event_queue;
    xQueueSend(ui_event_queue, &msg, 0);
}

// ─────────────────────────────────────────────
//  Aliases satisfying the public contract declared in ui_engine.h
//  (ui_engine.h is the original, stable public surface; reader_ui.h
//  is this module's own header used internally and by main.cpp).
// ─────────────────────────────────────────────
void reader_open_book(const String& filepath) { reader_ui_open_book(filepath); }