#include "lcd_display.h"
#include "board_config.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static spi_device_handle_t device;
static settings_t cfg;
static int width, height, xoff, yoff;
static uint16_t *dma;
static esp_err_t transfer(bool data, const void *bytes, size_t count) {
    esp_err_t e = gpio_set_level(BOARD.lcd_dc, data);
    if (e != ESP_OK)
        return e;
    spi_transaction_t t = {.length = count * 8};
    if (count <= sizeof(t.tx_data)) {
        t.flags = SPI_TRANS_USE_TXDATA;
        memcpy(t.tx_data, bytes, count);
    } else
        t.tx_buffer = bytes;
    return spi_device_transmit(device, &t);
}
static esp_err_t command(uint8_t cmd, const void *data, size_t count) {
    esp_err_t e = transfer(false, &cmd, 1);
    return e == ESP_OK && count ? transfer(true, data, count) : e;
}
int lcd_width(void) {
    return width;
}
int lcd_height(void) {
    return height;
}
esp_err_t lcd_init(const settings_t *s) {
    if (!s->lcd_enabled)
        return ESP_ERR_NOT_SUPPORTED;
    cfg = *s;
    bool rotated = s->lcd_rotation & 1;
    width = rotated ? s->lcd_height : s->lcd_width;
    height = rotated ? s->lcd_width : s->lcd_height;
    xoff = rotated ? s->lcd_y_offset : s->lcd_x_offset;
    yoff = rotated ? s->lcd_x_offset : s->lcd_y_offset;
    gpio_config_t io = {.pin_bit_mask = (1ULL << BOARD.lcd_dc) | (1ULL << BOARD.lcd_rst),
                        .mode = GPIO_MODE_OUTPUT};
    esp_err_t e = gpio_config(&io);
    if (e != ESP_OK)
        return e;
    if (BOARD.lcd_bl >= 0) {
        if ((e = gpio_set_direction(BOARD.lcd_bl, GPIO_MODE_OUTPUT)) != ESP_OK)
            return e;
        if ((e = gpio_set_level(BOARD.lcd_bl, 0)) != ESP_OK)
            return e;
    }
    spi_bus_config_t bus = {.mosi_io_num = BOARD.lcd_mosi,
                            .miso_io_num = -1,
                            .sclk_io_num = BOARD.lcd_sclk,
                            .quadwp_io_num = -1,
                            .quadhd_io_num = -1,
                            .max_transfer_sz = width * 8 * 2};
    if ((e = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO)) != ESP_OK)
        return e;
    spi_device_interface_config_t dev = {
        .clock_speed_hz = 20000000, .mode = 0, .spics_io_num = BOARD.lcd_cs, .queue_size = 1};
    if ((e = spi_bus_add_device(SPI2_HOST, &dev, &device)) != ESP_OK)
        return e;
    dma = heap_caps_malloc(width * 8 * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!dma)
        return ESP_ERR_NO_MEM;
    if ((e = gpio_set_level(BOARD.lcd_rst, 0)) != ESP_OK)
        return e;
    vTaskDelay(pdMS_TO_TICKS(20));
    if ((e = gpio_set_level(BOARD.lcd_rst, 1)) != ESP_OK)
        return e;
    vTaskDelay(pdMS_TO_TICKS(120));
    if ((e = command(0x01, NULL, 0)) != ESP_OK)
        return e;
    vTaskDelay(pdMS_TO_TICKS(150));
    if ((e = command(0x11, NULL, 0)) != ESP_OK)
        return e;
    vTaskDelay(pdMS_TO_TICKS(120));
    uint8_t format = 0x55;
    if ((e = command(0x3a, &format, 1)) != ESP_OK)
        return e;
    const uint8_t rotations[] = {0x00, 0x60, 0xc0, 0xa0};
    uint8_t madctl = rotations[s->lcd_rotation] | (s->lcd_bgr ? 0x08 : 0);
    if ((e = command(0x36, &madctl, 1)) != ESP_OK)
        return e;
    if ((e = command(s->lcd_invert ? 0x21 : 0x20, NULL, 0)) != ESP_OK)
        return e;
    if ((e = command(0x29, NULL, 0)) != ESP_OK)
        return e;
    if (BOARD.lcd_bl >= 0)
        e = gpio_set_level(BOARD.lcd_bl, s->brightness > 0);
    return e;
}
esp_err_t lcd_present(const uint16_t *pixels) {
    if (!device || !dma)
        return ESP_ERR_INVALID_STATE;
    uint16_t x0 = xoff, x1 = xoff + width - 1, y0 = yoff, y1 = yoff + height - 1;
    uint8_t col[] = {x0 >> 8, x0, x1 >> 8, x1}, row[] = {y0 >> 8, y0, y1 >> 8, y1};
    esp_err_t e = command(0x2a, col, 4);
    if (e != ESP_OK)
        return e;
    if ((e = command(0x2b, row, 4)) != ESP_OK)
        return e;
    if ((e = command(0x2c, NULL, 0)) != ESP_OK)
        return e;
    for (int y = 0; y < height; y += 8) {
        int rows = height - y < 8 ? height - y : 8, n = rows * width;
        for (int i = 0; i < n; ++i) {
            uint16_t p = pixels[y * width + i];
            dma[i] = (p << 8) | (p >> 8);
        }
        if ((e = transfer(true, dma, n * 2)) != ESP_OK)
            return e;
    }
    return ESP_OK;
}
