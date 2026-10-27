#include "board_config.h"
#include "driver/gpio.h"
#include "esp_log.h"

const board_pins_t BOARD = {
    .motor = {{4, 5}, {6, 7}, {8, 9}, {10, 11}},
    .sleep = {-1, -1},
    .servo = {40, 41},
    .lcd_sclk = 12,
    .lcd_mosi = 13,
    .lcd_dc = 14,
    .lcd_cs = 15,
    .lcd_rst = 16,
    .lcd_bl = -1,
    .i2s_bclk = 17,
    .i2s_ws = 18,
    .mic_sd = 21,
    .speaker_din = 39,
    .battery_adc = 1,
};

esp_err_t board_validate(void) {
    const int pins[] = {BOARD.motor[0][0], BOARD.motor[0][1], BOARD.motor[1][0], BOARD.motor[1][1],
                        BOARD.motor[2][0], BOARD.motor[2][1], BOARD.motor[3][0], BOARD.motor[3][1],
                        BOARD.sleep[0],    BOARD.sleep[1],    BOARD.servo[0],    BOARD.servo[1],
                        BOARD.lcd_sclk,    BOARD.lcd_mosi,    BOARD.lcd_dc,      BOARD.lcd_cs,
                        BOARD.lcd_rst,     BOARD.lcd_bl,      BOARD.i2s_bclk,    BOARD.i2s_ws,
                        BOARD.mic_sd,      BOARD.speaker_din, BOARD.battery_adc};
    for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        int p = pins[i];
        if (p == -1)
            continue;
        /* Reserve USB 19/20, strapping 0/3/45/46, flash/Octal PSRAM 26..37,
         * UART console 43/44 and pad JTAG 42. 39/40/41 repurpose pad JTAG;
         * use USB JTAG instead. Board exposure still needs physical review. */
        if (!GPIO_IS_VALID_OUTPUT_GPIO(p) || p == 0 || p == 3 || p == 19 || p == 20 ||
            (p >= 26 && p <= 37) || p == 42 || p == 43 || p == 44 || p == 45 || p == 46)
            return ESP_ERR_INVALID_ARG;
        for (unsigned j = 0; j < i; ++j)
            if (pins[j] == p)
                return ESP_ERR_INVALID_ARG;
    }
    if (BOARD.battery_adc != 1 && BOARD.battery_adc != -1)
        return ESP_ERR_NOT_SUPPORTED;
    return ESP_OK;
}

static esp_err_t low_output(int pin) {
    if (pin < 0)
        return ESP_OK;
    esp_err_t err = gpio_set_level(pin, 0);
    if (err != ESP_OK)
        return err;
    return gpio_set_direction(pin, GPIO_MODE_OUTPUT);
}

esp_err_t board_safe_outputs(void) {
    esp_err_t err = board_validate();
    if (err != ESP_OK)
        return err;
    for (int i = 0; i < 2; ++i) {
        if ((err = low_output(BOARD.sleep[i])) != ESP_OK)
            return err;
        if ((err = low_output(BOARD.servo[i])) != ESP_OK)
            return err;
    }
    for (int m = 0; m < 4; ++m)
        for (int p = 0; p < 2; ++p)
            if ((err = low_output(BOARD.motor[m][p])) != ESP_OK)
                return err;
    return ESP_OK;
}
