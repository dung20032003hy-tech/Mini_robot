#pragma once
#include <stdbool.h>
#include "esp_err.h"

/* Change this ONE mapping after checking the actual module/DevKit schematic.
 * -1 means unwired. Do not allocate GPIO for future hardware. */
typedef struct {
    int motor[4][2];
    int sleep[2];
    int servo[2];
    int lcd_sclk, lcd_mosi, lcd_dc, lcd_cs, lcd_rst, lcd_bl;
    int i2s_bclk, i2s_ws, mic_sd, speaker_din;
    int battery_adc;
} board_pins_t;
extern const board_pins_t BOARD;
esp_err_t board_validate(void);
esp_err_t board_safe_outputs(void);
