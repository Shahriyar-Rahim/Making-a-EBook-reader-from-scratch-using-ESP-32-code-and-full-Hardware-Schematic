// ═══════════════════════════════════════════════════════════════════
//  ota_manager.cpp  —  Legacy OTA stub.
//
//  OTA is now handled by network.cpp's manual /ota upload endpoint and
//  by the SD-card bootloader in main.cpp. This file remains only to
//  satisfy existing linkage for ota_manager_init()/ota_manager_tick().
// ═══════════════════════════════════════════════════════════════════
#include "ota_manager.h"
#include <Arduino.h>

void ota_manager_init() {
    Serial.println("[OTA] ota_manager stub initialized. OTA handled elsewhere.");
}

void ota_manager_tick() {
}
