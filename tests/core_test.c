#include "control_core.h"
#include "settings_manager.h"
#include "command_protocol.h"
#include "audio_output.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static void near(float a, float b) {
    assert(fabsf(a - b) < 0.0001f);
}
static bool parse(const char *json) {
    command_t c;
    return protocol_parse(json, strlen(json), &c);
}
int main(void) {
    float m[4];
    assert(core_mix(1, 1, 0.25, m));
    near(m[0], 0.25);
    near(m[2], 0);
    assert(core_mix(-1, -1, 0.25, m));
    near(m[0], -0.25);
    near(m[2], 0);
    assert(core_mix(0, 1, 0.25, m));
    near(m[0], 0.25);
    near(m[2], -0.25);
    assert(!core_mix(NAN, 0, 1, m));
    assert(!core_mix(2, 0, 1, m));
    assert(!core_mix(0, 0, 2, m));
    near(core_motor_output(0.25, true, 1.5, 0.25), -0.25);
    near(core_motor_output(-0.2, false, 0.5, 0.25), -0.1);
    ramp_t r = {0};
    near(core_ramp(&r, 0.25, 1, 2, 100, 10, 0.01), 0.01);
    for (unsigned now = 20; now <= 300; now += 10)
        core_ramp(&r, 0.25, 1, 2, 100, now, 0.01);
    near(r.actual, 0.25);
    uint32_t zero = 0;
    for (unsigned now = 310; now < 800; now += 10) {
        float v = core_ramp(&r, -0.25, 1, 2, 100, now, 0.01);
        if (v == 0 && !zero)
            zero = now;
        if (v < 0)
            assert(now - zero >= 100);
    }
    near(r.actual, -0.25);
    assert(zero);
    /* A separate stop command must not bypass reversal dead time. */
    r = (ramp_t){.actual = 0.1, .last_sign = 1};
    core_ramp(&r, 0, 1, 10, 100, 1000, 0.02);
    near(r.actual, 0);
    core_ramp(&r, -0.2, 1, 10, 100, 1050, 0.02);
    near(r.actual, 0);
    assert(core_ramp(&r, -0.2, 1, 10, 100, 1100, 0.02) < 0);
    assert(core_expired(50, UINT32_MAX - 100, 100));
    assert(!core_expired(50, 40, 400));
    assert(core_accept_sequence(1, 2));
    assert(!core_accept_sequence(2, 2));
    assert(!core_accept_sequence(2, 1));
    assert(!core_accept_sequence(2147483647U, 1));
    for (int t = -10; t <= 10; ++t)
        for (int st = -10; st <= 10; ++st) {
            assert(core_mix(t / 10.0f, st / 10.0f, 0.25f, m));
            for (int i = 0; i < 4; ++i)
                assert(fabsf(m[i]) <= 0.25001f);
        }
    assert(core_servo_pulse(-10, false, 60, 120, 1200, 1800) == 1200);
    assert(core_servo_pulse(150, false, 60, 120, 1200, 1800) == 1800);
    assert(core_servo_pulse(60, true, 60, 120, 1200, 1800) == 1800);
    assert(core_servo_pulse(90, false, 60, 120, 1200, 1800) == 1500);
    assert(core_transition(CORE_ARMED, true, false, false, true, true) == CORE_ESTOP);
    assert(core_transition(CORE_ESTOP, false, false, false, true, true) == CORE_ESTOP);
    assert(core_transition(CORE_ESTOP, false, false, true, false, true) == CORE_DISARMED);
    assert(core_transition(CORE_ARMED, false, false, false, true, false) == CORE_DISARMED);
    assert(core_transition(CORE_FAULT, false, false, true, true, true) == CORE_FAULT);
    assert(parse("{\"v\":1,\"seq\":1,\"type\":\"drive\",\"throttle\":1,\"steering\":-1}"));
    const char *bad[] = {
        "{}",
        "[]",
        "null",
        "{\"v\":1,\"seq\":1,\"type\":\"drive\",\"throttle\":2,\"steering\":0}",
        "{\"v\":1,\"seq\":1,\"type\":\"estop\",\"evil\":true}",
        "{\"v\":2,\"seq\":1,\"type\":\"estop\"}",
        "{\"v\":1,\"seq\":0,\"type\":\"estop\"}",
        "{\"v\":1,\"seq\":1.5,\"type\":\"estop\"}",
        "{\"v\":1,\"seq\":2147483648,\"type\":\"estop\"}",
        "{\"v\":1,\"seq\":1,\"type\":\"estop\",\"type\":\"arm\"}",
        "{\"v\":1,\"seq\":1,\"type\":\"estop\"} junk",
        "{\"v\":1,\"seq\":1,\"type\":\"eyes_options\",\"auto_blink\":true,\"brightness\":1.5}",
        "{\"v\":1,\"seq\":1,\"type\":\"drive\",\"throttle\":\"0\",\"steering\":0}",
        ("{\"v\":1,\"seq\":1,\"type\":\"drive\",\"throttle\":0,\"steering\":0,\"steering||speed\":"
         "1}")};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        assert(!parse(bad[i]));
    assert(!parse("[[[[[[[[[0]]]]]]]]]"));
    assert(!parse("{\"v\":1,\"seq\":1,\"type\":\"estop\\u0000drive\"}"));
    assert(protocol_json_frame_valid("{\"a\":\"[{}]\"}", strlen("{\"a\":\"[{}]\"}")));
    uint32_t usb_seq = 0;
    const char *usb_good[] = {
        "{\"v\":1,\"seq\":1,\"type\":\"usb_hello\"}",
        "{\"v\":1,\"seq\":2,\"type\":\"usb_status\"}",
        "{\"v\":1,\"seq\":3,\"type\":\"usb_settings_get\"}",
        "{\"v\":1,\"seq\":4,\"type\":\"usb_settings_patch\",\"patch\":{\"motor_limit\":0.25}}",
        "{\"v\":1,\"seq\":5,\"type\":\"usb_reboot\"}",
        "{\"v\":1,\"seq\":6,\"type\":\"usb_ai_config\",\"config\":{\"enabled\":false}}",
        "{\"v\":1,\"seq\":7,\"type\":\"usb_ai_pause\",\"paused\":true}"};
    for (unsigned i = 0; i < sizeof(usb_good) / sizeof(usb_good[0]); ++i) {
        cJSON *usb = protocol_usb_parse(usb_good[i], strlen(usb_good[i]), &usb_seq);
        assert(usb && usb_seq == i + 1);
        cJSON_Delete(usb);
        assert(!parse(usb_good[i])); /* USB management cannot become a WebSocket command. */
    }
    const char *usb_bad[] = {
        "{\"v\":1,\"seq\":2,\"type\":\"usb_hello\"}",
        "{\"v\":1,\"seq\":0,\"type\":\"usb_status\"}",
        "{\"v\":1,\"seq\":1.5,\"type\":\"usb_status\"}",
        "{\"v\":1,\"seq\":2147483648,\"type\":\"usb_status\"}",
        "{\"v\":2,\"seq\":1,\"type\":\"usb_status\"}",
        "{\"v\":1,\"seq\":1,\"type\":\"usb_status\",\"unknown\":1}",
        "{\"v\":1,\"seq\":1,\"type\":\"usb_status\",\"type\":\"usb_reboot\"}",
        "{\"v\":1,\"seq\":1,\"type\":\"usb_settings_patch\",\"patch\":[]}",
        "{\"v\":1,\"seq\":1,\"type\":\"usb_ai_config\",\"config\":[]}",
        "{\"v\":1,\"seq\":1,\"type\":\"usb_ai_config\",\"config\":{},\"api_key\":\"hidden\"}",
        "{\"v\":1,\"seq\":1,\"type\":\"usb_ai_pause\",\"paused\":1}",
        "{\"v\":1,\"seq\":1,\"type\":\"usb_settings_patch\",\"patch\":{\"x\":1,\"x\":2}}",
        "{\"v\":1,\"seq\":1,\"type\":\"usb_status\\u0000\"}",
        "{\"v\":1,\"seq\":1,\"type\":\"usb_status\"}{}"};
    for (unsigned i = 0; i < sizeof(usb_bad) / sizeof(usb_bad[0]); ++i)
        assert(!protocol_usb_parse(usb_bad[i], strlen(usb_bad[i]), &usb_seq));
    const char usb_nul[] = "{\"v\":1,\"seq\":1,\"type\":\"usb_status\"}\0{}";
    assert(!protocol_usb_parse(usb_nul, sizeof(usb_nul) - 1, &usb_seq));
    char usb_large[1025];
    memset(usb_large, ' ', sizeof(usb_large));
    assert(!protocol_usb_parse(usb_large, sizeof(usb_large), &usb_seq));
    puts("PASS: USB-only management envelopes, version, sequence, duplicate/unknown keys, NUL and size limits");
    settings_t s;
    settings_defaults(&s);
    memset(s.token, 'a', 64);
    s.token[64] = 0;
    strcpy(s.ap_password, "123456789012");
    assert(settings_validate(&s));
    cJSON *j = cJSON_Parse("{\"motor_limit\":0.3,\"servo\":[{\"min_deg\":70},{\"invert\":true}]}");
    assert(settings_patch(&s, j));
    cJSON_Delete(j);
    near(s.motor_limit, 0.3);
    near(s.servo[0].min_deg, 70);
    assert(s.servo[1].invert);
    const char *invalid[] = {"{\"timeout_ms\":10000}",
                             "{\"volume\":256}",
                             "{\"volume\":-1}",
                             "{\"volume\":50.5}",
                             "{\"wifi_password\":\"short\"}",
                             "{\"servo\":[{\"min_deg\":150},{}]}",
                             "{\"lcd_x_offset\":240}",
                             "{\"hardware_confirmed\":1}",
                             "{\"unknown\":1}"};
    settings_t before = s;
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        j = cJSON_Parse(invalid[i]);
        assert(!settings_patch(&s, j));
        assert(!memcmp(&s, &before, sizeof(s)));
        cJSON_Delete(j);
    }
    uint8_t wav[44] = {'R', 'I', 'F', 'F', 38, 0, 0,   0,   'W', 'A',  'V',  'E', 'f', 'm', 't',
                       ' ', 16,  0,   0,   0,  1, 0,   1,   0,   0x80, 0x3e, 0,   0,   0,   0x7d,
                       0,   0,   2,   0,   16, 0, 'd', 'a', 't', 'a',  2,    0,   0,   0};
    uint32_t bytes;
    assert(audio_output_wav_header(wav, 46, &bytes) && bytes == 2);
    wav[22] = 2;
    assert(!audio_output_wav_header(wav, 46, &bytes));
    puts("PASS: mixing, normalization, inversion, duty limits, acceleration, reversal deadtime, "
         "servo bounds, timeout rollover, safety transitions, strict JSON, transactional settings "
         "validation, WAV format");
    return 0;
}
