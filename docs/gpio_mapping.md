# GPIO mapping

Nguồn duy nhất: `components/board_config/board_config.c` (`BOARD`). Thay mapping ở đây rồi build lại. Không cho đổi pin trực tiếp qua Web/NVS: sai mapping khi chạy có thể cấp tín hiệu vào chân phần cứng khác.

| Chức năng | GPIO | Lưu ý |
|---|---|---|
| Motor LF / DRV1 AIN1,AIN2 | 4,5 | LEDC0,1 |
| Motor LR / DRV1 BIN1,BIN2 | 6,7 | LEDC2,3 |
| Motor RF / DRV2 AIN1,AIN2 | 8,9 | LEDC4,5 |
| Motor RR / DRV2 BIN1,BIN2 | 10,11 | LEDC6,7 |
| DRV1/DRV2 nSLEEP | -1,-1 | Chưa gán. Đổi theo chân thực; mỗi driver một pin riêng, pull-down phần cứng |
| Servo trái/phải | 40,41 | MCPWM group1, timer0, 2 generator/comparator, 50 Hz |
| LCD SCLK/MOSI/DC/CS/RST | 12,13,14,15,16 | SPI2 master 20 MHz, DMA |
| LCD BL | -1 | Chưa xác nhận phần cứng/điện áp/active level; không nối trực tiếp tải BL vào GPIO |
| I2S BCLK / WS | 17,18 | Một master I2S0 dùng chung RX/TX, 16 kHz, 2×32-bit slots |
| INMP441 SD | 21 | DIN ESP32; L/R chọn mic_right |
| MAX98357A DIN | 39 | DOUT ESP32 |
| Battery ADC | 1 | ADC1 channel0; mặc định tắt, chỉ bật khi có chia áp |

Mapping này là **đề xuất chưa xác minh board thật**. N16R8 dùng Quad flash/Octal PSRAM, khác N16R8V (WROOM-2) có cấu hình bộ nhớ khác. Không dùng cấu hình này cho N16R8V khi chưa kiểm tra.

Validator từ chối pin trùng, không phải output hợp lệ, flash/PSRAM 26–37, USB19/20, strapping0/3/45/46, UART43/44 và GPIO42. GPIO39/40/41 chiếm pad JTAG; dùng USB JTAG thay vì bộ JTAG ngoài. Không giả định tất cả GPIO có mặt trên header DevKit.

Không thể bảo vệ giai đoạn ROM/reset bằng phần mềm. Với module có nSLEEP, kéo xuống phần cứng và chỉ cho ESP nâng mức khi được arm. Với module nSLEEP cố định, cần pull-down trên 8 IN và xác nhận mạch driver thực; nếu board có kéo lên, sửa mạch trước khi cấp VM. Trong lúc firmware khởi động, tất cả IN và tín hiệu servo được đưa thấp.

Nguồn tham khảo: [ESP32-S3 GPIO](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/peripherals/gpio.html), [ESP32-S3-WROOM-1 datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf), [DRV8833 datasheet](https://www.ti.com/lit/ds/symlink/drv8833.pdf).
