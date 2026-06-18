#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SD.h>
#include <Update.h>
#include <WiFi.h>
#include <lvgl.h>
#include <esp_sleep.h>
#include "ui_engine.h"
#include "reader_ui.h"
#include "network.h"
#include "wifi_manager.h"
#include "camera_engine.h"
#include "audio_engine.h"

TFT_eSPI tft = TFT_eSPI();

#define POWER_BUTTON_PIN 1 // Button connected between GPIO 1 and GND

void checkSDCardBootloader() {
    if (!SD.exists("/update.bin")) {
        Serial.println("No SD update binary staged. Booting main app normally...");
        return;
    }

    File updateFile = SD.open("/update.bin", FILE_READ);
    size_t updateSize = updateFile.size();

    if (updateSize > 0) {
        Serial.printf("Found update.bin (%d bytes) on SD card. Initializing self-flash...\n", updateSize);

        if (Update.begin(updateSize, U_FLASH)) {
            size_t written = Update.writeStream(updateFile);
            if (written == updateSize) {
                Serial.println("Internal flash copy finished successfully.");
            }

            if (Update.end()) {
                if (Update.isFinished()) {
                    Serial.println("System update complete. Removing binary from SD to break loop...");
                    updateFile.close();
                    SD.remove("/update.bin");
                    delay(500);
                    ESP.restart();
                }
            } else {
                Serial.printf("Update engine failure: %s\n", Update.errorString());
            }
        }
    }
    updateFile.close();
}

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)&color_p->full, w * h, true);
    tft.endWrite();

    lv_disp_flush_ready(disp);
}

// --- BUTTON HARDWARE MONITOR ---
void checkPowerButtonHardware() {
    static bool lastButtonState = HIGH;
    static unsigned long buttonPressedTime = 0;
    static bool longPressHandled = false;

    bool currentButtonState = digitalRead(POWER_BUTTON_PIN);

    // Falling edge (button just pressed)
    if (currentButtonState == LOW && lastButtonState == HIGH) {
        buttonPressedTime = millis();
        longPressHandled = false;
    }

    // Held down
    if (currentButtonState == LOW && !longPressHandled) {
        if (millis() - buttonPressedTime > 1500) {
            longPressHandled = true;
            Serial.println("Long press detected. Shutting down cleanly...");

            set_display_brightness(0);
            SD.end();
            WiFi.disconnect(true);

            gpio_wakeup_enable((gpio_num_t)POWER_BUTTON_PIN, GPIO_INTR_LOW_LEVEL);
            esp_sleep_enable_gpio_wakeup();

            Serial.println("Entering deep sleep.");
            delay(100);
            esp_deep_sleep_start();
        }
    }

    // Rising edge (button released)
    if (currentButtonState == HIGH && lastButtonState == LOW) {
        if (!longPressHandled) {
            toggle_screen_lock();
        }
    }

    lastButtonState = currentButtonState;
}

// --- CORE 0: NETWORK + CLOCK + AUDIO TASK ---
// Real battery reading now happens in ui_engine.cpp's own periodic
// timer (read_battery_adc(), driven by an LVGL timer on Core 1, since
// it needs the ADC + post_battery_update() which both live there).
// This task handles networking, the NTP-synced clock, and draining
// the audio engine's record/playback request queue. audio_task_tick()
// is called first each iteration so mic streaming/playback isn't
// starved by web-server or OTA housekeeping further down the loop —
// if recordings come out choppy, this is the first place to look.
void CoreZeroNetworkTask(void * pvParameters) {
    init_network_subsystems();
    audio_init();

#ifdef CAMERA_MODULE_ENABLED
    camera_init();
#endif

    unsigned long last_clock_update = 0;

    for (;;) {
        audio_task_tick();
        handle_network_comms();

        if (millis() - last_clock_update > 10000) {
            int h, m, s;
            if (ntp_get_time(h, m, s)) {
                reader_ui_update_clock(h, m);
            }
            last_clock_update = millis();
        }

        vTaskDelay(pdMS_TO_TICKS(4));
    }
}

void setup() {
    Serial.begin(115200);

    pinMode(POWER_BUTTON_PIN, INPUT_PULLUP);

    if (!SD.begin()) {
        Serial.println("Critical error: SD storage bus initialization failed.");
    } else {
        checkSDCardBootloader();
    }

    tft.begin();
    tft.setRotation(1);
    lv_init();

    static lv_disp_draw_buf_t draw_buf;
    // 320 here matches disp_drv.hor_res below — using the literal directly
    // rather than LV_HOR_RES_MAX, since that macro is only defined when
    // lv_conf.h sets a fixed LV_HOR_RES, which this project doesn't do
    // (the resolution is supplied at runtime via disp_drv.hor_res/ver_res).
    static lv_color_t* disp_buf1 = (lv_color_t*) ps_malloc(320 * 15 * sizeof(lv_color_t));
    lv_disp_draw_buf_init(&draw_buf, disp_buf1, NULL, 320 * 15);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 320;
    disp_drv.ver_res = 480;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    init_ui_engine();

    xTaskCreatePinnedToCore(
        CoreZeroNetworkTask,
        "Network_Task",
        8192,
        NULL,
        1,
        NULL,
        0
    );
}

void loop() {
    checkPowerButtonHardware();

    if (xSemaphoreTake(lvgl_mutex, portMAX_DELAY) == pdTRUE) {
        lv_timer_handler();
        process_ui_events();   // drain battery/network/toast/AI/library events
        xSemaphoreGive(lvgl_mutex);
    }
    delay(4);
}