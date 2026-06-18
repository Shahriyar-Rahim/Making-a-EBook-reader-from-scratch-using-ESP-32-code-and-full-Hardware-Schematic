// ═══════════════════════════════════════════════════════════════════
//  ui_tabs_reader.cpp  —  New tab implementations for the Reader
//  Tabs: Library · Reader · AI Panel · WiFi Manager · Stats
//  All tabs are added to the existing tabview from ui_engine.cpp
// ═══════════════════════════════════════════════════════════════════
#include "ui_engine.h"
#include "book_engine.h"
#include "ai_engine.h"
#include "wifi_manager.h"
#include <lvgl.h>
#include <SD.h>

// ─────────────────────────────────────────────
//  External tabview handle from ui_engine.cpp
// ─────────────────────────────────────────────
extern lv_obj_t* tabview;
extern lv_obj_t* scr_main;

// ─────────────────────────────────────────────
//  Tab widget handles
// ─────────────────────────────────────────────

// ── Library Tab ──────────────────────────────
static lv_obj_t* tab_library     = nullptr;
static lv_obj_t* lib_list        = nullptr;
static lv_obj_t* lib_scan_btn    = nullptr;
static lv_obj_t* lib_empty_label = nullptr;

// ── Reader Tab ───────────────────────────────
static lv_obj_t* tab_reader      = nullptr;
static lv_obj_t* reader_title    = nullptr;
static lv_obj_t* reader_text     = nullptr;
static lv_obj_t* reader_progress = nullptr;
static lv_obj_t* reader_page_lbl = nullptr;
static lv_obj_t* reader_prev_btn = nullptr;
static lv_obj_t* reader_next_btn = nullptr;
static lv_obj_t* reader_bm_btn   = nullptr;
static lv_obj_t* reader_ai_btn   = nullptr;
static lv_obj_t* reader_theme_btn= nullptr;
static lv_obj_t* reader_no_book  = nullptr;
static lv_obj_t* reader_jump_slider = nullptr;

// ── AI Panel Tab ─────────────────────────────
static lv_obj_t* tab_ai          = nullptr;
static lv_obj_t* ai_result_area  = nullptr;
static lv_obj_t* ai_chat_ta      = nullptr;
static lv_obj_t* ai_status_lbl   = nullptr;
static lv_obj_t* ai_save_btn     = nullptr;

// ── WiFi Manager Tab ─────────────────────────
static lv_obj_t* tab_wifi        = nullptr;
static lv_obj_t* wifi_status_lbl = nullptr;
static lv_obj_t* wifi_scan_btn   = nullptr;
static lv_obj_t* wifi_net_list   = nullptr;
static lv_obj_t* wifi_pass_ta    = nullptr;
static lv_obj_t* wifi_conn_btn   = nullptr;
static lv_obj_t* wifi_ip_lbl     = nullptr;
static char      wifi_selected_ssid[33] = {};

// ── Stats Tab ────────────────────────────────
static lv_obj_t* tab_stats       = nullptr;
static lv_obj_t* stats_pages_lbl = nullptr;
static lv_obj_t* stats_time_lbl  = nullptr;
static lv_obj_t* stats_books_lbl = nullptr;
static lv_obj_t* stats_streak_lbl= nullptr;
static lv_obj_t* stats_arc       = nullptr;

// ─────────────────────────────────────────────
//  Helper: apply current theme colors to reader
// ─────────────────────────────────────────────
static void apply_reader_theme() {
    if (!reader_text || !tab_reader) return;
    ThemeColors tc = book_get_theme();

    lv_color_t bg  = lv_color_make(tc.bg_r,  tc.bg_g,  tc.bg_b);
    lv_color_t fg  = lv_color_make(tc.fg_r,  tc.fg_g,  tc.fg_b);
    lv_color_t hdr = lv_color_make(tc.hdr_r, tc.hdr_g, tc.hdr_b);

    lv_obj_set_style_bg_color(tab_reader,   bg, 0);
    lv_obj_set_style_bg_color(reader_text,  bg, 0);
    lv_obj_set_style_text_color(reader_text, fg, 0);
    if (reader_title)
        lv_obj_set_style_text_color(reader_title, fg, 0);
}

// ─────────────────────────────────────────────
//  Font size map
// ─────────────────────────────────────────────
static const lv_font_t* get_read_font() {
    switch (current_font) {
    case FONT_SMALL:  return &lv_font_montserrat_10;
    case FONT_MEDIUM: return &lv_font_montserrat_14;
    case FONT_LARGE:  return &lv_font_montserrat_16;
    case FONT_XL:     return &lv_font_montserrat_20;
    default:          return &lv_font_montserrat_14;
    }
}

// ─────────────────────────────────────────────
//  Library Tab
// ─────────────────────────────────────────────
static void on_lib_book_click(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= book_count) return;

    post_toast("Opening book...");

    // Switch to reader tab first so user sees feedback
    lv_tabview_set_act(tabview, 1, LV_ANIM_ON); // Tab index 1 = Reader

    if (book_open(idx)) {
        reader_ui_refresh();
    } else {
        post_toast("Cannot open book");
    }
}

static void on_lib_scan_click(lv_event_t* e) {
    post_toast("Scanning /Books...");
    int n = book_scan_library();
    library_ui_refresh();
    if (n == 0) {
        post_toast("No books found on SD");
    } else {
        char msg[48];
        snprintf(msg, sizeof(msg), "Found %d book(s)", n);
        post_toast(msg);
    }
}

void build_tab_library(lv_obj_t* tv) {
    tab_library = lv_tabview_add_tab(tv, LV_SYMBOL_FILE "  Books");
    lv_obj_set_style_pad_all(tab_library, 6, 0);
    lv_obj_set_style_bg_color(tab_library, lv_color_make(8, 12, 22), 0);

    // Header row
    lv_obj_t* hdr = lv_obj_create(tab_library);
    lv_obj_set_size(hdr, 308, 30);
    lv_obj_set_style_bg_color(hdr, lv_color_make(10, 15, 30), 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 4, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(hdr, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* title = lv_label_create(hdr);
    lv_label_set_text(title, LV_SYMBOL_FILE "  Library");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_make(56, 189, 248), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    lib_scan_btn = lv_btn_create(hdr);
    lv_obj_set_size(lib_scan_btn, 60, 22);
    lv_obj_align(lib_scan_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(lib_scan_btn, lv_color_make(37, 99, 235), 0);
    lv_obj_set_style_radius(lib_scan_btn, 4, 0);
    lv_obj_t* sb_lbl = lv_label_create(lib_scan_btn);
    lv_label_set_text(sb_lbl, LV_SYMBOL_REFRESH " Scan");
    lv_obj_set_style_text_font(sb_lbl, &lv_font_montserrat_10, 0);
    lv_obj_center(sb_lbl);
    lv_obj_add_event_cb(lib_scan_btn, on_lib_scan_click, LV_EVENT_CLICKED, nullptr);

    // Book list (scrollable)
    lib_list = lv_list_create(tab_library);
    lv_obj_set_size(lib_list, 308, 320);
    lv_obj_align_to(lib_list, hdr, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
    lv_obj_set_style_bg_color(lib_list, lv_color_make(8, 12, 22), 0);
    lv_obj_set_style_border_color(lib_list, lv_color_make(30, 41, 59), 0);
    lv_obj_set_style_radius(lib_list, 6, 0);
    lv_obj_set_style_pad_all(lib_list, 4, 0);
    lv_obj_set_style_pad_row(lib_list, 3, 0);

    lib_empty_label = lv_label_create(tab_library);
    lv_label_set_text(lib_empty_label, "No books found.\nPut .txt or .epub files\nin /Books on SD card,\nthen tap Scan.");
    lv_obj_set_style_text_font(lib_empty_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lib_empty_label, lv_color_make(100, 116, 139), 0);
    lv_obj_set_style_text_align(lib_empty_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lib_empty_label, LV_ALIGN_CENTER, 0, 30);

    // Auto-scan on first build
    book_scan_library();
    library_ui_refresh();
}

void library_ui_refresh() {
    if (!lib_list) return;

    lv_list_clean(lib_list);

    if (book_count == 0) {
        lv_obj_clear_flag(lib_empty_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(lib_empty_label, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < book_count; i++) {
        BookEntry& b = book_library[i];

        char entry_text[96];
        if (b.total_pages > 0) {
            int pct = (int)(b.progress * 100.0f);
            snprintf(entry_text, sizeof(entry_text),
                     "%s  [%d%%]", b.title, pct);
        } else {
            snprintf(entry_text, sizeof(entry_text), "%s", b.title);
        }

        lv_obj_t* btn = lv_list_add_btn(lib_list,
                         b.is_epub ? LV_SYMBOL_FILE : LV_SYMBOL_LIST,
                         entry_text);

        lv_obj_set_style_bg_color(btn, lv_color_make(15, 23, 42), 0);
        lv_obj_set_style_bg_color(btn, lv_color_make(30, 41, 59), LV_STATE_PRESSED);
        lv_obj_set_style_text_color(btn, lv_color_make(200, 210, 230), 0);
        lv_obj_set_style_text_font(btn, &lv_font_montserrat_12, 0);
        lv_obj_set_style_border_color(btn, lv_color_make(30, 41, 59), 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_min_height(btn, 36, 0);

        // Progress bar on each entry
        if (b.total_pages > 0 && b.progress > 0) {
            lv_obj_t* prog = lv_bar_create(btn);
            lv_obj_set_size(prog, 80, 3);
            lv_obj_align(prog, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
            lv_bar_set_range(prog, 0, 100);
            lv_bar_set_value(prog, (int)(b.progress * 100), LV_ANIM_OFF);
            lv_obj_set_style_bg_color(prog, lv_color_make(30, 41, 59), LV_PART_MAIN);
            lv_obj_set_style_bg_color(prog, lv_color_make(56, 189, 248), LV_PART_INDICATOR);
        }

        lv_obj_add_event_cb(btn, on_lib_book_click, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
}

// ─────────────────────────────────────────────
//  Reader Tab
// ─────────────────────────────────────────────
static void on_reader_prev(lv_event_t* e) {
    if (!current_book) return;
    if (book_prev_page()) {
        reader_ui_refresh();
    } else {
        post_toast("First page");
    }
}

static void on_reader_next(lv_event_t* e) {
    if (!current_book) return;
    if (book_next_page()) {
        reader_ui_refresh();
        if (current_page == total_pages - 1) {
            post_toast("End of book!");
        }
    } else {
        post_toast("Last page");
    }
}

static void on_reader_bookmark(lv_event_t* e) {
    if (!current_book) return;
    book_set_bookmark();
    post_toast("Bookmark saved!");
}

static void on_reader_ai(lv_event_t* e) {
    if (!current_book || strlen(page_buffer) == 0) {
        post_toast("Open a book first");
        return;
    }
    // Switch to AI tab
    lv_tabview_set_act(tabview, 3, LV_ANIM_ON); // Tab 3 = AI
    post_toast("Tap an AI action below");
}

static void on_reader_theme(lv_event_t* e) {
    if (!current_book) return;
    int next = ((int)current_theme + 1) % THEME_COUNT;
    book_set_theme((ReadTheme)next);
    apply_reader_theme();
    post_toast(THEMES[current_theme].name);
}

static void on_reader_jump(lv_event_t* e) {
    if (!current_book || !reader_jump_slider) return;
    int val = lv_slider_get_value(reader_jump_slider);
    book_jump_to_percent(val / 100.0f);
    reader_ui_refresh();
}

void build_tab_reader(lv_obj_t* tv) {
    tab_reader = lv_tabview_add_tab(tv, LV_SYMBOL_EDIT "  Read");
    lv_obj_set_style_pad_all(tab_reader, 4, 0);
    lv_obj_set_style_bg_color(tab_reader, lv_color_make(8, 12, 22), 0);
    lv_obj_clear_flag(tab_reader, LV_OBJ_FLAG_SCROLLABLE);

    // "No book" placeholder
    reader_no_book = lv_label_create(tab_reader);
    lv_label_set_text(reader_no_book, LV_SYMBOL_FILE "\n\nNo book open.\nGo to Books tab\nand select a book.");
    lv_obj_set_style_text_font(reader_no_book, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(reader_no_book, lv_color_make(100, 116, 139), 0);
    lv_obj_set_style_text_align(reader_no_book, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(reader_no_book, LV_ALIGN_CENTER, 0, 0);

    // Title bar
    reader_title = lv_label_create(tab_reader);
    lv_label_set_text(reader_title, "");
    lv_obj_set_size(reader_title, 308, 20);
    lv_obj_align(reader_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_font(reader_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(reader_title, lv_color_make(56, 189, 248), 0);
    lv_label_set_long_mode(reader_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_add_flag(reader_title, LV_OBJ_FLAG_HIDDEN);

    // Main text area
    reader_text = lv_label_create(tab_reader);
    lv_obj_set_size(reader_text, 308, 290);
    lv_obj_align(reader_text, LV_ALIGN_TOP_LEFT, 0, 24);
    lv_label_set_long_mode(reader_text, LV_LABEL_LONG_WRAP);
    lv_label_set_text(reader_text, "");
    lv_obj_set_style_text_font(reader_text, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(reader_text, lv_color_make(200, 210, 230), 0);
    lv_obj_set_style_bg_color(reader_text, lv_color_make(8, 12, 22), 0);
    lv_obj_set_style_pad_all(reader_text, 4, 0);
    lv_obj_add_flag(reader_text, LV_OBJ_FLAG_HIDDEN);

    // Progress bar
    reader_progress = lv_bar_create(tab_reader);
    lv_obj_set_size(reader_progress, 308, 4);
    lv_obj_align(reader_progress, LV_ALIGN_TOP_LEFT, 0, 318);
    lv_bar_set_range(reader_progress, 0, 100);
    lv_bar_set_value(reader_progress, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(reader_progress, lv_color_make(30, 41, 59), LV_PART_MAIN);
    lv_obj_set_style_bg_color(reader_progress, lv_color_make(56, 189, 248), LV_PART_INDICATOR);
    lv_obj_add_flag(reader_progress, LV_OBJ_FLAG_HIDDEN);

    // Jump slider
    reader_jump_slider = lv_slider_create(tab_reader);
    lv_obj_set_size(reader_jump_slider, 180, 10);
    lv_obj_align(reader_jump_slider, LV_ALIGN_TOP_LEFT, 0, 326);
    lv_slider_set_range(reader_jump_slider, 0, 100);
    lv_obj_set_style_bg_color(reader_jump_slider, lv_color_make(30, 41, 59), LV_PART_MAIN);
    lv_obj_set_style_bg_color(reader_jump_slider, lv_color_make(100, 116, 139), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(reader_jump_slider, lv_color_make(255, 255, 255), LV_PART_KNOB);
    lv_obj_add_event_cb(reader_jump_slider, on_reader_jump, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_flag(reader_jump_slider, LV_OBJ_FLAG_HIDDEN);

    // Page label
    reader_page_lbl = lv_label_create(tab_reader);
    lv_label_set_text(reader_page_lbl, "");
    lv_obj_align(reader_page_lbl, LV_ALIGN_TOP_RIGHT, 0, 326);
    lv_obj_set_style_text_font(reader_page_lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(reader_page_lbl, lv_color_make(100, 116, 139), 0);
    lv_obj_add_flag(reader_page_lbl, LV_OBJ_FLAG_HIDDEN);

    // Navigation row
    struct { const char* sym; lv_event_cb_t cb; lv_obj_t** out; } nav[] = {
        { LV_SYMBOL_PREV,      on_reader_prev,     &reader_prev_btn  },
        { LV_SYMBOL_BOOKMARK,  on_reader_bookmark, &reader_bm_btn    },
        { LV_SYMBOL_NEXT,      on_reader_next,     &reader_next_btn  },
        { "AI",                on_reader_ai,       &reader_ai_btn    },
        { LV_SYMBOL_IMAGE,     on_reader_theme,    &reader_theme_btn },
    };

    int bw = 56, bh = 30, bx = 0, by = 342;
    for (auto& n : nav) {
        lv_obj_t* btn = lv_btn_create(tab_reader);
        lv_obj_set_size(btn, bw, bh);
        lv_obj_set_pos(btn, bx, by);
        lv_obj_set_style_bg_color(btn, lv_color_make(15, 23, 42), 0);
        lv_obj_set_style_border_color(btn, lv_color_make(30, 41, 59), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, n.sym);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, n.cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
        if (n.out) *n.out = btn;
        bx += bw + 4;
    }
}

void reader_ui_refresh() {
    if (!tab_reader) return;

    if (!current_book) {
        lv_obj_clear_flag(reader_no_book, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(reader_title,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(reader_text,    LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(reader_progress,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(reader_page_lbl,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(reader_prev_btn,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(reader_next_btn,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(reader_bm_btn,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(reader_ai_btn,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(reader_theme_btn,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(reader_jump_slider,LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_add_flag(reader_no_book, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(reader_title,    LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(reader_text,     LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(reader_progress, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(reader_page_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(reader_prev_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(reader_next_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(reader_bm_btn,   LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(reader_ai_btn,   LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(reader_theme_btn,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(reader_jump_slider,LV_OBJ_FLAG_HIDDEN);

    // Apply theme
    apply_reader_theme();

    // Title
    lv_label_set_text(reader_title, current_book->title);

    // Text content
    lv_label_set_text(reader_text, page_buffer);
    lv_obj_set_style_text_font(reader_text, get_read_font(), 0);

    // Progress
    int pct = (total_pages > 1) ? (current_page * 100 / (total_pages - 1)) : 100;
    lv_bar_set_value(reader_progress, pct, LV_ANIM_ON);
    lv_slider_set_value(reader_jump_slider, pct, LV_ANIM_OFF);

    // Page label
    lv_label_set_text_fmt(reader_page_lbl, "%d/%d", current_page + 1, total_pages);
}

// ─────────────────────────────────────────────
//  AI Panel Tab
// ─────────────────────────────────────────────
static void on_ai_summarize(lv_event_t* e) {
    if (!current_book || strlen(page_buffer) == 0) {
        post_toast("Open a book first");
        return;
    }
    lv_label_set_text(ai_status_lbl, "Summarizing...");
    lv_textarea_set_text(ai_result_area, "Waiting for AI response...");
    ai_summarize(page_buffer, current_book->title, nullptr);
}

static void on_ai_explain(lv_event_t* e) {
    if (!current_book || strlen(page_buffer) == 0) {
        post_toast("Open a book first");
        return;
    }
    lv_label_set_text(ai_status_lbl, "Explaining...");
    lv_textarea_set_text(ai_result_area, "Waiting for AI response...");
    ai_explain(page_buffer, nullptr);
}

static void on_ai_mcq(lv_event_t* e) {
    if (!current_book || strlen(page_buffer) == 0) {
        post_toast("Open a book first");
        return;
    }
    lv_label_set_text(ai_status_lbl, "Generating MCQs...");
    lv_textarea_set_text(ai_result_area, "Generating questions...");
    ai_generate_mcq(page_buffer, current_book->title, nullptr);
}

static void on_ai_notes(lv_event_t* e) {
    if (!current_book || strlen(page_buffer) == 0) {
        post_toast("Open a book first");
        return;
    }
    lv_label_set_text(ai_status_lbl, "Extracting notes...");
    lv_textarea_set_text(ai_result_area, "Extracting key points...");
    ai_extract_notes(page_buffer, current_book->title, nullptr);
}

static void on_ai_chat_send(lv_event_t* e) {
    if (!current_book) { post_toast("Open a book first"); return; }
    const char* question = lv_textarea_get_text(ai_chat_ta);
    if (!question || strlen(question) == 0) { post_toast("Type a question"); return; }
    lv_label_set_text(ai_status_lbl, "AI Thinking...");
    lv_textarea_set_text(ai_result_area, "Processing question...");
    ai_chat(question, page_buffer, nullptr);
}

static void on_ai_save(lv_event_t* e) {
    if (!current_book) return;
    const char* result = lv_textarea_get_text(ai_result_area);
    if (!result || strlen(result) == 0) return;
    if (ai_save_notes_to_sd(current_book->title, result)) {
        post_toast("Notes saved to SD!");
    } else {
        post_toast("Save failed");
    }
}

void build_tab_ai(lv_obj_t* tv) {
    tab_ai = lv_tabview_add_tab(tv, "AI");
    lv_obj_set_style_pad_all(tab_ai, 4, 0);
    lv_obj_set_style_bg_color(tab_ai, lv_color_make(8, 12, 22), 0);

    lv_obj_t* title = lv_label_create(tab_ai);
    lv_label_set_text(title, "🤖  AI Reading Assistant");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_make(56, 189, 248), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    // Status label
    ai_status_lbl = lv_label_create(tab_ai);
    lv_label_set_text(ai_status_lbl, "Ready");
    lv_obj_set_style_text_font(ai_status_lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(ai_status_lbl, lv_color_make(34, 197, 94), 0);
    lv_obj_align(ai_status_lbl, LV_ALIGN_TOP_RIGHT, 0, 2);

    // AI action buttons row 1
    struct { const char* label; lv_event_cb_t cb; } btns1[] = {
        { "Summarize", on_ai_summarize },
        { "Explain",   on_ai_explain   },
    };
    int bx = 0;
    for (auto& b : btns1) {
        lv_obj_t* btn = lv_btn_create(tab_ai);
        lv_obj_set_size(btn, 148, 28);
        lv_obj_set_pos(btn, bx, 20);
        lv_obj_set_style_bg_color(btn, lv_color_make(37, 99, 235), 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, b.label);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, b.cb, LV_EVENT_CLICKED, nullptr);
        bx += 152;
    }

    // Row 2
    struct { const char* label; lv_event_cb_t cb; } btns2[] = {
        { "Gen MCQs",    on_ai_mcq   },
        { "Make Notes",  on_ai_notes },
    };
    bx = 0;
    for (auto& b : btns2) {
        lv_obj_t* btn = lv_btn_create(tab_ai);
        lv_obj_set_size(btn, 148, 28);
        lv_obj_set_pos(btn, bx, 52);
        lv_obj_set_style_bg_color(btn, lv_color_make(21, 128, 61), 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, b.label);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, b.cb, LV_EVENT_CLICKED, nullptr);
        bx += 152;
    }

    // Result textarea
    ai_result_area = lv_textarea_create(tab_ai);
    lv_obj_set_size(ai_result_area, 308, 160);
    lv_obj_set_pos(ai_result_area, 0, 84);
    lv_textarea_set_placeholder_text(ai_result_area, "AI response will appear here...");
    lv_obj_set_style_bg_color(ai_result_area, lv_color_make(15, 23, 42), 0);
    lv_obj_set_style_text_color(ai_result_area, lv_color_make(200, 210, 230), 0);
    lv_obj_set_style_border_color(ai_result_area, lv_color_make(30, 41, 59), 0);
    lv_obj_set_style_text_font(ai_result_area, &lv_font_montserrat_10, 0);

    // Save button
    ai_save_btn = lv_btn_create(tab_ai);
    lv_obj_set_size(ai_save_btn, 80, 24);
    lv_obj_set_pos(ai_save_btn, 228, 248);
    lv_obj_set_style_bg_color(ai_save_btn, lv_color_make(100, 16, 240), 0);
    lv_obj_set_style_radius(ai_save_btn, 4, 0);
    lv_obj_t* sv_lbl = lv_label_create(ai_save_btn);
    lv_label_set_text(sv_lbl, LV_SYMBOL_SAVE " Save");
    lv_obj_set_style_text_font(sv_lbl, &lv_font_montserrat_10, 0);
    lv_obj_center(sv_lbl);
    lv_obj_add_event_cb(ai_save_btn, on_ai_save, LV_EVENT_CLICKED, nullptr);

    // Chat input
    lv_obj_t* chat_lbl = lv_label_create(tab_ai);
    lv_label_set_text(chat_lbl, "Ask about current page:");
    lv_obj_set_style_text_font(chat_lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(chat_lbl, lv_color_make(100, 116, 139), 0);
    lv_obj_set_pos(chat_lbl, 0, 276);

    ai_chat_ta = lv_textarea_create(tab_ai);
    lv_obj_set_size(ai_chat_ta, 230, 30);
    lv_obj_set_pos(ai_chat_ta, 0, 290);
    lv_textarea_set_one_line(ai_chat_ta, true);
    lv_textarea_set_placeholder_text(ai_chat_ta, "Ask anything...");
    lv_obj_set_style_bg_color(ai_chat_ta, lv_color_make(15, 23, 42), 0);
    lv_obj_set_style_text_color(ai_chat_ta, lv_color_make(200, 210, 230), 0);
    lv_obj_set_style_border_color(ai_chat_ta, lv_color_make(56, 189, 248), 0);
    lv_obj_set_style_text_font(ai_chat_ta, &lv_font_montserrat_10, 0);

    lv_obj_t* send_btn = lv_btn_create(tab_ai);
    lv_obj_set_size(send_btn, 70, 30);
    lv_obj_set_pos(send_btn, 234, 290);
    lv_obj_set_style_bg_color(send_btn, lv_color_make(37, 99, 235), 0);
    lv_obj_set_style_radius(send_btn, 4, 0);
    lv_obj_t* send_lbl = lv_label_create(send_btn);
    lv_label_set_text(send_lbl, LV_SYMBOL_SEND " Ask");
    lv_obj_set_style_text_font(send_lbl, &lv_font_montserrat_10, 0);
    lv_obj_center(send_lbl);
    lv_obj_add_event_cb(send_btn, on_ai_chat_send, LV_EVENT_CLICKED, nullptr);
}

void ai_panel_show_result(bool ok, const char* result) {
    if (!ai_result_area || !ai_status_lbl) return;
    if (ok) {
        lv_label_set_text(ai_status_lbl, "Done");
        lv_obj_set_style_text_color(ai_status_lbl, lv_color_make(34, 197, 94), 0);
        lv_textarea_set_text(ai_result_area, result ? result : "");
    } else {
        lv_label_set_text(ai_status_lbl, "Error");
        lv_obj_set_style_text_color(ai_status_lbl, lv_color_make(239, 68, 68), 0);
        lv_textarea_set_text(ai_result_area, result ? result : "AI Error");
    }
}

// ─────────────────────────────────────────────
//  WiFi Manager Tab
// ─────────────────────────────────────────────
static void on_wifi_scan(lv_event_t* e) {
    post_toast("Scanning...");
    lv_list_clean(wifi_net_list);
    lv_obj_t* wait_lbl = lv_list_add_btn(wifi_net_list, LV_SYMBOL_REFRESH, "Scanning...");
    lv_obj_set_style_text_color(wait_lbl, lv_color_make(100, 116, 139), 0);

    // Scan runs synchronously here (we're on Core 1 in the UI callback)
    // For a real device, post to Core 0. For simplicity, do it inline.
    // The scan itself is fast (<500ms with short timeout).
    int n = wifi_scan_networks();
    wifi_scan_ui_refresh(n);
}

static void on_wifi_net_select(lv_event_t* e) {
    lv_obj_t* btn = lv_event_get_target(e);
    // Extract SSID from button label (child label)
    lv_obj_t* lbl = lv_obj_get_child(btn, 1); // index 1 = text label after icon
    if (!lbl) lbl = lv_obj_get_child(btn, 0);
    const char* txt = lv_label_get_text(lbl);
    if (!txt) return;

    // Strip signal prefix like "[Good] " from text
    const char* ssid_start = strchr(txt, ']');
    if (ssid_start) {
        ssid_start += 2; // skip "] "
    } else {
        ssid_start = txt;
    }
    strncpy(wifi_selected_ssid, ssid_start, sizeof(wifi_selected_ssid) - 1);

    lv_label_set_text_fmt(wifi_status_lbl, "Selected: %s", wifi_selected_ssid);
}

static void on_wifi_connect_btn(lv_event_t* e) {
    if (strlen(wifi_selected_ssid) == 0) {
        post_toast("Select a network first");
        return;
    }
    const char* pass = lv_textarea_get_text(wifi_pass_ta);
    lv_label_set_text(wifi_status_lbl, "Connecting...");
    post_toast("Connecting...");

    bool ok = wifi_connect(wifi_selected_ssid, pass ? pass : "");
    if (ok) {
        lv_label_set_text_fmt(wifi_status_lbl, "Connected: %s", wifi_selected_ssid);
        lv_label_set_text_fmt(wifi_ip_lbl, "IP: %s", WiFi.localIP().toString().c_str());
        lv_obj_clear_flag(wifi_ip_lbl, LV_OBJ_FLAG_HIDDEN);
        post_toast("WiFi Connected!");
    } else {
        lv_label_set_text(wifi_status_lbl, "Connection failed");
        post_toast("WiFi Failed");
    }
}

void build_tab_wifi(lv_obj_t* tv) {
    tab_wifi = lv_tabview_add_tab(tv, LV_SYMBOL_WIFI "  WiFi");
    lv_obj_set_style_pad_all(tab_wifi, 4, 0);
    lv_obj_set_style_bg_color(tab_wifi, lv_color_make(8, 12, 22), 0);

    lv_obj_t* title = lv_label_create(tab_wifi);
    lv_label_set_text(title, LV_SYMBOL_WIFI "  WiFi Manager");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_make(56, 189, 248), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    wifi_status_lbl = lv_label_create(tab_wifi);
    lv_label_set_text(wifi_status_lbl, wifi_sta_connected ? "Connected" : "Not connected");
    lv_obj_set_style_text_font(wifi_status_lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(wifi_status_lbl,
        wifi_sta_connected ? lv_color_make(34, 197, 94) : lv_color_make(239, 68, 68), 0);
    lv_obj_align_to(wifi_status_lbl, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

    wifi_ip_lbl = lv_label_create(tab_wifi);
    lv_label_set_text_fmt(wifi_ip_lbl, "IP: %s",
        wifi_sta_connected ? WiFi.localIP().toString().c_str() : "--");
    lv_obj_set_style_text_font(wifi_ip_lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(wifi_ip_lbl, lv_color_make(100, 116, 139), 0);
    lv_obj_align_to(wifi_ip_lbl, wifi_status_lbl, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);
    if (!wifi_sta_connected) lv_obj_add_flag(wifi_ip_lbl, LV_OBJ_FLAG_HIDDEN);

    // Scan button
    wifi_scan_btn = lv_btn_create(tab_wifi);
    lv_obj_set_size(wifi_scan_btn, 80, 24);
    lv_obj_align_to(wifi_scan_btn, wifi_ip_lbl, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
    lv_obj_set_style_bg_color(wifi_scan_btn, lv_color_make(37, 99, 235), 0);
    lv_obj_set_style_radius(wifi_scan_btn, 4, 0);
    lv_obj_t* scan_lbl = lv_label_create(wifi_scan_btn);
    lv_label_set_text(scan_lbl, LV_SYMBOL_REFRESH " Scan");
    lv_obj_set_style_text_font(scan_lbl, &lv_font_montserrat_10, 0);
    lv_obj_center(scan_lbl);
    lv_obj_add_event_cb(wifi_scan_btn, on_wifi_scan, LV_EVENT_CLICKED, nullptr);

    // Network list
    wifi_net_list = lv_list_create(tab_wifi);
    lv_obj_set_size(wifi_net_list, 308, 160);
    lv_obj_align_to(wifi_net_list, wifi_scan_btn, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
    lv_obj_set_style_bg_color(wifi_net_list, lv_color_make(10, 15, 30), 0);
    lv_obj_set_style_border_color(wifi_net_list, lv_color_make(30, 41, 59), 0);
    lv_obj_set_style_radius(wifi_net_list, 6, 0);
    lv_obj_set_style_pad_all(wifi_net_list, 4, 0);
    lv_obj_set_style_pad_row(wifi_net_list, 2, 0);

    lv_obj_t* scan_hint = lv_list_add_btn(wifi_net_list, LV_SYMBOL_WIFI, "Tap Scan to find networks");
    lv_obj_set_style_text_color(scan_hint, lv_color_make(100, 116, 139), 0);
    lv_obj_set_style_text_font(scan_hint, &lv_font_montserrat_10, 0);

    // Password field
    lv_obj_t* pass_lbl = lv_label_create(tab_wifi);
    lv_label_set_text(pass_lbl, "Password:");
    lv_obj_set_style_text_font(pass_lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(pass_lbl, lv_color_make(100, 116, 139), 0);
    lv_obj_align_to(pass_lbl, wifi_net_list, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

    wifi_pass_ta = lv_textarea_create(tab_wifi);
    lv_obj_set_size(wifi_pass_ta, 220, 28);
    lv_obj_align_to(wifi_pass_ta, pass_lbl, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);
    lv_textarea_set_password_mode(wifi_pass_ta, true);
    lv_textarea_set_one_line(wifi_pass_ta, true);
    lv_textarea_set_placeholder_text(wifi_pass_ta, "Enter password...");
    lv_obj_set_style_bg_color(wifi_pass_ta, lv_color_make(15, 23, 42), 0);
    lv_obj_set_style_text_color(wifi_pass_ta, lv_color_make(200, 210, 230), 0);
    lv_obj_set_style_border_color(wifi_pass_ta, lv_color_make(30, 41, 59), 0);
    lv_obj_set_style_text_font(wifi_pass_ta, &lv_font_montserrat_10, 0);

    wifi_conn_btn = lv_btn_create(tab_wifi);
    lv_obj_set_size(wifi_conn_btn, 82, 28);
    lv_obj_align_to(wifi_conn_btn, wifi_pass_ta, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
    lv_obj_set_style_bg_color(wifi_conn_btn, lv_color_make(21, 128, 61), 0);
    lv_obj_set_style_radius(wifi_conn_btn, 4, 0);
    lv_obj_t* conn_lbl = lv_label_create(wifi_conn_btn);
    lv_label_set_text(conn_lbl, LV_SYMBOL_OK " Connect");
    lv_obj_set_style_text_font(conn_lbl, &lv_font_montserrat_10, 0);
    lv_obj_center(conn_lbl);
    lv_obj_add_event_cb(wifi_conn_btn, on_wifi_connect_btn, LV_EVENT_CLICKED, nullptr);
}

void wifi_scan_ui_refresh(int count) {
    if (!wifi_net_list) return;
    lv_list_clean(wifi_net_list);

    if (count == 0) {
        lv_obj_t* b = lv_list_add_btn(wifi_net_list, LV_SYMBOL_CLOSE, "No networks found");
        lv_obj_set_style_text_color(b, lv_color_make(239, 68, 68), 0);
        lv_obj_set_style_text_font(b, &lv_font_montserrat_10, 0);
        return;
    }

    for (int i = 0; i < count; i++) {
        ScannedNetwork& n = scanned_networks[i];
        char entry[64];
        snprintf(entry, sizeof(entry), "[%s] %s%s",
                 wifi_rssi_label(n.rssi),
                 n.ssid,
                 n.encrypted ? " " LV_SYMBOL_LOCK : "");

        lv_obj_t* btn = lv_list_add_btn(wifi_net_list, LV_SYMBOL_WIFI, entry);
        lv_obj_set_style_bg_color(btn, lv_color_make(15, 23, 42), 0);
        lv_obj_set_style_bg_color(btn, lv_color_make(30, 41, 59), LV_STATE_PRESSED);
        lv_obj_set_style_text_color(btn, lv_color_make(200, 210, 230), 0);
        lv_obj_set_style_text_font(btn, &lv_font_montserrat_10, 0);
        lv_obj_set_style_min_height(btn, 28, 0);
        lv_obj_add_event_cb(btn, on_wifi_net_select, LV_EVENT_CLICKED, nullptr);
    }
}

// ─────────────────────────────────────────────
//  Stats Tab
// ─────────────────────────────────────────────
static const char* format_time(uint32_t sec) {
    static char buf[32];
    if (sec < 60)       snprintf(buf, sizeof(buf), "%us", sec);
    else if (sec < 3600) snprintf(buf, sizeof(buf), "%um", sec / 60);
    else                snprintf(buf, sizeof(buf), "%uh %um", sec / 3600, (sec % 3600) / 60);
    return buf;
}

void build_tab_stats(lv_obj_t* tv) {
    lv_obj_t* tab = lv_tabview_add_tab(tv, LV_SYMBOL_LIST "  Stats");
    tab_stats = tab;
    lv_obj_set_style_pad_all(tab, 6, 0);
    lv_obj_set_style_bg_color(tab, lv_color_make(8, 12, 22), 0);

    lv_obj_t* title = lv_label_create(tab);
    lv_label_set_text(title, "📊  Reading Statistics");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_make(56, 189, 248), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    // Arc for pages read
    stats_arc = lv_arc_create(tab);
    lv_obj_set_size(stats_arc, 110, 110);
    lv_arc_set_rotation(stats_arc, 135);
    lv_arc_set_bg_angles(stats_arc, 0, 270);
    lv_arc_set_range(stats_arc, 0, 1000);
    lv_arc_set_value(stats_arc, 0);
    lv_obj_set_style_arc_color(stats_arc, lv_color_make(56, 189, 248), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(stats_arc, lv_color_make(30, 41, 59), LV_PART_MAIN);
    lv_obj_set_style_arc_width(stats_arc, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(stats_arc, 12, LV_PART_MAIN);
    lv_arc_set_mode(stats_arc, LV_ARC_MODE_NORMAL);
    lv_obj_remove_style(stats_arc, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(stats_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(stats_arc, LV_ALIGN_TOP_RIGHT, 0, 20);

    // Stat labels
    struct { lv_obj_t** out; const char* init; } rows[] = {
        { &stats_pages_lbl,  "Pages Read:        --"   },
        { &stats_time_lbl,   "Reading Time:      --"   },
        { &stats_books_lbl,  "Books Completed:   --"   },
        { &stats_streak_lbl, "Session Count:     --"   },
    };

    int y = 28;
    for (auto& r : rows) {
        *r.out = lv_label_create(tab);
        lv_label_set_text(*r.out, r.init);
        lv_obj_set_style_text_font(*r.out, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(*r.out, lv_color_make(148, 163, 184), 0);
        lv_obj_set_pos(*r.out, 0, y);
        y += 22;
    }

    // Currently reading info
    lv_obj_t* cr_lbl = lv_label_create(tab);
    lv_label_set_text(cr_lbl, "Currently Reading:");
    lv_obj_set_style_text_font(cr_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(cr_lbl, lv_color_make(56, 189, 248), 0);
    lv_obj_set_pos(cr_lbl, 0, 130);

    lv_obj_t* cr_book = lv_label_create(tab);
    lv_label_set_text(cr_book,
        current_book ? current_book->title : "No book open");
    lv_obj_set_style_text_font(cr_book, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(cr_book, lv_color_make(200, 210, 230), 0);
    lv_obj_set_pos(cr_book, 0, 148);

    // Library count
    char lib_str[48];
    snprintf(lib_str, sizeof(lib_str), "Library: %d book(s) on SD", book_count);
    lv_obj_t* lib_lbl = lv_label_create(tab);
    lv_label_set_text(lib_lbl, lib_str);
    lv_obj_set_style_text_font(lib_lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lib_lbl, lv_color_make(100, 116, 139), 0);
    lv_obj_set_pos(lib_lbl, 0, 166);

    // AI config section
    lv_obj_t* ai_sec = lv_label_create(tab);
    lv_label_set_text(ai_sec, "─── AI Settings ───────────────");
    lv_obj_set_style_text_font(ai_sec, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(ai_sec, lv_color_make(56, 189, 248), 0);
    lv_obj_set_pos(ai_sec, 0, 190);

    lv_obj_t* backend_lbl = lv_label_create(tab);
    lv_label_set_text(backend_lbl, ai_backend == AI_BACKEND_GEMINI
                                    ? "Backend: Google Gemini" : "Backend: OpenAI");
    lv_obj_set_style_text_font(backend_lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(backend_lbl, lv_color_make(148, 163, 184), 0);
    lv_obj_set_pos(backend_lbl, 0, 204);

    // Toggle backend button
    lv_obj_t* toggle_btn = lv_btn_create(tab);
    lv_obj_set_size(toggle_btn, 150, 24);
    lv_obj_set_pos(toggle_btn, 0, 220);
    lv_obj_set_style_bg_color(toggle_btn, lv_color_make(100, 16, 240), 0);
    lv_obj_set_style_radius(toggle_btn, 4, 0);
    lv_obj_t* tb_lbl = lv_label_create(toggle_btn);
    lv_label_set_text(tb_lbl, "Toggle AI Backend");
    lv_obj_set_style_text_font(tb_lbl, &lv_font_montserrat_10, 0);
    lv_obj_center(tb_lbl);
    lv_obj_add_event_cb(toggle_btn, [](lv_event_t* e) {
        AIBackend next = (ai_backend == AI_BACKEND_GEMINI)
                          ? AI_BACKEND_OPENAI : AI_BACKEND_GEMINI;
        ai_set_backend(next);
        post_toast(next == AI_BACKEND_GEMINI ? "Using Gemini" : "Using OpenAI");
    }, LV_EVENT_CLICKED, nullptr);

    book_stats_load();
    stats_ui_refresh();
}

void stats_ui_refresh() {
    if (!stats_pages_lbl) return;
    lv_label_set_text_fmt(stats_pages_lbl,  "Pages Read:       %lu",   reading_stats.total_pages_read);
    lv_label_set_text_fmt(stats_time_lbl,   "Reading Time:     %s",    format_time(reading_stats.total_reading_seconds));
    lv_label_set_text_fmt(stats_books_lbl,  "Books Completed:  %lu",   reading_stats.books_completed);
    lv_label_set_text_fmt(stats_streak_lbl, "Session Count:    %lu",   reading_stats.last_read_timestamp);

    if (stats_arc) {
        int arc_val = (int)min(reading_stats.total_pages_read, (uint32_t)1000);
        lv_arc_set_value(stats_arc, arc_val);
    }
}

// ─────────────────────────────────────────────
//  Post helpers for new events
// ─────────────────────────────────────────────
void post_wifi_scan_done(int count) {
    UIEventMsg msg = {};
    msg.type = UI_EVT_WIFI_SCAN_DONE;
    msg.val1 = count;
    xQueueSend(ui_event_queue, &msg, 0);
}

void post_wifi_connected(bool ok, const char* ssid) {
    UIEventMsg msg = {};
    msg.type = UI_EVT_WIFI_CONNECTED;
    msg.val1 = ok ? 1 : 0;
    if (ssid) strncpy(msg.str, ssid, sizeof(msg.str) - 1);
    xQueueSend(ui_event_queue, &msg, 0);
}

void post_ai_result(bool ok) {
    UIEventMsg msg = {};
    msg.type = UI_EVT_AI_RESULT_READY;
    msg.val1 = ok ? 1 : 0;
    xQueueSend(ui_event_queue, &msg, 0);
}

void post_book_page(int page, int total) {
    UIEventMsg msg = {};
    msg.type = UI_EVT_BOOK_PAGE_LOADED;
    msg.val1 = page;
    msg.val2 = total;
    xQueueSend(ui_event_queue, &msg, 0);
}

// ─────────────────────────────────────────────
//  Main init hook — called from ui_engine.cpp
//  after the base tabview is created
// ─────────────────────────────────────────────
void init_reader_tabs(lv_obj_t* tv) {
    build_tab_library(tv);
    build_tab_reader(tv);
    build_tab_ai(tv);
    build_tab_wifi(tv);
    build_tab_stats(tv);
}