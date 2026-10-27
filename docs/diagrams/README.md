# Hình đấu nối WALL-E Mini

Hai hình được tạo theo GPIO đang có trong `components/board_config/board_config.c` ngày 2026-10-16, bằng công cụ **image_gen tích hợp**. Đây là sơ đồ kết nối logic theo tên GPIO, không phải thứ tự chân header của một DevKit cụ thể. Chưa kiểm chứng trên phần cứng thực tế.

- [Tờ 1 — Dây tín hiệu](walle-signal-wiring-v1.png)
- [Tờ 2 — Cấp nguồn và mạch đo pin tùy chọn](walle-power-wiring-v3.png)
- [Prompt và các lần hiệu chỉnh](image-prompts.md)
- [Hiệu chỉnh cuối ký hiệu buck](final-correction-prompt.md)

## Cách đọc

Nhãn nguồn giống nhau nghĩa là nối cùng một đường điện. GPIO là tên chân in trên board; xác nhận pinout của DevKit trước khi nối. GND của các thiết bị về mass chung ở **đầu ra được bảo vệ của BMS**, không nối tắt qua cực âm cell. Xem thêm [hướng dẫn đấu dây](../wiring.md).

| Thiết bị | Dây tín hiệu từ ESP32-S3 | Nguồn / đấu tải |
|---|---|---|
| DRV8833 #1, kênh A — motor trái trước LF | GPIO4 → AIN1; GPIO5 → AIN2 | AOUT1/AOUT2 → hai cực motor LF |
| DRV8833 #1, kênh B — motor trái sau LR | GPIO6 → BIN1; GPIO7 → BIN2 | BOUT1/BOUT2 → hai cực motor LR |
| DRV8833 #2, kênh A — motor phải trước RF | GPIO8 → AIN1; GPIO9 → AIN2 | AOUT1/AOUT2 → hai cực motor RF |
| DRV8833 #2, kênh B — motor phải sau RR | GPIO10 → BIN1; GPIO11 → BIN2 | BOUT1/BOUT2 → hai cực motor RR |
| Hai DRV8833 | nSLEEP chưa gán GPIO (`-1`) | VM từ buck 3 V; GND chung; kiểm tra mạch nSLEEP của module |
| Servo trái / phải | GPIO40 / GPIO41 → SIG | V+ từ buck 5 V, GND chung |
| LCD ST7789 | GPIO12 → SCLK; 13 → MOSI; 14 → DC; 15 → CS; 16 → RST | GND chung; VCC và BL cần xác nhận module trước khi nối |
| INMP441 | GPIO17 → SCK; 18 → WS; SD → GPIO21 | VDD 3,3 V từ ESP, GND chung; L/R → GND cho kênh LEFT mặc định |
| MAX98357A | GPIO17 → BCLK; 18 → LRC; 39 → DIN | VIN từ buck 5 V theo thông số module, GND chung; SD/GAIN theo module |
| Loa 4 Ω / 3 W | Qua MAX98357A | Hai cực loa vào SPK+ và SPK−; không cực nào nối GND |
| Đo pin tùy chọn | Điểm giữa chia áp → GPIO1 / ADC1_CH0 | 100 kΩ phía trên, 33 kΩ xuống GND, 100 nF từ ADC xuống GND; không đưa 2S trực tiếp vào GPIO |

GPIO17 và GPIO18 là hai đường clock dùng chung cho mic và ampli; hai GPIO này không được nối chập với nhau. Mỗi motor dùng một cầu H riêng như bảng; không ghép song song các đầu ra motor trong cấu hình này.

## Những điểm còn cần xác nhận

- **DevKit:** tên chân nguồn 5V/VIN, GPIO có thực sự được đưa ra header, và mạch ghép nguồn USB với buck. Không đưa nguồn 5 V vào chân 3V3.
- **LCD:** điện áp VCC, mức logic, cách cấp đèn nền BL và cực tính. Firmware đang để `lcd_bl = -1`; hình không chỉ định điện áp cho LCD.
- **DRV8833:** firmware để hai chân nSLEEP là `-1`. Kiểm tra jumper/pull-up/pull-down của module; chưa có điều khiển sleep bằng GPIO. Hình không thể hiện một mạch motor đã sẵn sàng chạy khi điểm này chưa được giải quyết. Xem [GPIO mapping](../gpio_mapping.md).
- **Mạch đo pin:** chỉ là ví dụ tùy chọn. Với 100 kΩ / 33 kΩ, tỷ số lý thuyết là `4.0303`; điện áp ADC ở pin 8,4 V khoảng `2.085 V`. Đo và hiệu chuẩn trước khi bật `battery_enabled`; đặt `battery_ratio` theo tỷ số thực, không giữ mặc định `4.0` nếu sai với mạch.

LCD, audio và đo pin mặc định tắt; `hardware_confirmed` mặc định false. Hình đấu dây không thay đổi các thiết lập này. Chỉnh và đo đầu ra buck 3 V / 5 V khi chưa nối tải; ngắt pin và USB khi đổi dây.

## Tài liệu kỹ thuật đối chiếu

- [TI DRV8833 datasheet — chân VM, IN, OUT, nSLEEP](https://www.ti.com/lit/ds/symlink/drv8833.pdf).
- [Analog Devices MAX98357A/MAX98357B datasheet — nguồn và ngõ ra loa](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98357A-MAX98357B.pdf).

Đặc tính IC không xác nhận toàn bộ mạch của module thương mại. Đối chiếu nhãn và sơ đồ module thực trước khi cấp điện.
