# Kế hoạch kiểm thử

Tất cả thử motor lần đầu: robot được kê chắc, bánh xích không chạm bàn/sàn, có công tắc nguồn vật lý trong tầm tay. Chưa thực hiện checklist nào trên phần cứng trong phiên này.

## Tự động trên máy phát triển

`tests/run_host_tests.py` biên dịch logic C production + cJSON chính thức thành executable host rồi chạy assertions: mixing/normalize, inversion/trim/duty clamp, ramp, reversal deadtime kể cả stop rồi reverse, servo pulse clamp/invert, millis rollover/timeout, state transitions, JSON strict validation, schema/config transaction và WAV format. Tiếp đó chạy JavaScript production với DOM/WebSocket adapters: thả joystick gửi0, không giữ lệnh khi đã thả, blur disarm, xác nhận arm, hidden tab không heartbeat, reconnect không tự arm. Không thay logic production bằng bản Python mô phỏng.

Runner cũng chạy mã task safety và driver motor production với adapter clock/LEDC/GPIO/FreeRTOS trên host. Các adapter ghi nhận lệnh phần mềm, không mô phỏng hoặc chứng nhận chuyển động thật. Kết quả thực tế cuối phiên trong `verification.md`.

## Chẩn đoán từng module

USB: gõ tên lệnh + ` CONFIRM` rồi Enter. Web: nhận quyền, vào Hệ thống→diagnostics, xác nhận bánh xích/tay an toàn. Mỗi lệnh motor chỉ chạy một motor200ms, duty yêu cầu≤15% và chịu ramp; không báo PASS cho cơ khí.

| Lệnh | Quan sát cần làm |
|---|---|
| test_motor_lf / lr / rf / rr | Đúng motor và chiều; đo IN/PWM/VM, nghe kẹt cơ; module không quá nhiệt |
| test_servo_left / right | Yêu cầu +2° nếu trong giới hạn; tháo tải, xác nhận đúng bên và chiều; vị trí báo là commanded |
| test_lcd | RGB bars, viền đủ bốn cạnh, không lệch/cắt, đúng284×76 ngang |
| test_microphone | Ring mẫu tăng, nói vào mic→peak thay đổi, lấy PCM nghe lại; im lặng/mất mic không được coi là PASS |
| test_speaker | Tone nghe rõ, không clipping/reset; chỉnh volume thấp trước |
| test_wifi | AP/STA/IP thật; kiểm tra ping/HTTP từ máy khác, không chỉ dựa vào driver init |
| test_memory | Heap integrity + PSRAM size thực, không đồng nghĩa stress test hoàn tất |
| test_safety | Chốt E-STOP; đo output ngừng, lệnh mới bị từ chối; reset rồi arm bằng tay |

## Điện/phần cứng

- [ ] Đúng cực pin2S, BMS/cầu chì/công tắc và buck3V/5V; không chập.
- [ ] Đúng GPIO module/header; nSLEEP/IN không kéo lên trong reset; kiểm tra bằng scope cả ROM boot.
- [ ] Không cấp động cơ/servo qua GPIO/3V3; không ghép USB5V với buck sai cách.
- [ ] Motor đúng chiều từng bên; cặp motor cùng xích không chống nhau; dòng/ngưỡng driver đúng datasheet/module.
- [ ] Servo pulse/góc/home không va chạm, không buzz/kẹt, không mặc định180°.
- [ ] LCD đúng nguồn/BL/offset/BGR/invert/rotation; thử đủ12 biểu cảm và hướng nhìn.
- [ ] INMP441 đúng L/R, sign/slot/PCM16, peak/clipping; MAX98357A loa nối SPK+/SPK−, không GND.
- [ ] Battery chia áp đo thật và hiệu chuẩn; khi tắt sensor luôn báo unavailable.
- [ ] Không sụt áp/reset khi hai servo + loa + Wi-Fi tải đồng thời; kiểm tra nhiệt các nguồn/driver.

## Tích hợp và fault injection

- [ ] Motor+Wi-Fi và motor+LCD: đo ramp/PWM không giật khi render/JSON load.
- [ ] Servo+LCD, micro+speaker full duplex, audio+Wi-Fi telemetry: đo drop/error counters, không WDT.
- [ ] Khi không drive≥timeout400ms: motor về0 và disarm trong chu kỳ control kế; heartbeat vẫn gửi không được duy trì chuyển động.
- [ ] Đóng tab, tắt Wi-Fi thiết bị, rút router, kill trình duyệt: motor dừng, reconnect không tự chạy; cần claim/arm/drive mới.
- [ ] E-STOP trong ramp/servo/audio, sau đó drive/arm bị từ chối; reset E-STOP không tự arm.
- [ ] Hai trình duyệt tranh quyền: chỉ một controller, client khác nhận lỗi; E-STOP từ client xác thực khác vẫn được xử lý.
- [ ] Token sai, auth thiếu, JSON thiếu key/key trùng/sai kiểu/NaN/seq cũ/packet lớn/fragment bị từ chối và không gia hạn timeout.
- [ ] Chỉnh settings sai bị từ chối toàn bộ; không thể save khi giữ quyền/motor/servo chạy; reboot áp dụng đúng; không lộ password trong GET/log.
- [ ] Upload WAV lúc có controller bị từ chối; định dạng lạ/metadata/size lẻ/over1MiB bị từ chối, không allocation toàn file.
- [ ] Pin thấp/critical, ADC hỏng, lỗi PWM task, mất PSRAM/NVS: trạng thái lỗi phản ánh thực; không báo pin/motor PASS giả.

## Stress 30–60 phút

Chạy theo mức tải tăng dần sau khi các mục an toàn đạt. Kết nối/ngắt Wi-Fi nhiều lần, giữ lệnh100ms, đổi biểu cảm liên tục, hai servo luân phiên trong giới hạn, tone/WAV cùng telemetry, theo dõi min heap/largest block/PSRAM, stack high-water các task (debugger hoặc telemetry control), io_errors/dropped_samples/WDT/reset reason, nhiệt/điện áp/dòng. Lưu log đo thực không chứa pairing secret. Sau chạy kiểm tra E-STOP và timeout lại. Không đánh dấu hoàn tất trước khi có bản ghi và thiết bị thật.
