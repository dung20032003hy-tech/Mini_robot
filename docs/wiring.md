# Đấu dây và kiểm tra nguồn

Hình minh họa theo GPIO trong firmware: [tờ 1 — dây tín hiệu](diagrams/walle-signal-wiring-v1.png), [tờ 2 — cấp nguồn](diagrams/walle-power-wiring-v3.png). Xem [bảng đối chiếu và các điểm cần xác nhận](diagrams/README.md). Các hình dùng tên GPIO, không phải vị trí chân header DevKit.

**Hardware verification pending.** Ngắt pin và USB khi thay dây. Chưa biết model DevKit/module LCD nên không kết luận điện áp VIN/BL hoặc cách ghép nguồn USB + buck của board đó.

1. Pin 2S → BMS 2S đúng sơ đồ nhà sản xuất → cầu chì → công tắc nguồn → hai buck. Pin đầy 2S có thể đạt 8,4 V, tuyệt đối không đưa vào GPIO.
2. Chỉnh/đo buck động cơ ở **3 V** và buck hệ thống ở **5 V** khi chưa nối tải. Đo cực tính, kiểm tra chập và khả năng dòng đỉnh thực.
3. DRV8833 VM lấy đường motor 3 V, GND về mass chung, OUT A/B đến từng motor. Không cấp motor từ 3V3 hoặc GPIO. IN theo bảng GPIO. Xác nhận module có jumper/điện trở kéo nSLEEP hay không.
4. Servo lấy 5 V từ nhánh đủ dòng, GND chung với ESP; dây tín hiệu40/41. Đo thứ tự dây servo, không dựa chỉ vào màu. Hiệu chuẩn khi tháo horn/tải, không coi giới hạn60–120° mặc định là đã phù hợp cơ khí.
5. LCD SCLK12, MOSI13, DC14, CS15, RST16. **Xác nhận VCC, mức logic, BL, kích thước vùng nhìn được với người bán hoặc datasheet module trước khi cấp nguồn.** ST7789 controller không đủ để suy ra điện áp board LCD. Bật `lcd_enabled` sau đó.
6. INMP441 VDD3,3 V, GND chung, SCK/BCLK17, WS18, SD21. L/R nối GND chọn left (mặc định), nối3,3 V chọn right và đổi `mic_right`. Không để L/R trôi.
7. MAX98357A VIN theo thông số module trên đường5 V, GND chung, BCLK17, LRC18, DIN39. Hai chân loa4Ω3W vào SPK+ và SPK−. **Không nối cực loa nào xuống GND.** Xác nhận chân SD/GAIN theo module, lọc nguồn sát ampli.
8. Nếu đo pin, nối mạch chia áp và lọc vào GPIO1. Ví dụ mạch được tính riêng: Rtrên100kΩ, Rdưới33kΩ, ratio=(100+33)/33≈4,0303, điện áp ADC ở8,4 V≈2,085 V; tụ100 nF từ ADC xuống GND. Đo tỉ số thực bằng đồng hồ và hiệu chuẩn, xác nhận giới hạn ADC theo datasheet. Chỉ sau đó bật `battery_enabled` và đặt `battery_ratio` đúng. Không có mạch thì để false.

Tụ khối/tụ gốm và đi dây motor tách khỏi micro giúp giảm nhiễu nhưng giá trị/công suất phải chọn từ đo tải thực. Buck5 V phải chịu đồng thời hai servo, Wi-Fi và loa. Nối mass theo nhánh công suất, tránh dòng motor chạy qua đường mass tín hiệu micro/ESP. Không ghép song song các nguồn5 V vào DevKit nếu chưa biết mạch bảo vệ nguồn của board.

Trước lần thử: bánh xích không chạm sàn, có công tắc ngắt nguồn trong tầm tay, giới hạn PWM25%, xung test200 ms. Hai motor cùng xích không có encoder có thể chống nhau; kiểm tra cơ khí và dòng từng nhánh. Phần mềm không xác nhận quá dòng/nhiệt do không có cảm biến.
