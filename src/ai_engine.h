#ifndef AI_ENGINE_H
#define AI_ENGINE_H

#include <Arduino.h>
#include <functional>

// ─────────────────────────────────────────────
//  AI Feature Modes
// ─────────────────────────────────────────────
enum class AIMode {
    SUMMARIZE,        // Summarize selected text / current page
    EXPLAIN,          // Explain selection in simpler terms
    VOCABULARY,       // Define a single tapped word
    STUDY_MCQ,        // Generate multiple-choice questions
    STUDY_SHORT_Q,    // Generate short-answer questions
    STUDY_VIVA,       // Generate viva/oral exam questions
    NOTE_EXTRACT,     // Extract key notes from chapter
    CHAT_WITH_BOOK,   // Free-form Q&A grounded in book context
    OCR_CLEANUP,      // Clean up raw OCR text into readable prose
};

// Async callback: called once the AI response arrives (or on error).
// success=false → result contains a human-readable error message.
using AICallback = std::function<void(bool success, const String& result)>;

// ─────────────────────────────────────────────
//  Configuration (set once, e.g. from Settings tab)
// ─────────────────────────────────────────────
// Provider selection — only one needs a key configured.
enum class AIProvider { GEMINI, OPENAI };

void ai_configure(AIProvider provider, const String& api_key);
bool ai_is_configured();
AIProvider ai_get_provider();

// ─────────────────────────────────────────────
//  Core request function
//  context   = book text relevant to the request (page/chapter/selection)
//  user_text = the word to define, the question to ask, etc. (mode-dependent)
//  Runs the HTTPS request on the calling task — call from the network
//  core (Core 0), never from the LVGL/UI core, since it blocks on I/O.
// ─────────────────────────────────────────────
void ai_request(AIMode mode, const String& context, const String& user_text,
                 AICallback callback);

// Convenience wrappers used by the UI layer — these just build the
// right prompt and call ai_request() under the hood.
void ai_summarize_page(const String& page_text, AICallback cb);
void ai_explain_selection(const String& selection, const String& surrounding_context, AICallback cb);
void ai_define_word(const String& word, const String& sentence_context, AICallback cb);
void ai_generate_mcqs(const String& chapter_text, int count, AICallback cb);
void ai_generate_short_questions(const String& chapter_text, int count, AICallback cb);
void ai_generate_viva_questions(const String& chapter_text, int count, AICallback cb);
void ai_extract_notes(const String& chapter_text, AICallback cb);
void ai_chat_with_book(const String& book_context, const String& chat_history,
                        const String& new_question, AICallback cb);
void ai_cleanup_ocr_text(const String& raw_ocr_text, AICallback cb);

#endif // AI_ENGINE_H