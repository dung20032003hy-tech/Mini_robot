# Bản ghi kiểm chứng

Ngày triển khai: 2026-10-16 (Asia/Saigon).

Cập nhật 2026-10-19: đã nạp firmware vào ESP32-S3 thật qua COM6 và xác minh hash dữ liệu ghi. Xem bản ghi bổ sung cuối tài liệu; các kết quả dưới đây mô tả lần kiểm chứng ban đầu ngày 2026-10-16.

- Firmware build: **PASS**, lệnh PlatformIO `run` thực tế trả exit code0 và `[SUCCESS]`. Lượt cuối mất19,99s sau khi các thành phần SDK đã được biên dịch.
- Target: custom board `esp32-s3-n16r8`, Xtensa ESP32-S3 240MHz, Quad Flash16MB, Octal PSRAM80MHz; cấu hình sinh ra xác nhận `CONFIG_ESPTOOLPY_FLASHSIZE_16MB`, `CONFIG_SPIRAM_MODE_OCT`, WebSocket và watchdog5s.
- Phiên bản đã build: PlatformIO platform6.12.0; ESP-IDF5.5.0/package3.50500.0; GCC14.2.0+20241119; CMake3.30.2; Ninja1.9.0; esptool4.9.0.
- RAM tĩnh:56.616/327.680 byte (17,3%). Không phải số đo heap còn trống khi robot đang chạy.
- Flash app:963.819/4.194.304 byte của phân vùng app (23,0%); tổng Flash chip được cấu hình16MB.
- Host C logic tests: **PASS** (mix/normalize, inversion/trim/limits, ramp/deadtime, servo bounds, millis rollover, state transitions, seq, JSON malformed/deep/NUL/duplicate/unknown fields, settings transaction, WAV format).
- Production safety task với clock/hardware adapters trên host: **PASS** (safe boot, E-STOP giữa motor test, chốt/reset, command timeout dù heartbeat còn, lease timeout độc lập HTTP, servo stop, disconnect, sleep, low battery, maintenance, fault latch).
- Production motor driver với LEDC/GPIO adapters trên host: **PASS** (mapping, nSLEEP/wake, polarity, invalid duty/NaN, static brake/coast, sleep, trim/inversion clamp).
- JavaScript production qua QuickJS: **PASS** (thả joystick chỉ còn target0, blur/disarm, xác nhận arm, hidden tab, reconnect không tự arm, WAV construction).
- Static Web assets: **PASS** (ID không trùng/không thiếu, mọi ID tĩnh trong JavaScript tồn tại, CSS/JS có đủ).
- Lệnh host test thực tế: `python tests/run_host_tests.py`, exit code0; log lưu tại `.pio/host-tests/results.txt`.
- Browser rendering: chưa có browser automation trong môi trường, chưa kiểm chứng trên trình duyệt thật.
- Flash ESP32: chưa thực hiện.
- Toàn bộ đo điện, motor/servo/LCD/audio/Wi-Fi/ADC trên target, integration và stress: **Hardware verification pending**.

## Artifact đã tạo

| File | Kích thước | Offset khi flash |
|---|---|---|
| `.pio/build/esp32-s3-n16r8/bootloader.bin` |22.560 byte|0x0|
| `.pio/build/esp32-s3-n16r8/partitions.bin` |3.072 byte|0x8000|
| `.pio/build/esp32-s3-n16r8/firmware.bin` |964.224 byte|0x10000|

Nên dùng PlatformIO Upload để công cụ tự chọn đúng các tệp/offset; chưa thực hiện flash trong phiên này. `firmware.bin` là app image, không phải ảnh ghép để ghi ở0x0.

SHA-256 của firmware.bin cuối phiên:

```text
B0E0DEA375D9536AAFED07F8A337B980F2E3ACC4FE443FDAFD3930D3500D996E
```

`esptool.py --chip esp32s3 image_info .../firmware.bin` đã chạy thực tế, exit code0, checksum và validation hash **valid**. Điều đó xác nhận cấu trúc image, không xác nhận phần cứng.

Package framework nằm trong `.pio/packages` để tránh dấu cách ở tên tài khoản Windows. Có cả CMake embed declaration và `board_build.embed_files` để build hoạt động bằng ESP-IDF native lẫn PlatformIO.

Nguồn API dùng để triển khai: ESP-IDF5.5 official drivers MCPWM/LEDC/I2S/ADC/SPI/HTTPD và datasheet DRV8833. Package5.5.0 được khóa; không trộn Arduino/legacy I2S.

- [ESP-IDF5.5 MCPWM](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/peripherals/mcpwm.html)
- [ESP-IDF5.5 I2S](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/peripherals/i2s.html)
- [ESP-IDF5.5 HTTP server](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/protocols/esp_http_server.html)
- [PlatformIO6.12.0 package manifest](https://github.com/platformio/platform-espressif32/blob/v6.12.0/platform.json)
- [TI DRV8833 datasheet](https://www.ti.com/lit/ds/symlink/drv8833.pdf)

## Nạp board thật — 2026-10-19

- Cổng ban đầu COM5 không trả lời giao thức nạp. Sau thao tác BOOT + RESET, thiết bị xuất hiện ở **COM6**, USB Serial/JTAG.
- Truy vấn ROM thực tế: **ESP32-S3 QFN56 revision v0.2**, Flash **16 MB Quad / 3,3 V**, embedded PSRAM **8 MB (AP_3v3)**, crystal 40 MHz. Đây là thông tin nhận dạng chip, chưa phải kiểm thử PSRAM trong ứng dụng.
- Build lại: **PASS**, PlatformIO `run -e esp32-s3-n16r8`, exit code 0, 12,27 s. App 963.819 byte, RAM tĩnh 56.616 byte.
- Upload: **PASS**, `run -e esp32-s3-n16r8 -t upload --upload-port COM6`, exit code 0, 21,40 s.
- Đã ghi bootloader tại `0x0`, bảng phân vùng tại `0x8000`, app tại `0x10000`; công cụ báo `Hash of data verified` cho cả ba ảnh. Không thực hiện erase toàn bộ chip.
- Sau khi người dùng thả BOOT và bấm RESET/EN vật lý, console COM6 trả lời `status`: firmware `2.0.0`, ESP-IDF `5.5.0`, uptime 13 s, trạng thái `IDLE`, reason `Ready; actuators disarmed`, `armed=false`, `servo_enabled=false`, cả bốn motor command bằng 0.
- Chẩn đoán trên chip: NVS, PSRAM, motor/servo driver, Wi-Fi và Web trả `ESP_OK`. PSRAM khả dụng 8.385.940 byte, heap khả dụng 8.571.116 byte tại thời điểm chụp. Driver motor/servo khởi tạo thành công không xác nhận tải đã hoạt động.
- LCD (`eyes`), audio và battery trả `ESP_ERR_NOT_SUPPORTED` theo thiết lập mặc định tắt; chưa bật các ngoại vi này.
- Lưu snapshot không chứa thông tin ghép nối tại `.pio/hardware-checks/2026-10-19-initial-status.json`, console tại `.pio/hardware-checks/2026-10-19-boot-console.txt`. Chưa thử truy cập Web từ trình duyệt hoặc kết nối Wi-Fi bằng điện thoại/laptop.
- Đã đặt `upload_port=COM6`, `monitor_port=COM6`, baud 115200 trong `platformio.ini` cho laptop hiện tại. Đổi lại nếu Windows gán cổng khác.
- Motor, servo, LCD, microphone, loa, ADC, thử tải nguồn và kiểm thử dừng khẩn trên phần cứng: **chưa kiểm chứng**. Không phát lệnh arm hay lệnh thử chuyển động trong lần nạp này.

## GUI Windows và firmware USB 2.1.0 — 2026-10-19

- Đã tạo `WALL-E-Control.exe`, GUI WPF tiếng Việt kết nối USB, có điều khiển motor/tay/mắt/âm thanh, trạng thái, cấu hình và nút nạp firmware qua PlatformIO. Không cần cài thư viện GUI thêm trên laptop hiện tại.
- Build GUI và kiểm thử desktop: **PASS**, 67 assertions. Kiểm tra bằng client C# thật với adapter cổng: xác nhận/lease/arm, giữ/thả, timeout, lỗi đọc/ghi, UI bị gián đoạn, ACK đến muộn sau stop/claim, giới hạn servo, lưu cấu hình, kết nối lại, nạp XAML và các sự kiện nút/tab thật. Không mở cổng phần cứng hoặc gửi lệnh chuyển động trong bộ kiểm thử này. Log `.pio/gui-tests/results.txt`.
- Bộ kiểm thử firmware C/JS trên host: **PASS**, có bổ sung khung quản trị USB, version/sequence, key trùng/lạ, NUL, kích thước và việc WebSocket không nhận các lệnh `usb_*`. Các nhóm safety/motor/UI cũ tiếp tục PASS.
- Build ESP32-S3 firmware 2.1.0: **PASS**, PlatformIO `run -e esp32-s3-n16r8`, exit code 0. RAM tĩnh 56.664/327.680 byte (17,3%), app 966.343/4.194.304 byte (23,0%). `firmware.bin` dài 966.752 byte.
- SHA-256 bản firmware USB mới: `4417A09504AA81553CDF317E8B569DE6B8CA05F3FDC299ABC8939361D8A01D23`.
- **Chưa nạp firmware USB 2.1.0 lên board trong lần tạo GUI:** người dùng cho biết sẽ cắm board sau. Cần bấm **Cập nhật firmware USB** trước khi kết nối GUI lần đầu. Kết quả boot 2.0.0 ở mục trước không phải kiểm thử bản USB mới.
- Xem [hướng dẫn GUI](desktop_gui.md). Mọi kiểm thử chuyển động, audio/LCD/ADC và tải nguồn vẫn cần thực hiện với phần cứng thật.

## AI giọng nói Wi-Fi độc lập 2.2.0 — 2026-10-21

- Bổ sung component `voice_assistant`: kết nối OpenAI Realtime trực tiếp từ ESP32 qua TLS, SNTP/CA/hostname verification, VAD local, PCM 16/24 kHz, bộ đệm PSRAM giới hạn, nghe/nói luân phiên, trạng thái/câu trả lời, hủy theo controller/safety, giới hạn 30 lượt/giờ và xử lý lỗi không ghi key.
- Thêm trang **Trợ lý AI** trong GUI: Wi-Fi 2.4 GHz, audio/kênh mic, API key qua PasswordBox USB, model/voice/ngưỡng, lưu/reboot, pause/resume và telemetry. API key không đọc lại qua GUI/HTTP; chưa bật NVS encryption.
- Build GUI và kiểm thử desktop: **PASS, 81 assertions**, gồm các kiểm thử cũ và thao tác nút AI thật: cấm cấu hình khi giữ quyền, yêu cầu reboot, nhận diện firmware cũ, xóa các ô mật khẩu sau gửi và không ghi key vào log/feedback. `.pio/gui-tests/results.txt`.
- Host tests C/JS: **PASS**. Mã production của VAD, chuyển mẫu 16→24→16 kHz với sóng 440 Hz và ranh giới block tùy ý, ghép WS FIN/continuation ở mọi điểm chia của fixture, cấu hình giao dịch, header injection, duplicate keys, lọc/xóa key đều qua. Đã sửa lỗi signed/unsigned trong phép nội suy nhờ kiểm thử tín hiệu. Safety/motor/protocol/Web UI tiếp tục PASS.
- Kiểm tra source ESP-IDF phát hiện transport WebSocket mask payload tại chỗ dù khai báo `const`; code AI sao chép vào RAM trước mọi lần gửi, kể cả JSON literal. Tắt tag `transport_ws` vì đường lỗi của thư viện có thể log HTTP Authorization.
- Build/upload ESP32-S3 cuối: **PASS**, RAM tĩnh **60.560/327.680 byte**, app **1.138.463/4.194.304 byte** (27,1% partition app). `firmware.bin` **1.138.864 byte**; project/diagnostics version **2.2.0**.
- SHA-256 ảnh firmware sau build/upload: `A65A1F1E85B9F6F0578DA880885AEFBEB0C0B9A59FE354706E4510540D5DF115`.
- `esptool.py --chip esp32s3 image_info firmware.bin`: checksum và validation hash **valid**. Không ghi flash trong bước kiểm tra này.
- Đã render và xem ảnh GUI thật `docs/gui-ai-preview.png`; file ứng dụng mới `WALL-E-Control.exe` đã build.
- COM6 xuất hiện trở lại trong lúc hoàn thiện. Đã kiểm tra bản cũ 2.0.0 trả `IDLE`, motor/servo disarmed trước khi nạp. Upload `run -e esp32-s3-n16r8 -t upload --upload-port COM6`: **PASS**, exit0, hash verified cho bootloader/partition/app; không erase toàn chip. Board tự khởi động qua reset của công cụ.
- Xác nhận trên board: **firmware 2.2.0**, `IDLE`, `armed=false`, `servo_enabled=false`, `controller_transport=none`; NVS/PSRAM/motors/servos/Wi-Fi/Web/voice_ai khởi tạo `ESP_OK`. Audio/LCD/battery đang tắt theo cấu hình cũ. AI `DISABLED`, `key_configured=false`, Wi-Fi STA chưa cấu hình. Không arm/servo-enable hoặc phát âm thanh trong bước này. Snapshot lọc secrets: `.pio/hardware-checks/2026-10-21-ai-bootstrap.json`.
- Đã kiểm tra `usb_hello` thật trả version1/seq1/transport USB, rồi bấm **Kết nối** trong GUI qua UI Automation để đọc trạng thái và cấu hình, không nhận quyền điều khiển.
- **Chưa gọi OpenAI API thật.** Người dùng chọn robot hoạt động độc lập và đã gửi một API key vào chat; không dùng/lưu key đã lộ đó vào project hoặc thực hiện API request. Đã hướng dẫn thu hồi và nhập key mới trực tiếp trong GUI.
- Chưa kiểm chứng trên chip: TLS/DNS/NTP/Internet, quyền model/hạn mức API, âm lượng/kênh/gain micro, chất lượng VAD/phát loa, độ ổn định bộ nhớ và hủy AI khi điều khiển thật. Các mục này cần board, ngoại vi và key mới. Không đánh đồng build/host tests với hội thoại trên phần cứng.

Hướng dẫn tiếp theo: [ai_voice.md](ai_voice.md).

## GUI điều khiển bốn motor độc lập và chuỗi — 2026-10-21

- Theo thay đổi cơ cấu của người dùng, thay phần bánh xích trong tab1 bằng M1–M4 độc lập, giữ Thuận/Nghịch, mức công suất riêng, chọn chiều riêng cho tổ hợp, các preset cặp/cả bốn và phím giữ1–4/Shift. GPIO giữ4/5,6/7,8/9,10/11; gửi trực tiếp lệnh `motors` của firmware đã nạp.
- Thêm soạn/reorder/xóa tối đa20 bước, thời gian0,1–30s, chụp giá trị từng bước; chạy một lượt bằng GUI và gửi0 khi kết thúc. Chuỗi bị hủy khi dừng, đổi tab/cửa sổ, mất kết nối, UI ngắt trên300ms hoặc motor ACK lỗi. Không bỏ qua các bước theo kiểu phát dồn sau khi UI bị treo, không tự nối lại/chạy tiếp.
- Controller làm mới `motors` mỗi100ms trong chế độ độc lập, tránh bị vòng lặp `drive` cũ ghi đè. ACK motor cũ bị chặn sau stop/arm mới. Đổi thông số khi đang giữ nút sẽ gửi0 và cần nhấn lại.
- Build và desktop tests: **PASS, 232 assertions**. Kiểm tra đủ81 tổ hợp0/+/- của4 kênh, chuyển chế độ, copy vector, từ chốiNaN, release tức thời, ACK motor lỗi/cũ, thứ tự/thời gian/copy bước, pause0, hoàn tất một lượt, bounds, lỗi ghi khi bắt đầu, focus/UI timeout, các nút motor và sự kiện WPF thực, preset khác chiều, sửa mức khi giữ, soạn/reorder/chạy/dừng chuỗi. Serial và mouse capture dùng adapter giả lập; không mở cổng vật lý hoặc quay motor trong bộ kiểm thử. Nhánh pointer production vẫn dùng WPF CaptureMouse/ReleaseMouseCapture.
- Các kiểm thử AI/servo/settings/USB cũ tiếp tục PASS. Log `.pio/gui-tests/results.txt`. Render GUI mới: `docs/gui-motors-preview.png`, `docs/gui-sequence-preview.png`.
- Không sửa firmware hoặc nạp lại board cho thay đổi GUI này. Chạy motor/chuỗi với tải thật và chất lượng thời gian trên phần cứng vẫn chưa kiểm chứng. GUI cần USB/laptop trong thời gian chạy chuỗi; danh sách chưa lưu file/flash.
- Đã mở lại ứng dụng mới ở tab1 và xác nhận qua UI Automation có tiêu đề4 động cơ DC, các nút riêngM1–M4 và nút đến chuỗi thời gian. Cuối lần cập nhật laptop chỉ còn COM3/COM4, không thấy COM6; GUI để trạng thái chưa kết nối, chưa gửi lệnh arm/chạy motor thật.
