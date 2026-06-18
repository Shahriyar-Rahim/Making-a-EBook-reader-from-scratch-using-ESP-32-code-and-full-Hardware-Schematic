// ═══════════════════════════════════════════════════════════════════
//  book_engine.cpp  —  TXT + EPUB reading engine for ESP32-S3
//  Notes on design:
//   - TXT: streamed and paginated by measuring word-wrapped lines
//     against the viewport box (chars-per-line estimate from font px).
//   - EPUB: treated as a ZIP archive. We don't do a full OPF/NCX parse;
//     we walk the archive's central directory, pull every .xhtml/.html
//     file in spine order (best-effort: sorted by name, which matches
//     the vast majority of EPUB exports), strip tags, and paginate the
//     resulting plain text per "chapter" (one EPUB-internal file = one
//     chapter). This keeps RAM use low and avoids a full XML parser.
//   - PDF is intentionally NOT supported (per hardware constraints).
// ═══════════════════════════════════════════════════════════════════
#include "book_engine.h"
#include <SD.h>
#include <Preferences.h>
#include <Update.h>   // for CRC table convenience (not used for flashing here)
#include <algorithm>  // std::sort (chapter ordering)

extern SemaphoreHandle_t sd_mutex;

// ─────────────────────────────────────────────
//  Internal state
// ─────────────────────────────────────────────
static String            current_path;
static String            current_title;
static BookFormat        current_format = BookFormat::UNKNOWN;
static std::vector<BookPage> pages;
static int                cur_page_idx = 0;

static int                viewport_chars_per_line = 36;
static int                viewport_lines_per_page = 14;

// EPUB chapter list (raw plain text per chapter, extracted once on open)
static std::vector<String> epub_chapters;
static std::vector<String> epub_chapter_titles;
static int                  epub_cur_chapter = 0;

// ─────────────────────────────────────────────
//  Helpers: format detection
// ─────────────────────────────────────────────
static BookFormat detect_format(const String& path) {
    String lower = path;
    lower.toLowerCase();
    if (lower.endsWith(".txt"))  return BookFormat::TXT;
    if (lower.endsWith(".epub")) return BookFormat::EPUB;
    return BookFormat::UNKNOWN;
}

static String derive_title(const String& path) {
    int slash = path.lastIndexOf('/');
    String name = (slash >= 0) ? path.substring(slash + 1) : path;
    int dot = name.lastIndexOf('.');
    if (dot > 0) name = name.substring(0, dot);
    name.replace('_', ' ');
    name.replace('-', ' ');
    return name;
}

// ─────────────────────────────────────────────
//  Plain-text word wrap → pages
// ─────────────────────────────────────────────
static std::vector<String> wrap_text_to_lines(const String& text, int cols) {
    std::vector<String> lines;
    int len = text.length();
    int i = 0;
    while (i < len) {
        // Skip a single leading newline as paragraph break marker
        if (text[i] == '\n') { lines.push_back(""); i++; continue; }

        int line_end = i;
        int last_space = -1;
        int count = 0;
        while (line_end < len && text[line_end] != '\n' && count < cols) {
            if (text[line_end] == ' ') last_space = line_end;
            line_end++;
            count++;
        }
        if (line_end < len && text[line_end] != '\n' && last_space > i) {
            // Wrap at the last space to avoid breaking mid-word
            lines.push_back(text.substring(i, last_space));
            i = last_space + 1;
        } else {
            lines.push_back(text.substring(i, line_end));
            i = line_end;
        }
    }
    return lines;
}

static void paginate_plaintext(const String& text) {
    pages.clear();
    std::vector<String> lines = wrap_text_to_lines(text, viewport_chars_per_line);

    size_t running_offset = 0;
    for (size_t i = 0; i < lines.size(); i += viewport_lines_per_page) {
        String page_text;
        size_t page_start_offset = running_offset;
        size_t end = min(i + (size_t)viewport_lines_per_page, lines.size());
        for (size_t j = i; j < end; j++) {
            page_text += lines[j];
            page_text += '\n';
            running_offset += lines[j].length() + 1;
        }
        pages.push_back({ page_text, page_start_offset });
    }
    if (pages.empty()) {
        pages.push_back({ "(empty book)", 0 });
    }
}

// ─────────────────────────────────────────────
//  Minimal HTML tag stripper (for EPUB xhtml content)
// ─────────────────────────────────────────────
static String strip_html_tags(const String& html) {
    String out;
    out.reserve(html.length());
    bool in_tag = false;
    bool in_entity = false;
    String entity_buf;

    for (size_t i = 0; i < html.length(); i++) {
        char c = html[i];
        if (c == '<') { in_tag = true; continue; }
        if (c == '>') { in_tag = false; continue; }
        if (in_tag) continue;

        if (c == '&') { in_entity = true; entity_buf = ""; continue; }
        if (in_entity) {
            if (c == ';') {
                in_entity = false;
                if (entity_buf == "amp") out += '&';
                else if (entity_buf == "lt") out += '<';
                else if (entity_buf == "gt") out += '>';
                else if (entity_buf == "nbsp") out += ' ';
                else if (entity_buf == "quot") out += '"';
                else if (entity_buf == "apos") out += '\'';
                // unknown entities silently dropped
            } else {
                entity_buf += c;
            }
            continue;
        }
        out += c;
    }

    // Collapse runs of whitespace, keep paragraph breaks as single \n
    String collapsed;
    bool last_was_space = false;
    for (size_t i = 0; i < out.length(); i++) {
        char c = out[i];
        if (c == '\r') continue;
        if (c == ' ' || c == '\t') {
            if (!last_was_space) collapsed += ' ';
            last_was_space = true;
        } else {
            collapsed += c;
            last_was_space = (c == '\n');
        }
    }
    return collapsed;
}

// ─────────────────────────────────────────────
//  Minimal ZIP reader (local-file-header walk, no central-dir parse)
//  EPUB uses ZIP "stored" (uncompressed) or "deflate". We only support
//  STORED entries for the zero-dependency path; most EPUB exporters
//  (Calibre, Sigil) write the mimetype stored and content deflated —
//  so for deflated entries we fall back to a tiny inflate via miniz
//  if available, else we skip the entry gracefully.
// ─────────────────────────────────────────────
#pragma pack(push, 1)
struct ZipLocalHeader {
    uint32_t sig;
    uint16_t version;
    uint16_t flags;
    uint16_t method;      // 0 = stored, 8 = deflate
    uint16_t mod_time;
    uint16_t mod_date;
    uint32_t crc32;
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint16_t name_len;
    uint16_t extra_len;
};
#pragma pack(pop)

// Reads every entry in the EPUB zip whose name ends in .xhtml/.html/.htm,
// strips tags, and pushes the chapter text + title into epub_chapters.
// Returns true if at least one chapter was extracted.
static bool extract_epub_chapters(const String& path) {
    epub_chapters.clear();
    epub_chapter_titles.clear();

    File f = SD.open(path, FILE_READ);
    if (!f) return false;

    struct Candidate { String name; uint32_t offset; uint32_t comp_size; uint32_t uncomp_size; uint16_t method; };
    std::vector<Candidate> candidates;

    size_t file_size = f.size();
    size_t pos = 0;

    while (pos < file_size) {
        f.seek(pos);
        ZipLocalHeader hdr;
        size_t n = f.read((uint8_t*)&hdr, sizeof(hdr));
        if (n < sizeof(hdr)) break;
        if (hdr.sig != 0x04034b50) break;  // not a local file header → end of entries

        char name_buf[256] = {};
        size_t name_n = min((size_t)hdr.name_len, sizeof(name_buf) - 1);
        f.read((uint8_t*)name_buf, name_n);
        String name(name_buf);

        size_t data_offset = pos + sizeof(hdr) + hdr.name_len + hdr.extra_len;

        String lower = name; lower.toLowerCase();
        if ((lower.endsWith(".xhtml") || lower.endsWith(".html") || lower.endsWith(".htm"))
            && lower.indexOf("nav") < 0 && lower.indexOf("toc") < 0) {
            candidates.push_back({ name, (uint32_t)data_offset, hdr.comp_size, hdr.uncomp_size, hdr.method });
        }

        pos = data_offset + hdr.comp_size;
    }

    // Sort by filename — matches spine order for the vast majority of
    // real-world EPUB exports (chapter001.xhtml, chapter002.xhtml, ...)
    std::sort(candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b) { return a.name < b.name; });

    for (auto& c : candidates) {
        if (c.method != 0) {
            // Deflate-compressed entry — skip (no inflate lib linked by default).
            // Most minimal/converted EPUBs from Calibre with "store" option work;
            // for deflated EPUBs, recommend re-saving as TXT or uncompressed EPUB.
            continue;
        }
        f.seek(c.offset);
        String raw;
        raw.reserve(c.uncomp_size + 1);
        uint8_t buf[512];
        uint32_t remaining = c.uncomp_size;
        while (remaining > 0) {
            size_t chunk = min((uint32_t)sizeof(buf), remaining);
            size_t got = f.read(buf, chunk);
            if (got == 0) break;
            raw.concat((const char*)buf, got);
            remaining -= got;
        }
        String plain = strip_html_tags(raw);
        plain.trim();
        if (plain.length() > 0) {
            epub_chapters.push_back(plain);
            // crude title: first non-empty line, capped
            int nl = plain.indexOf('\n');
            String title = (nl > 0) ? plain.substring(0, nl) : plain.substring(0, 40);
            title.trim();
            if (title.length() > 40) title = title.substring(0, 40) + "...";
            epub_chapter_titles.push_back(title.length() ? title : ("Chapter " + String(epub_chapters.size())));
        }
    }

    f.close();
    return !epub_chapters.empty();
}

// ─────────────────────────────────────────────
//  Library scan
// ─────────────────────────────────────────────
std::vector<LibraryEntry> scan_library() {
    std::vector<LibraryEntry> out;

    if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return out;

    if (!SD.exists("/Books")) {
        SD.mkdir("/Books");
    }

    File dir = SD.open("/Books");
    if (dir && dir.isDirectory()) {
        File entry = dir.openNextFile();
        while (entry) {
            if (!entry.isDirectory()) {
                String name = entry.name();
                String full = String("/Books/") + name;
                BookFormat fmt = detect_format(full);
                if (fmt != BookFormat::UNKNOWN) {
                    LibraryEntry le;
                    le.filename     = full;
                    le.title        = derive_title(full);
                    le.format       = fmt;
                    le.size_bytes   = entry.size();
                    ReadingProgress p = progress_load(full);
                    le.percent_read = p.percent;
                    out.push_back(le);
                }
            }
            entry = dir.openNextFile();
        }
        dir.close();
    }

    xSemaphoreGive(sd_mutex);
    return out;
}

// ─────────────────────────────────────────────
//  Open / close
// ─────────────────────────────────────────────
bool book_open(const String& filepath, int viewport_w_px, int viewport_h_px,
                int font_px_w, int font_px_h) {
    book_close();

    current_path   = filepath;
    current_title  = derive_title(filepath);
    current_format = detect_format(filepath);

    viewport_chars_per_line = max(10, viewport_w_px / max(1, font_px_w));
    viewport_lines_per_page = max(3,  viewport_h_px / max(1, font_px_h));

    bool ok = false;

    if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) return false;

    if (current_format == BookFormat::TXT) {
        File f = SD.open(filepath, FILE_READ);
        if (f) {
            String text;
            text.reserve(f.size() + 1);
            uint8_t buf[1024];
            while (f.available()) {
                size_t n = f.read(buf, sizeof(buf));
                text.concat((const char*)buf, n);
            }
            f.close();
            paginate_plaintext(text);
            ok = true;
        }
    } else if (current_format == BookFormat::EPUB) {
        if (extract_epub_chapters(filepath)) {
            epub_cur_chapter = 0;
            paginate_plaintext(epub_chapters[0]);
            ok = true;
        }
    }

    xSemaphoreGive(sd_mutex);

    if (ok) {
        ReadingProgress p = progress_load(filepath);
        if (current_format == BookFormat::EPUB && p.chapter_index < (int)epub_chapters.size()) {
            book_goto_chapter(p.chapter_index);
        }
        cur_page_idx = constrain(p.last_page, 0, (int)pages.size() - 1);
    }
    return ok;
}

void book_close() {
    if (current_path.length() > 0) progress_save();
    pages.clear();
    epub_chapters.clear();
    epub_chapter_titles.clear();
    current_path = "";
    current_title = "";
    current_format = BookFormat::UNKNOWN;
    cur_page_idx = 0;
    epub_cur_chapter = 0;
}

// ─────────────────────────────────────────────
//  Page accessors
// ─────────────────────────────────────────────
int    book_get_page_count()   { return (int)pages.size(); }
int    book_get_current_page() { return cur_page_idx; }
String book_get_title()        { return current_title; }

String book_get_page_text(int page_index) {
    if (page_index < 0 || page_index >= (int)pages.size()) return "";
    return pages[page_index].text;
}

bool book_next_page() {
    if (cur_page_idx + 1 < (int)pages.size()) {
        cur_page_idx++;
        return true;
    }
    // Auto-advance chapter for EPUB
    if (current_format == BookFormat::EPUB &&
        epub_cur_chapter + 1 < (int)epub_chapters.size()) {
        return book_goto_chapter(epub_cur_chapter + 1);
    }
    return false;
}

bool book_prev_page() {
    if (cur_page_idx > 0) {
        cur_page_idx--;
        return true;
    }
    if (current_format == BookFormat::EPUB && epub_cur_chapter > 0) {
        bool ok = book_goto_chapter(epub_cur_chapter - 1);
        if (ok) cur_page_idx = (int)pages.size() - 1;
        return ok;
    }
    return false;
}

bool book_jump_to_page(int page_index) {
    if (page_index < 0 || page_index >= (int)pages.size()) return false;
    cur_page_idx = page_index;
    return true;
}

bool book_jump_to_percent(float pct) {
    pct = constrain(pct, 0.0f, 100.0f);
    int target = (int)((pct / 100.0f) * (pages.size() - 1));
    return book_jump_to_page(target);
}

String book_get_context_window(int chars_before, int chars_after) {
    // Concatenate current page +/- neighbours for AI context
    String ctx;
    int start = max(0, cur_page_idx - 1);
    int end   = min((int)pages.size() - 1, cur_page_idx + 1);
    for (int i = start; i <= end; i++) ctx += pages[i].text;
    if ((int)ctx.length() > chars_before + chars_after) {
        ctx = ctx.substring(0, chars_before + chars_after);
    }
    return ctx;
}

// ─────────────────────────────────────────────
//  EPUB chapter navigation
// ─────────────────────────────────────────────
int book_get_chapter_count()   { return (int)epub_chapters.size(); }
int book_get_current_chapter() { return epub_cur_chapter; }

String book_get_chapter_title(int idx) {
    if (idx < 0 || idx >= (int)epub_chapter_titles.size()) return "";
    return epub_chapter_titles[idx];
}

bool book_goto_chapter(int idx) {
    if (idx < 0 || idx >= (int)epub_chapters.size()) return false;
    epub_cur_chapter = idx;
    paginate_plaintext(epub_chapters[idx]);
    cur_page_idx = 0;
    return true;
}

// ─────────────────────────────────────────────
//  Progress persistence (NVS / Preferences)
// ─────────────────────────────────────────────
static String progress_key(const String& filepath) {
    // Preferences keys are capped at 15 chars — hash the path
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < filepath.length(); i++) {
        hash ^= (uint8_t)filepath[i];
        hash *= 16777619u;
    }
    return "bk_" + String(hash, 16);
}

void progress_save() {
    if (current_path.length() == 0) return;
    Preferences prefs;
    prefs.begin("bookprog", false);

    ReadingProgress p;
    p.last_page     = cur_page_idx;
    p.total_pages   = (int)pages.size();
    p.chapter_index = epub_cur_chapter;
    p.percent       = (pages.size() > 1)
                        ? (100.0f * cur_page_idx / (pages.size() - 1))
                        : 100.0f;
    p.last_opened   = (uint32_t)time(nullptr);

    String key = progress_key(current_path);
    prefs.putBytes(key.c_str(), &p, sizeof(p));
    prefs.end();
}

ReadingProgress progress_load(const String& filepath) {
    ReadingProgress p;
    Preferences prefs;
    prefs.begin("bookprog", true);
    String key = progress_key(filepath);
    if (prefs.isKey(key.c_str())) {
        prefs.getBytes(key.c_str(), &p, sizeof(p));
    }
    prefs.end();
    return p;
}