#pragma once
#include "settings_manager.h"

/* Credentials can only be provisioned over physical USB. Never serialize key. */
typedef struct {
    uint32_t version;
    bool enabled;
    uint16_t vad_threshold;
    char model[64], voice[16], api_key[256];
} voice_config_t;
void voice_config_defaults(voice_config_t *config);
bool voice_config_validate(const voice_config_t *config);
bool voice_config_patch(voice_config_t *config, const cJSON *patch);
cJSON *voice_config_public(const voice_config_t *config);
esp_err_t voice_assistant_init(const settings_t *settings);
esp_err_t voice_assistant_configure(const cJSON *patch); /* save; reboot to apply */
cJSON *voice_assistant_settings(void);
cJSON *voice_assistant_status(void);
/* Called by the shared controller: manual control always takes priority. */
void voice_assistant_control_busy(bool busy);
void voice_assistant_pause(bool paused);
bool voice_assistant_owns_audio(void);
