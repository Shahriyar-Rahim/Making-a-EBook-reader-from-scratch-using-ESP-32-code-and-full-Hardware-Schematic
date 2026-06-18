#ifndef READER_UI_H
#define READER_UI_H

#include <lvgl.h>
#include "ui_engine.h"

// ─────────────────────────────────────────────
//  Called once from init_ui_engine() (in ui_engine.cpp) AFTER the
//  tabview has been created, to attach the additional tabs this
//  module owns: Library, Reader, AI Study, Camera, Settings.
//  Keeping these in a separate translation unit keeps ui_engine.cpp
//  focused on the original draw/notes/system scope and avoids one
//  giant file.
// ─────────────────────────────────────────────
void reader_ui_init(lv_obj_t* parent_tabview);

// Called from process_ui_events() in ui_engine.cpp when a
// UI_EVT_AI_RESULT / UI_EVT_OCR_RESULT / UI_EVT_LIBRARY_REFRESH
// event is drained from the queue.
void reader_ui_handle_ai_result(bool success, const String& text);
void reader_ui_handle_library_refresh();

// Switches the active screen to the Reader tab and loads a book —
// called from the Library tab's "open" tap handler.
void reader_ui_open_book(const String& filepath);

// Used by main.cpp's network task tick to refresh the clock label
// without pulling LVGL internals into network.cpp.
void reader_ui_update_clock(int hour, int minute);

#endif // READER_UI_H