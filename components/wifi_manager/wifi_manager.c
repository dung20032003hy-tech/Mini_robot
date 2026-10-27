#include "wifi_manager.h"
#include "safety_manager.h"
#include "robot_eyes.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static wifi_status_t status;
static esp_timer_handle_t reconnect;
static bool station_enabled;
static void retry(void *arg) {
    esp_err_t e = esp_wifi_connect();
    if (e != ESP_OK)
        ESP_LOGW("wifi", "Reconnect: %s", esp_err_to_name(e));
}
static void wifi_event(void *arg, esp_event_base_t base, int32_t event, void *data) {
    if (base == WIFI_EVENT && event == WIFI_EVENT_STA_START && station_enabled)
        retry(NULL);
    if (base == WIFI_EVENT && event == WIFI_EVENT_STA_DISCONNECTED) {
        portENTER_CRITICAL(&mux);
        status.station_connected = false;
        status.station_ip[0] = 0;
        portEXIT_CRITICAL(&mux);
        safety_disarm("Station disconnected; re-arm required");
        eyes_notice("AP");
        esp_err_t e = esp_timer_start_once(reconnect, 3000000);
        if (e != ESP_OK && e != ESP_ERR_INVALID_STATE)
            ESP_LOGW("wifi", "Retry timer: %s", esp_err_to_name(e));
    }
    if (base == WIFI_EVENT && event == WIFI_EVENT_AP_STACONNECTED) {
        portENTER_CRITICAL(&mux);
        ++status.ap_clients;
        portEXIT_CRITICAL(&mux);
    }
    if (base == WIFI_EVENT && event == WIFI_EVENT_AP_STADISCONNECTED) {
        portENTER_CRITICAL(&mux);
        if (status.ap_clients)
            --status.ap_clients;
        portEXIT_CRITICAL(&mux);
        safety_disarm("AP client disconnected; re-arm required");
    }
    if (base == IP_EVENT && event == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *info = data;
        portENTER_CRITICAL(&mux);
        status.station_connected = true;
        snprintf(status.station_ip, 16, IPSTR, IP2STR(&info->ip_info.ip));
        portEXIT_CRITICAL(&mux);
        ESP_LOGI("wifi", "Station connected; movement remains disarmed");
        eyes_notice("WIFI");
    }
}
void wifi_manager_snapshot(wifi_status_t *out) {
    portENTER_CRITICAL(&mux);
    *out = status;
    portEXIT_CRITICAL(&mux);
}
esp_err_t wifi_manager_init(const settings_t *s) {
    esp_err_t e = esp_netif_init();
    if (e != ESP_OK)
        return e;
    if ((e = esp_event_loop_create_default()) != ESP_OK)
        return e;
    esp_netif_t *ap = esp_netif_create_default_wifi_ap();
    if (!ap)
        return ESP_ERR_NO_MEM;
    if (!esp_netif_create_default_wifi_sta())
        return ESP_ERR_NO_MEM;
    esp_netif_ip_info_t ip;
    if ((e = esp_netif_get_ip_info(ap, &ip)) != ESP_OK)
        return e;
    snprintf(status.ap_ip, 16, IPSTR, IP2STR(&ip.ip));
    uint8_t mac[6];
    if ((e = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP)) != ESP_OK)
        return e;
    snprintf(status.ap_ssid, sizeof(status.ap_ssid), "WALLE-%02X%02X%02X", mac[3], mac[4], mac[5]);
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if ((e = esp_wifi_init(&init)) != ESP_OK)
        return e;
    if ((e = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK)
        return e;
    esp_event_handler_instance_t wifi_handler, ip_handler;
    if ((e = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL,
                                                 &wifi_handler)) != ESP_OK)
        return e;
    if ((e = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL,
                                                 &ip_handler)) != ESP_OK)
        return e;
    esp_timer_create_args_t timer = {.callback = retry, .name = "wifi_retry"};
    if ((e = esp_timer_create(&timer, &reconnect)) != ESP_OK)
        return e;
    wifi_config_t ac = {0};
    strcpy((char *)ac.ap.ssid, status.ap_ssid);
    strcpy((char *)ac.ap.password, s->ap_password);
    ac.ap.ssid_len = strlen(status.ap_ssid);
    ac.ap.channel = 6;
    ac.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ac.ap.max_connection = 3;
    ac.ap.pmf_cfg.capable = true;
    station_enabled = s->wifi_ssid[0] != 0;
    if ((e = esp_wifi_set_mode(station_enabled ? WIFI_MODE_APSTA : WIFI_MODE_AP)) != ESP_OK)
        return e;
    if ((e = esp_wifi_set_config(WIFI_IF_AP, &ac)) != ESP_OK)
        return e;
    if (station_enabled) {
        wifi_config_t sta = {0};
        strcpy((char *)sta.sta.ssid, s->wifi_ssid);
        strcpy((char *)sta.sta.password, s->wifi_password);
        sta.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        sta.sta.pmf_cfg.capable = true;
        if ((e = esp_wifi_set_config(WIFI_IF_STA, &sta)) != ESP_OK)
            return e;
    }
    if ((e = esp_wifi_start()) != ESP_OK)
        return e;
    return esp_wifi_set_ps(WIFI_PS_NONE);
}
