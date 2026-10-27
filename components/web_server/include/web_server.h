#pragma once
#include "settings_manager.h"
esp_err_t web_server_init(const settings_t *settings);
/* Physical USB console only. Commands are queued onto the HTTP server task so
 * USB and WebSocket share the same controller lease and configuration state. */
esp_err_t web_server_usb_command(const char *json, size_t length);
