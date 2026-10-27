#pragma once
#include "settings_manager.h"
typedef struct {
    bool station_connected;
    char station_ip[16], ap_ip[16], ap_ssid[33];
    unsigned ap_clients;
} wifi_status_t;
esp_err_t wifi_manager_init(const settings_t *cfg);
void wifi_manager_snapshot(wifi_status_t *out);
