#ifndef NETWORK_H
#define NETWORK_H

// ─────────────────────────────────────────────
//  Network Subsystem Public API
// ─────────────────────────────────────────────

// Called once from CoreZeroNetworkTask at startup.
// Starts Wi-Fi AP, BLE server, HTTP dashboard, and OTA endpoint.
void init_network_subsystems();

// Called in a tight loop from CoreZeroNetworkTask.
// Handles OTA polling and any periodic network housekeeping.
void handle_network_comms();

#endif // NETWORK_H