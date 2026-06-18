// ═══════════════════════════════════════════════════════════════════
//  ai_engine.cpp  —  Cloud AI integration (Gemini / OpenAI) for the
//  Workbench Reader. All calls are synchronous HTTPS requests intended
//  to run on Core 0 (the network task) — never block the LVGL core.
// ═══════════════════════════════════════════════════════════════════
#include "ai_engine.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

static AIProvider current_provider = AIProvider::GEMINI;
static String     current_api_key  = "";

// Gemini endpoint (flash model — fast + cheap, fits the use case)
static const char* GEMINI_URL =
    "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent";
static const char* OPENAI_URL = "https://api.openai.com/v1/chat/completions";

// ─────────────────────────────────────────────
//  Config
// ─────────────────────────────────────────────
void ai_configure(AIProvider provider, const String& api_key) {
    current_provider = provider;
    current_api_key  = api_key;

    Preferences prefs;
    prefs.begin("ai_cfg", false);
    prefs.putInt("provider", (int)provider);
    prefs.putString("api_key", api_key);
    prefs.end();
}

bool ai_is_configured() {
    if (current_api_key.length() == 0) {
        // Lazy-load from NVS on first check
        Preferences prefs;
        prefs.begin("ai_cfg", true);
        current_provider = (AIProvider)prefs.getInt("provider", (int)AIProvider::GEMINI);
        current_api_key  = prefs.getString("api_key", "");
        prefs.end();
    }
    return current_api_key.length() > 0;
}

AIProvider ai_get_provider() { return current_provider; }

// ─────────────────────────────────────────────
//  Truncation guard — keep request payloads small for ESP32 RAM/heap
//  and to control token cost. ~6000 chars is a safe context budget.
// ─────────────────────────────────────────────
static String truncate_context(const String& text, size_t max_chars = 6000) {
    if (text.length() <= max_chars) return text;
    return text.substring(0, max_chars) + "\n[...truncated...]";
}

// Escape a string for safe embedding inside a JSON string value
static String json_escape(const String& s) {
    String out;
    out.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

// ─────────────────────────────────────────────
//  Low-level HTTPS call — Gemini
// ─────────────────────────────────────────────
static bool call_gemini(const String& prompt, String& out_response, String& out_error) {
    if (WiFi.status() != WL_CONNECTED && WiFi.getMode() != WIFI_AP) {
        out_error = "No internet connection (device is in AP-only mode — "
                    "connect the ESP32 to a Wi-Fi network with internet "
                    "access via Settings > Wi-Fi to use AI features).";
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();  // Note: for production, pin Google's root CA instead.

    HTTPClient https;
    String url = String(GEMINI_URL) + "?key=" + current_api_key;

    if (!https.begin(client, url)) {
        out_error = "Failed to initialize HTTPS connection.";
        return false;
    }
    https.addHeader("Content-Type", "application/json");
    https.setTimeout(20000);

    String body = String("{\"contents\":[{\"parts\":[{\"text\":\"") +
                  json_escape(prompt) + "\"}]}],"
                  "\"generationConfig\":{\"temperature\":0.4,\"maxOutputTokens\":800}}";

    int code = https.POST(body);
    if (code != 200) {
        out_error = "Gemini API error (HTTP " + String(code) + "): " + https.getString();
        https.end();
        return false;
    }

    String resp = https.getString();
    https.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, resp);
    if (err) {
        out_error = "Failed to parse AI response JSON.";
        return false;
    }

    const char* text = doc["candidates"][0]["content"]["parts"][0]["text"];
    if (!text) {
        out_error = "AI response was empty or blocked by safety filters.";
        return false;
    }
    out_response = String(text);
    return true;
}

// ─────────────────────────────────────────────
//  Low-level HTTPS call — OpenAI
// ─────────────────────────────────────────────
static bool call_openai(const String& prompt, String& out_response, String& out_error) {
    if (WiFi.status() != WL_CONNECTED && WiFi.getMode() != WIFI_AP) {
        out_error = "No internet connection (device is in AP-only mode).";
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient https;
    if (!https.begin(client, OPENAI_URL)) {
        out_error = "Failed to initialize HTTPS connection.";
        return false;
    }
    https.addHeader("Content-Type", "application/json");
    https.addHeader("Authorization", "Bearer " + current_api_key);
    https.setTimeout(20000);

    String body = String("{\"model\":\"gpt-4o-mini\",\"messages\":[{\"role\":\"user\",\"content\":\"") +
                  json_escape(prompt) + "\"}],\"temperature\":0.4,\"max_tokens\":800}";

    int code = https.POST(body);
    if (code != 200) {
        out_error = "OpenAI API error (HTTP " + String(code) + "): " + https.getString();
        https.end();
        return false;
    }

    String resp = https.getString();
    https.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, resp);
    if (err) {
        out_error = "Failed to parse AI response JSON.";
        return false;
    }

    const char* text = doc["choices"][0]["message"]["content"];
    if (!text) {
        out_error = "AI response was empty.";
        return false;
    }
    out_response = String(text);
    return true;
}

// ─────────────────────────────────────────────
//  Prompt templates per mode
// ─────────────────────────────────────────────
static String build_prompt(AIMode mode, const String& context, const String& user_text) {
    String ctx = truncate_context(context);

    switch (mode) {
        case AIMode::SUMMARIZE:
            return "Summarize the following book excerpt in 3-4 concise sentences, "
                   "capturing the main idea and key points. Plain text only, no markdown:\n\n" + ctx;

        case AIMode::EXPLAIN:
            return "Explain the following passage in simple, plain language as if "
                   "to someone unfamiliar with the topic. Keep it under 120 words. "
                   "Plain text only:\n\nPassage: \"" + user_text + "\"\n\n"
                   "Surrounding context for reference:\n" + ctx;

        case AIMode::VOCABULARY:
            return "Define the word \"" + user_text + "\" as used in this sentence: \"" +
                   ctx + "\". Respond in this exact plain-text format with no markdown:\n"
                   "Meaning: <one-line definition>\n"
                   "Pronunciation: <simple phonetic spelling>\n"
                   "Example: <one short example sentence using the word>";

        case AIMode::STUDY_MCQ:
            return "Generate " + user_text + " multiple-choice questions (4 options each, "
                   "mark the correct one with an asterisk) based on this chapter text. "
                   "Plain text, numbered, no markdown formatting:\n\n" + ctx;

        case AIMode::STUDY_SHORT_Q:
            return "Generate " + user_text + " short-answer study questions (no answers needed) "
                   "based on this chapter text. Plain text, numbered list:\n\n" + ctx;

        case AIMode::STUDY_VIVA:
            return "Generate " + user_text + " oral exam (viva voce) questions a professor "
                   "might ask about this chapter, ranging from basic recall to deeper "
                   "analysis. Plain text, numbered list:\n\n" + ctx;

        case AIMode::NOTE_EXTRACT:
            return "Extract the key facts, definitions, and takeaways from this chapter "
                   "as a concise bullet-style list (use '-' for bullets, plain text, "
                   "no markdown headers):\n\n" + ctx;

        case AIMode::CHAT_WITH_BOOK:
            return "You are a helpful reading assistant. Using ONLY the following book "
                   "context, answer the user's question. If the answer isn't in the "
                   "context, say so honestly rather than guessing.\n\n"
                   "Book context:\n" + ctx + "\n\n"
                   "Conversation so far:\n" + user_text;

        case AIMode::OCR_CLEANUP:
            return "The following text was extracted via OCR from a photographed book "
                   "page and may contain recognition errors, broken line breaks, or "
                   "stray characters. Clean it up into well-formatted, readable prose "
                   "while preserving the original meaning and wording as closely as "
                   "possible. Plain text only:\n\n" + ctx;
    }
    return ctx;
}

// ─────────────────────────────────────────────
//  Main dispatch — synchronous, meant for Core 0
// ─────────────────────────────────────────────
void ai_request(AIMode mode, const String& context, const String& user_text,
                 AICallback callback) {
    if (!ai_is_configured()) {
        callback(false, "AI is not configured. Add a Gemini or OpenAI API key in "
                         "Settings > AI Assistant.");
        return;
    }

    String prompt = build_prompt(mode, context, user_text);
    String response, error;
    bool ok;

    if (current_provider == AIProvider::GEMINI) {
        ok = call_gemini(prompt, response, error);
    } else {
        ok = call_openai(prompt, response, error);
    }

    callback(ok, ok ? response : error);
}

// ─────────────────────────────────────────────
//  Convenience wrappers
// ─────────────────────────────────────────────
void ai_summarize_page(const String& page_text, AICallback cb) {
    ai_request(AIMode::SUMMARIZE, page_text, "", cb);
}

void ai_explain_selection(const String& selection, const String& surrounding_context, AICallback cb) {
    ai_request(AIMode::EXPLAIN, surrounding_context, selection, cb);
}

void ai_define_word(const String& word, const String& sentence_context, AICallback cb) {
    ai_request(AIMode::VOCABULARY, sentence_context, word, cb);
}

void ai_generate_mcqs(const String& chapter_text, int count, AICallback cb) {
    ai_request(AIMode::STUDY_MCQ, chapter_text, String(count), cb);
}

void ai_generate_short_questions(const String& chapter_text, int count, AICallback cb) {
    ai_request(AIMode::STUDY_SHORT_Q, chapter_text, String(count), cb);
}

void ai_generate_viva_questions(const String& chapter_text, int count, AICallback cb) {
    ai_request(AIMode::STUDY_VIVA, chapter_text, String(count), cb);
}

void ai_extract_notes(const String& chapter_text, AICallback cb) {
    ai_request(AIMode::NOTE_EXTRACT, chapter_text, "", cb);
}

void ai_chat_with_book(const String& book_context, const String& chat_history,
                        const String& new_question, AICallback cb) {
    String history_plus_q = chat_history + "\nUser: " + new_question;
    ai_request(AIMode::CHAT_WITH_BOOK, book_context, history_plus_q, cb);
}

void ai_cleanup_ocr_text(const String& raw_ocr_text, AICallback cb) {
    ai_request(AIMode::OCR_CLEANUP, raw_ocr_text, "", cb);
}