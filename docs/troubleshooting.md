# Xử lý lỗi

| Triệu chứng | Kiểm tra |
|---|---|
| Build lần đầu rất lâu | PlatformIO đang tải toolchain; chờ tiến độ thực. Kiểm tra Internet/proxy/dung lượng. Không upload trước khi build SUCCESS |
| IDF_PATH hoặc đường dẫn có khoảng trắng | Dự án đã dùng packages_dir=.pio/packages để tránh dấu cách trong user profile; giữ cả đường dẫn dự án không có dấu cách |
| Không thấy Serial | Kiểm tra USB native vs UART bridge, cáp dữ liệu, driver/cổng COM; console mặc định USB Serial/JTAG |
| Bootloader không nhận | Ngắt tải motor/servo, dùng BOOT+RESET theo board, kiểm tra19/20 không bị nối linh kiện khác; đúng COM |
| PSRAM error | Đúng N16R8 WROOM-1, Octal PSRAM/Quad flash, cấp nguồn; không dùng cấu hình này cho N16R8V chưa kiểm tra |
| Motor không chạy | hardware_confirmed, claim, xác nhận wheels clear, arm, drive mới mỗi100ms, hết timeout phải arm lại; VM3V, GND, nSLEEP, dây, motor_limit |
| Motor tự chạy lúc reset | Dừng thử, ngắt VM; phần cứng thiếu pull-down/nSLEEP hoặc jumper kéo lên; firmware không bảo vệ thời gian ROM boot |
| Servo đứng yên | Cần servo_enable riêng với ARMS_CLEAR, không tự có pulse lúc boot; đo nguồn5V, GPIO40/41, min/max pulse và cơ khí |
| E-STOP không mở khóa | reset_estop có xác nhận, không critical battery/driver fault; sau reset cần arm riêng. Fault driver phải sửa và reboot |
| LCD trắng/lệch/cắt/đảo màu | Enable LCD sau kiểm tra nguồn, DC/CS/RST, kích thước thật76×284, offset native, rotation/BGR/invert; chạy RGB/border test |
| Không có mic/âm thanh | audio_enabled, L/R, SD21/DIN39, chung17/18, cùng standard32-bit stereo16kHz; SPK không GND, nguồn ampli/SD/GAIN đúng module |
| PCM toàn0 | Không kết luận mic OK: kiểm tra capture bật/slot L/R/wiring, peak thực khi nói |
| Thu âm đứt/mất mẫu | Theo dõi dropped_samples, giảm tải Wi-Fi/LCD, dùng AP gần, không kỳ vọng streaming thời gian thực qua browser luôn không mất mẫu |
| Pin unavailable | Đúng khi battery_enabled=false/chưa có mạch; không nối trực tiếp pin2S. Enable chỉ sau chia áp và ADC calibration |
| Settings save bị chặn | Nhấn Trả quyền, motor+servo đều khóa, chờ≥3s giữa lần save; reboot để áp dụng |
| Mất token/AP password | Gõ pair qua console USB vật lý; factory_reset CONFIRM tạo credentials mới |
| NVS partition lỗi | Không tự erase toàn flash. Ngắt tải, sao lưu nếu có thể, sửa/erase NVS có chủ ý qua công cụ chính thức rồi cấu hình lại |
| ESP brownout/reset | Đo5V/3V3 khi servo/loa/Wi-Fi chạy; nguồn/cáp/mass/tụ, không vô hiệu brownout để che lỗi nguồn |

Không phát log pairing lên mạng. Xóa các terminal capture chứa token/mật khẩu nếu chia sẻ log lỗi.
