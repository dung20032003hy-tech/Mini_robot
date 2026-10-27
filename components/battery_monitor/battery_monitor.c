#include "battery_monitor.h"
#include "board_config.h"
#include "safety_manager.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static adc_oneshot_unit_handle_t adc;
static adc_cali_handle_t cal;
static settings_t cfg;
static battery_status_t status;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
void battery_snapshot(battery_status_t *out) {
    portENTER_CRITICAL(&mux);
    *out = status;
    portEXIT_CRITICAL(&mux);
}
static void sample(void *arg) {
    unsigned lows = 0, criticals = 0, failures = 0;
    float filtered = 0;
    while (true) {
        int raw, mv;
        esp_err_t e = adc_oneshot_read(adc, ADC_CHANNEL_0, &raw);
        if (e == ESP_OK)
            e = adc_cali_raw_to_voltage(cal, raw, &mv);
        if (e == ESP_OK) {
            failures = 0;
            float v = mv / 1000.0f * cfg.battery_ratio;
            filtered = filtered == 0 ? v : filtered * 0.8f + v * 0.2f;
            lows = filtered < cfg.battery_low ? lows + 1 : 0;
            criticals = filtered < cfg.battery_critical ? criticals + 1 : 0;
            portENTER_CRITICAL(&mux);
            status = (battery_status_t){true, true, filtered};
            portEXIT_CRITICAL(&mux);
            safety_battery(lows >= 3, criticals >= 3);
        } else {
            if (++failures >= 3) {
                portENTER_CRITICAL(&mux);
                status.available = false;
                portEXIT_CRITICAL(&mux);
                safety_battery(true, true);
                safety_disarm("Battery measurement failure");
            }
            ESP_LOGW("battery", "ADC read failed: %s", esp_err_to_name(e));
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
esp_err_t battery_init(const settings_t *s) {
    if (!s->battery_enabled || BOARD.battery_adc != 1)
        return ESP_ERR_NOT_SUPPORTED;
    cfg = *s;
    adc_oneshot_unit_init_cfg_t unit = {.unit_id = ADC_UNIT_1};
    esp_err_t e = adc_oneshot_new_unit(&unit, &adc);
    if (e != ESP_OK)
        return e;
    adc_oneshot_chan_cfg_t ch = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    if ((e = adc_oneshot_config_channel(adc, ADC_CHANNEL_0, &ch)) != ESP_OK)
        return e;
    adc_cali_curve_fitting_config_t c = {.unit_id = ADC_UNIT_1,
                                         .chan = ADC_CHANNEL_0,
                                         .atten = ADC_ATTEN_DB_12,
                                         .bitwidth = ADC_BITWIDTH_DEFAULT};
    if ((e = adc_cali_create_scheme_curve_fitting(&c, &cal)) != ESP_OK)
        return e;
    return xTaskCreate(sample, "battery", 3072, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
