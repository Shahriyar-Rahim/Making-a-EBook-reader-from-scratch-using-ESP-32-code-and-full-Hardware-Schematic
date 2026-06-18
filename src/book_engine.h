#ifndef BOOK_ENGINE_H
#define BOOK_ENGINE_H

#include <Arduino.h>
#include <vector>

// ─────────────────────────────────────────────
//  Supported formats
// ─────────────────────────────────────────────
enum class BookFormat {
    TXT,
    EPUB,
    UNKNOWN
};

// One paginated screen of text, pre-wrapped to the reader viewport
struct BookPage {
    String text;
    size_t byte_offset;   // offset into the source chapter/file this page starts at
};

// Per-book reading state, persisted to NVS keyed by a hash of the filename
struct ReadingProgress {
    int    last_page      = 0;
    int    total_pages    = 0;
    int    chapter_index  = 0;   // for EPUB
    float  percent        = 0.0f;
    uint32_t seconds_read  = 0;
    uint32_t last_opened   = 0;  // unix timestamp
};

// Library entry shown in the file browser
struct LibraryEntry {
    String     filename;     // full path on SD, e.g. /Books/atomic_habits.txt
    String     title;        // derived display title
    BookFormat format;
    size_t     size_bytes;
    float      percent_read;
};

// ─────────────────────────────────────────────
//  Public API
// ─────────────────────────────────────────────

// Scans /Books on the SD card and populates the library list.
// Safe to call repeatedly (e.g. on tab-open) — it rescans each time.
std::vector<LibraryEntry> scan_library();

// Opens a book, detects format, loads + paginates it for the given
// viewport size (chars-per-line / lines-per-page are computed from
// font metrics by the caller and passed in here).
// Returns true on success. Call get_page_count() / get_page() after.
bool book_open(const String& filepath, int viewport_w_px, int viewport_h_px,
                int font_px_w, int font_px_h);

void book_close();

int         book_get_page_count();
int         book_get_current_page();
String      book_get_title();
String      book_get_page_text(int page_index);   // bounds-checked

bool        book_next_page();
bool        book_prev_page();
bool        book_jump_to_page(int page_index);
bool        book_jump_to_percent(float pct);

// Returns the raw plain-text of the current chapter (EPUB) or whole
// file (TXT) around the current page — used as context for AI features.
String      book_get_context_window(int chars_before, int chars_after);

// Persisted progress (Preferences-backed)
void              progress_save();
ReadingProgress    progress_load(const String& filepath);

// EPUB-specific: chapter navigation
int     book_get_chapter_count();
int     book_get_current_chapter();
bool    book_goto_chapter(int idx);
String  book_get_chapter_title(int idx);

#endif // BOOK_ENGINE_H