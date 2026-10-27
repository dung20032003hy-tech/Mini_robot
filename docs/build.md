# Cài đặt, build, flash và monitor

## PlatformIO (phù hợp môi trường hiện tại)

Đã có PlatformIO Core trong VS Code. `platformio.ini` chọn platform6.12.0 và package ESP-IDF3.50500.0 (ESP-IDF5.5.0). Không dùng platform mới nhất tự động vì có thể chuyển major ESP-IDF.

Board manifest riêng `boards/esp32-s3-n16r8.json` mô tả đúng Flash16MB và CPU ESP32-S3; PSRAM Octal được cấu hình trong sdkconfig. GPIO DevKit vẫn cần xác minh độc lập. `packages_dir=.pio/packages` tránh dấu cách trong tên tài khoản Windows; giữ thư mục dự án ở đường dẫn không có dấu cách như `E:\Robot_Mini`.

```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe"
& $pio --version
& $pio run -e esp32-s3-n16r8
& $pio run -e esp32-s3-n16r8 -t menuconfig
& $pio device list
& $pio run -e esp32-s3-n16r8 -t upload --upload-port COM7
& $pio device monitor --port COM7 --baud 115200
```

Lần đầu cần Internet và dung lượng vài GB cho toolchain/ESP-IDF. Build log phải thể hiện ESP-IDF5.5.0; xem `docs/verification.md` để biết kết quả thực tế của phiên này. Không coi package đã download là build thành công.

## ESP-IDF độc lập

Cài bản **5.5.0** bằng [ESP-IDF Windows installer chính thức](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/get-started/windows-setup.html). Chọn target ESP32-S3 và toolchain đúng manifest của bản đó. Mở ESP-IDF PowerShell đã export environment, không trộn PATH của nhiều bản:

```powershell
idf.py --version
idf.py set-target esp32s3
idf.py build
idf.py -p COM7 flash monitor
```

Root CMake dùng cấu trúc ESP-IDF chuẩn `main` + `components`; web assets được nhúng vào app, không cần upload filesystem riêng. Phân vùng factory app4MiB, phần còn lại storage dự phòng; **chưa có OTA**, không có API firmware update. Mọi quy trình flash phải ngắt nguồn motor/servo hoặc dừng và kiểm chứng trạng thái IN trước khi vào bootloader. Không để công cụ upload tự chạy khi robot đang chạy.

`sdkconfig.defaults`: Quad flash80MHz16MB, Octal PSRAM80MHz, cho phép boot nếu PSRAM không tìm thấy nhưng diagnostics báo lỗi; WebSocket bật; task watchdog5s và panic. PSRAM thiếu cần sửa cấu hình/board, không xem báo lỗi đó là bình thường.

USB Serial/JTAG dùng USB native19/20. Nếu board có hai cổng USB, xác định cổng native theo sơ đồ. Với cổng UART bridge, menuconfig→Component config→ESP System Settings→Channel for console output→UART, rebuild. Không gán cố định COM7 trong source.

Host tests và adapter phần cứng: xem README. Các test firmware target, đo scope và stress thực tế trong test_plan chưa thể chạy nếu không có board.
