# Kiến trúc firmware

`app_main` đưa pin về LOW → NVS/schema → kiểm tra PSRAM → motor/servo drivers (output vẫn tắt) → safety/control task → LCD → audio → ADC nếu bật → Wi-Fi/AP/STA → HTTP/WebSocket → console. Lỗi NVS hoặc pin dừng boot với IN thấp. Lỗi actuator khóa CORE_FAULT. Module tùy chọn lỗi vẫn cho chẩn đoán qua mạng; không báo READY cho module lỗi.

| Task / event | Ưu tiên | Vai trò / đồng bộ |
|---|---|---|
| control_safety, 5 ms hoặc notification | 10 | Chủ duy nhất LEDC/MCPWM sau init; ramp, timeout, E-STOP, driver errors, servo slew; watchdog5 s |
| audio_duplex | 4 | Chủ duy nhất I2S RX/TX, DMA blocks128 frames; bounded PCM queue và ring buffer |
| battery, 500 ms | 3 | ADC1 calibrated, lọc điện áp, 3 mẫu thấp liên tiếp để cảnh báo/cắt |
| web_lease, 200 ms | 3 | Gửi lease check đến task HTTPD; không trực tiếp truy cập session |
| httpd (ESP-IDF) | mặc định5 | HTTP/WebSocket event-driven, validate/ACK, sở hữu session và controller lease |
| display, 40 ms + SPI | 2 | Chủ duy nhất SPI2, framebuffer + DMA scratch8 dòng, animation độc lập motor |
| usb_console | 1 | Provisioning vật lý và diagnostics được yêu cầu rõ ràng |
| Wi-Fi/event loop/esp_timer (ESP-IDF) | SDK | Reconnect bằng timer, status mutex; mất kết nối yêu cầu disarm |

Không pin task lên core cụ thể: để scheduler phân phối trước, đo jitter bằng logic analyzer rồi mới tối ưu affinity. Task priority không thay thế nguồn ổn định hay bảo vệ driver. Không có xử lý mạng/UI trong ISR. Không busy-wait; dùng DMA, queue và block/yield có timeout.

Trạng thái song song: safety có DISARMED/ARMED/ESTOP/FAULT; output robot suy ra BOOTING/INITIALIZING/IDLE/MOVING/SLEEPING/LOW_BATTERY/ERROR/EMERGENCY_STOP, audio có IDLE/LISTENING/SPEAKING, connectivity là AP/STA state riêng. Servo có enabled và góc commanded; giá trị không phải phản hồi cơ khí. Motor chỉ chạy khi arm, hardware_confirmed, drivers sẵn sàng, không ngủ, không critical battery. E-STOP phải reset rồi arm riêng. Driver fault không được reset mềm thành READY, phải reboot và kiểm tra.

Control dữ liệu nhỏ được bảo vệ bằng `portMUX` (không cấp phát hay làm I/O mạng/NVS/SPI trong critical section). Các lệnh E-STOP/disarm cập nhật cờ + notification, task control áp dụng output; không chờ HTTP heartbeat. LEDC/MCPWM cập nhật trong cùng critical section với kiểm tra safety để không bật lại do stale snapshot. Đo độ trễ thực trên target là bắt buộc; con số5 ms chỉ là chu kỳ thiết kế.

Settings load một lần, immutable khi chạy. API lưu bản pending vào NVS chỉ khi không có controller và mọi actuator đã khóa; sau lưu phải reboot mới áp dụng. Không ghi flash theo telemetry/heartbeat. Schema thay đổi cần migration hoặc phục hồi mặc định an toàn; NVS phân vùng bị hỏng không tự xóa toàn bộ.

LEDC ESP32-S3 có8 kênh: dùng cả8 cho 4 motor IN/IN, timer0 20kHz/10bit. Servo dùng MCPWM group1 một timer1MHz/period20000 và hai comparator/generator, tránh cạnh tranh LEDC. Coast00, brake11, nSLEEP0 tắt bridge nếu thực sự nối. Có thời gian wake2 ms không chặn, reversal deadtime mặc định100 ms sau khi output về0.

Audio dùng I2S0 **new channel API**, một allocation TX+RX, cùng config stereo Philips32bit/16kHz và cùng BCLK/WS. INMP24bit MSB nằm trong slot32bit, lấy high16 về PCM16, chọn slot L/R; TX nhân PCM16 bằng65536 và lặp hai slot cho MAX98357A. Không trộn legacy `i2s_driver_install`. RX ring8192 sample≈16KiB; queue TX12×128 PCM16≈3KiB; DMA cố định. Chỉ audio task đụng I2S.

Từ bản 2.2, component `voice_assistant` thêm task `voice_ai` ưu tiên2, stack18KiB, dùng PSRAM khoảng2,54MB cho câu hỏi12s và trả lời45s, cùng buffer JSON128KiB. Task này dùng PCM API, không truy cập I2S hay actuators trực tiếp. VAD local thu từng câu; kết nối Realtime WebSocket TLS có CA/hostname và SNTP, PCM16/24kHz qua resampling. Mic tắt trong lúc gửi/nhận/phát. Một session giữ ngữ cảnh đến khi mạng/controller/safety làm gián đoạn. Controller có quyền ưu tiên audio; epoch hủy tác vụ cũ khi đổi trạng thái. Cấu hình AI lưu riêng `voice_v1`, write-only API key qua USB, không thay schema motor/servo cũ. Chi tiết [ai_voice.md](ai_voice.md).

Frame LCD theo kích thước đã cấu hình, mặc định284×76≈43KiB; scratch DMA284×8×2≈4,5KiB. Không làm rendering trên control task. Per-frame cấp phát =0. JSON/HTTP cấp phát hữu hạn trên task mạng, giới hạn packet1024 byte và settings4096 byte; WAV streaming tối đa1MiB, không giữ toàn bộ tệp. Khi upload phải trả quyền và khóa actuators vì HTTPD handler streaming chiếm task mạng.

Diagnostics cho free/min heap, largest internal block, PSRAM, reset reason và stack free của task control. Kiểm tra các task còn lại bằng debugger/FreeRTOS tools trong stress test. Log UI40 dòng; modules cache20 phần tử; không log token/password qua ESP_LOG. Lệnh `pair` chỉ hiển thị thông tin riêng khi được yêu cầu trên USB vật lý.
