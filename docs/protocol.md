# Giao thức local API v1

Mọi WebSocket message là text JSON ≤1024 byte, phải có `v:1`, `seq` integer1..2147483647 tăng nghiêm ngặt trên mỗi kết nối và `type`. Không chấp nhận key lạ, key trùng, số không hữu hạn, sai kiểu, NUL, độ sâu quá8 lớp hoặc JSON dư sau object. Không wrap seq; mở kết nối mới để bắt đầu lại. Fragmented text frames không hỗ trợ. Lệnh lỗi/stale không gia hạn motor timeout.

1. `{"v":1,"seq":1,"type":"auth","token":"<64 hex>"}`
2. `{"v":1,"seq":2,"type":"claim"}` — một session duy nhất giữ quyền; session khác bị từ chối.
3. Gửi `heartbeat` mỗi100–250 ms; lease hết sau1 s, sau đó phải claim và arm mới. Heartbeat **không** gia hạn timeout chuyển động.
4. `{"v":1,"seq":3,"type":"arm","confirm":"WHEELS_LIFTED"}` — cần hardware_confirmed và không lỗi/ngủ/pin nguy cấp.
5. `{"v":1,"seq":4,"type":"drive","throttle":0.5,"steering":-0.2,"speed":0.5}`. t/s∈[-1,1], speed∈[0,1] mặc định1; mix normalize rồi nhân motor_limit×speed. Gửi mới khoảng100 ms khi giữ joystick. Thả phải gửi0 ngay; UI tiếp tục gửi0 khi còn arm và tab đang hiển thị. Blur/ẩn tab gửi disarm và ngừng heartbeat; không giữ lại target cũ.

| type | Trường bổ sung | Hành vi |
|---|---|---|
| motors | values:[lf,lr,rf,rr] mỗi giá trị[-1,1] | PWM riêng, nhân motor_limit |
| disarm | không | Dừng ngay theo chu kỳ control và khóa cả motor/servo |
| estop | không | Client đã xác thực nào cũng được gửi, không cần claim; chốt E-STOP |
| reset_estop | confirm:"WHEELS_LIFTED" | Chỉ reset về DISARMED |
| release | không | Dừng, khóa, trả controller lease |
| arms | left_deg, right_deg | Phải nằm trong giới hạn từng tay, servo đã bật |
| servo_enable | enabled:bool, confirm:"ARMS_CLEAR" khi bật | Không auto bật lúc boot |
| expression | value: tên biểu cảm lowercase | normal/happy/sad/angry/surprised/blink/sleep/listening/thinking/speaking/error/low_battery |
| look | x,y∈[-1,1] | Nhìn ngang/dọc |
| eyes_options | auto_blink:bool, brightness:integer0..100 | Độ sáng pixel, không đèn nền PWM |
| audio | action,volume:integer0..100 | capture_start/capture_stop/tone/stop |
| sleep | enabled:bool | Ngủ dừng actuators, thức không tự arm |
| test | name,confirm:"WHEELS_LIFTED" | Các lệnh test_* trong test_plan |

ACK: `{"v":1,"type":"ack","seq":4,"ok":true,"result":"ESP_OK"}`. `ok` chỉ có nghĩa là lệnh phần mềm được chấp nhận; không xác nhận chuyển động hay âm thanh vật lý. UI đọc telemetry qua HTTP mỗi500 ms. Khi quá400 ms không nhận **lệnh drive/motors hợp lệ** sẽ dừng và disarm; không chạy lại dù heartbeat còn sống.

Tất cả `/api/*` cần header `Authorization: Bearer <token>`. Không CORS, không có token trong query string. Static HTML/JS/CSS công khai trong AP nhưng không có quyền điều khiển.

| API | Nội dung |
|---|---|
| GET /api/status | telemetry JSON, heap/reset/state/audio/battery/module errors |
| GET /api/settings | cấu hình pending đã lọc password/token |
| POST /api/settings | patch JSON≤4096 byte; release controller và stop toàn bộ trước; validate rồi NVS commit; cần reboot |
| POST /api/reboot | disarm → chờ300 ms → restart; không nhận motion sau khi chuẩn bị reboot |
| POST /api/factory-reset | thêm X-Confirm: FACTORY_RESET; reset rồi reboot; tạo lại token/AP password |
| GET /api/audio | PCM16 LE mono16kHz, block≤512 samples, nonblocking; body rỗng nếu chưa có mẫu |
| POST /api/pcm | PCM16 LE mono16kHz body≤1MiB; phải release và stop |
| POST /api/wav | header44 byte chuẩn RIFF/WAVE/fmt16/data và PCM16 mono16kHz; không chunk phụ |

Ví dụ tone kiểm tra cần auth/claim và `audio_enabled=true`. Audio thu/phát cần kiểm tra gain, slot, nguồn và phản hồi âm học. HTTP chưa TLS: chỉ LAN tin cậy, không sử dụng làm service Internet.

## GUI USB — firmware 2.1.0

Gửi từng dòng `gui ` + JSON v1 + newline qua USB Serial/JTAG (115200). Phản hồi bắt đầu bằng `GUI `, sau đó là JSON. GUI bỏ qua các dòng console/log thông thường và không lưu thông tin ghép nối. Console `pair`, `status`, `stop` và các lệnh chẩn đoán cũ vẫn dùng được.

1. `gui {"v":1,"seq":1,"type":"usb_hello"}` mở phiên USB vật lý, trả `type:"hello", transport:"usb"`. Không tự claim hay arm. Mở lại một phiên USB đang giữ quyền sẽ disarm và trả quyền cũ.
2. `usb_status` trả telemetry, thêm `usb_controller`, `controller_transport`, `motor_limit`, `lcd_enabled`, `audio_enabled` và `servo_limits` của cấu hình đang chạy.
3. `usb_settings_get` trả `type:"settings", settings:{...}` với cấu hình pending đã lọc secrets.
4. `usb_settings_patch` thêm `patch:{...}`; chỉ lưu khi đã trả controller, stop motor/servo và cách lần lưu trước ít nhất 3 s. Dùng cùng validate/transaction/maintenance với HTTP.
5. `usb_reboot` khóa hệ thống rồi khởi động lại.

Sau hello, mọi lệnh điều khiển `claim`, `heartbeat`, `arm`, `drive`, `disarm`, `release`, `estop`, servo/mắt/audio dùng đúng schema WebSocket ở trên và cùng chuỗi seq tăng nghiêm ngặt. Không cần auth token trên cổng USB vật lý; điều này không cấp quyền cho bất kỳ WebSocket nào. Các lệnh `usb_*` bị parser WebSocket từ chối.

Cả USB và WebSocket được dispatch trên task HTTPD, dùng một controller lease và cùng trạng thái cấu hình. Timeout motor/lease của safety task hoạt động độc lập. Hàng đợi USB tối đa 8 lệnh chờ, khung JSON tối đa 1024 byte. Console loại toàn bộ dòng tràn hoặc chứa NUL đến newline, không diễn giải phần đuôi thành lệnh mới. Chi tiết GUI: [desktop_gui.md](desktop_gui.md).

## AI Wi-Fi độc lập — firmware 2.2.0

- `usb_settings_get` thêm `ai_settings:{enabled,key_configured,model,voice,vad_threshold}` của cấu hình pending. Không trả key.
- `usb_ai_config` nhận `config:{enabled,api_key?,model?,voice?,vad_threshold?}`. `api_key` bỏ qua nghĩa là giữ key hiện tại; chuỗi rỗng xóa key và chỉ hợp lệ khi AI tắt. Key/model/voice phải vượt qua validate trước khi ghi NVS. Chỉ USB vật lý, đã hello, seq tăng, trả controller, motor/servo dừng, cách lần lưu AI trước ít nhất 3 s. Lưu xong khóa bảo trì và cần reboot; không tự bật AI bằng ACK.
- `usb_ai_pause` nhận `paused:bool`. Chỉ khi không có controller và actuators dừng. Không được resume khi có cấu hình đang chờ reboot. Chỉ thay đổi trạng thái hiện tại; thiết lập tự nghe sau boot được lưu riêng bằng `usb_ai_config`.
- Telemetry `ai` thêm trạng thái, mã lỗi đã lọc, câu trả lời gần nhất (tối đa 1536 byte UTF-8), số lượt trong giờ và thời gian đợi thử lại. Không có key hoặc nội dung lỗi thô của dịch vụ bên ngoài.
- Các lệnh AI provisioning vẫn bị parser WebSocket từ chối; không mở HTTP endpoint ghi key. `/api/audio` và `/api/pcm`/`wav` từ chối truy cập thủ công khi AI đang sở hữu audio. Nhận controller sẽ tạm dừng AI và đóng phiên hội thoại; trả quyền cho phép AI bắt đầu phiên mới nếu không có pause/bảo trì/safety chặn.

ESP32 tự dùng Realtime WebSocket TLS đến OpenAI, không chuyển audio qua giao thức GUI USB. Chi tiết khởi tạo và giới hạn ở [ai_voice.md](ai_voice.md).
