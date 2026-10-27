# Cấu hình

GPIO sửa trong `BOARD`, tất cả tùy chọn hành vi lưu struct schema2 trong namespace NVS `walle`, key `settings_v2`. Dữ liệu sai schema/size/range được thay bằng mặc định an toàn và credentials mới; không tự xóa toàn bộ NVS khi partition bị lỗi.

| Nhóm | Giá trị mặc định / giới hạn |
|---|---|
| hardware_confirmed | false; khóa motor/servo cho đến khi chủ robot xác nhận mapping/cơ khí |
| motor_limit | 0.25; range0.05..0.8, 1.0 tương đương100% lý thuyết nhưng cấm full duty bằng cấu hình an toàn |
| motor_trim[4] / invert[4] | 1.0 / false, trim0.5..1.5; sau trim tiếp tục clamp motor_limit |
| pwm_hz | 20000, range18000..20000 |
| accel / decel | 0.6 / 1.5 duty fraction mỗi giây |
| timeout_ms / reverse_dead_ms | 400 (300..500) / 100 (20..1000) |
| servo mỗi bên | min60°, max120°, home90°, speed30°/s; pulse1200..1800µs |
| servo pulse / invert | min500..max2500µs, min<max; tuyến tính trên khoảng min_deg..max_deg, invert phản chiếu trong khoảng đó |
| lcd_enabled | false, chỉ bật sau xác nhận module |
| lcd_width/height | native76×284, rotation1→logical284×76; giới hạn native240×320 |
| lcd_x_offset/y_offset | native0/0 chưa kiểm chứng; cộng kích thước không vượt RAM240×320 |
| lcd_rotation / bgr / invert | 1 / false / false; chỉnh theo RGB/border test |
| brightness / auto_blink | 80% / true |
| audio_enabled / mic_right | false / false; left slot mặc định |
| volume | 20%,0..100%; gain số≤1 để không tạo clipping thêm |
| battery_enabled | false, không báo V/% khi thiếu mạch |
| battery_ratio / low / critical | 4.0 / 7.0V / 6.6V; phải hiệu chuẩn theo chia áp thật và cell/BMS |
| wifi_ssid / wifi_password | rỗng; STA chỉ dùng mạng WPA2 trở lên; password8..63 ký tự khi đặt |

Góc servo mặc định **không phải xác nhận cơ khí an toàn**. Khi bật servo lần đầu, góc cơ khí thật không biết nên vẫn có thể nhảy về home; tháo horn/giảm tải để hiệu chuẩn trước. Không giả định180°.

API GET không trả password/token. POST có thể thêm `wifi_password` để cập nhật; không có key này thì giữ nguyên. Chỉnh SSID rỗng để chỉ dùng AP. Xóa mật khẩu bằng chuỗi rỗng chỉ khi cấu hình phù hợp; STA vẫn không kết nối AP mở vì authmode tối thiểu WPA2. Không sửa schema qua patch. Mỗi lần lưu cách nhau tối thiểu3 giây; không tự lưu khi trượt slider. Sau lưu tất cả lệnh arm/test bị khóa cho đến reboot. Factory reset đổi cả AP password và token; dùng `pair` qua USB để lấy lại.
