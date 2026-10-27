/* Host-only unavailable hardware adapters. Never compiled into firmware.
 * Pure validation/mixing/protocol/WAV code is the actual production source. */
#include "nvs.h"
#include "esp_random.h"
#include "audio_input.h"
esp_err_t nvs_open(const char *a, int b, nvs_handle_t *c) {
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t nvs_get_blob(nvs_handle_t a, const char *b, void *c, size_t *d) {
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t nvs_set_blob(nvs_handle_t a, const char *b, const void *c, size_t d) {
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t nvs_commit(nvs_handle_t a) {
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t nvs_erase_all(nvs_handle_t a) {
    return ESP_ERR_NOT_SUPPORTED;
}
void nvs_close(nvs_handle_t a) {
    (void)a;
}
esp_err_t nvs_flash_init(void) {
    return ESP_ERR_NOT_SUPPORTED;
}
uint32_t esp_random(void) {
    return 1;
}
void bootloader_random_enable(void) {}
void bootloader_random_disable(void) {}
esp_err_t audio_engine_enqueue(const int16_t *a, size_t n) {
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t audio_engine_tone(void) {
    return ESP_ERR_NOT_SUPPORTED;
}
void audio_engine_stop(void) {}
void audio_engine_volume(unsigned n) {}
