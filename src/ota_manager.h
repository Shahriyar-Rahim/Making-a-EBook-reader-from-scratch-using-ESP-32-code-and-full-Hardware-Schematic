#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

// ─────────────────────────────────────────────
//  OTA Manager — legacy stub.
//
//  OTA is now handled by network.cpp's manual /ota upload endpoint and
//  by the SD-card bootloader in main.cpp. This header exists only to
//  satisfy existing linkage for ota_manager_init()/ota_manager_tick().
// ─────────────────────────────────────────────

void ota_manager_init();

void ota_manager_tick();

#endif // OTA_MANAGER_H