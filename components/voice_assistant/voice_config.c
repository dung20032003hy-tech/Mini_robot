#include "voice_assistant.h"
#include <string.h>
#include <math.h>

void voice_config_defaults(voice_config_t *c) {
    memset(c, 0, sizeof(*c));
    c->version = 1;
    c->vad_threshold = 250;
    strcpy(c->model, "gpt-realtime-2.1-mini");
    strcpy(c->voice, "marin");
}
static bool identifier(const char *s, size_t capacity) {
    if (!memchr(s, 0, capacity) || !s[0])
        return false;
    for (; *s; ++s)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '-' || *s == '.'))
            return false;
    return true;
}
bool voice_config_validate(const voice_config_t *c) {
    if (c->version != 1 || c->vad_threshold < 50 || c->vad_threshold > 5000 ||
        !identifier(c->model, sizeof(c->model)) || strncmp(c->model, "gpt-realtime", 12) ||
        !identifier(c->voice, sizeof(c->voice)) ||
        !memchr(c->api_key, 0, sizeof(c->api_key)))
        return false;
    const char *voices[] = {"alloy", "ash", "ballad", "coral", "echo", "sage", "shimmer",
                            "verse", "marin", "cedar"};
    bool valid_voice = false;
    for (unsigned i = 0; i < sizeof(voices) / sizeof(voices[0]); ++i)
        valid_voice |= !strcmp(c->voice, voices[i]);
    if (!valid_voice)
        return false;
    size_t len = strlen(c->api_key);
    if (len && (len < 20 || strncmp(c->api_key, "sk-", 3)))
        return false;
    for (size_t i = 0; i < len; ++i)
        if (!((c->api_key[i] >= 'A' && c->api_key[i] <= 'Z') ||
              (c->api_key[i] >= 'a' && c->api_key[i] <= 'z') ||
              (c->api_key[i] >= '0' && c->api_key[i] <= '9') ||
               c->api_key[i] == '_' || c->api_key[i] == '-'))
            return false; /* Also prevents HTTP header injection. */
    return !c->enabled || len > 0;
}
bool voice_config_patch(voice_config_t *c, const cJSON *j) {
    if (!cJSON_IsObject(j))
        return false;
    voice_config_t next = *c;
    for (const cJSON *v = j->child; v; v = v->next) {
        for (const cJSON *other = v->next; other; other = other->next)
            if (!strcmp(v->string, other->string))
                return false;
        if (!strcmp(v->string, "enabled") && cJSON_IsBool(v))
            next.enabled = cJSON_IsTrue(v);
        else if (!strcmp(v->string, "vad_threshold") && cJSON_IsNumber(v) &&
                 isfinite(v->valuedouble) && v->valuedouble >= 50 && v->valuedouble <= 5000 &&
                 floor(v->valuedouble) == v->valuedouble)
            next.vad_threshold = v->valueint;
        else if (!strcmp(v->string, "api_key") && cJSON_IsString(v) &&
                 strlen(v->valuestring) < sizeof(next.api_key)) {
            memset(next.api_key, 0, sizeof(next.api_key));
            strcpy(next.api_key, v->valuestring);
        } else if (!strcmp(v->string, "model") && cJSON_IsString(v) &&
                   strlen(v->valuestring) < sizeof(next.model))
            strcpy(next.model, v->valuestring);
        else if (!strcmp(v->string, "voice") && cJSON_IsString(v) &&
                 strlen(v->valuestring) < sizeof(next.voice))
            strcpy(next.voice, v->valuestring);
        else
            return false;
    }
    if (!voice_config_validate(&next))
        return false;
    *c = next;
    return true;
}
cJSON *voice_config_public(const voice_config_t *c) {
    cJSON *j = cJSON_CreateObject();
    if (j) {
        cJSON_AddBoolToObject(j, "enabled", c->enabled);
        cJSON_AddBoolToObject(j, "key_configured", c->api_key[0] != 0);
        cJSON_AddStringToObject(j, "model", c->model);
        cJSON_AddStringToObject(j, "voice", c->voice);
        cJSON_AddNumberToObject(j, "vad_threshold", c->vad_threshold);
    }
    return j;
}
