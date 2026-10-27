# Prompt tạo sơ đồ đấu nối

Ngày tạo: 2026-10-16. Công cụ: **image_gen tích hợp** (không dùng CLI/API).
Nguồn GPIO: `components/board_config/board_config.c`, đối chiếu với `docs/gpio_mapping.md` và `docs/wiring.md`.
Hình thể hiện kết nối logic theo tên GPIO; không xác nhận vị trí chân trên một DevKit cụ thể.

## Tờ 1 — Dây tín hiệu

```text
Use case: infographic-diagram
Asset type: accurate wiring poster for an existing ESP32-S3 robot firmware, Vietnamese labels.
Primary request: Draw a crisp, professional landscape electrical signal wiring diagram for WALL-E MINI using ESP32-S3 N16R8. Use high resolution, generous whitespace, large sharp typography, white background, thin orthogonal colored wires with round junction dots. Flat clean engineering illustration; small recognizable device icons as secondary aids. Technical accuracy takes priority over decorative realism. This is sheet 1 of 2, signal connections. No photorealistic breadboard, no invented physical header positions.
Title verbatim: "WALL-E MINI • SƠ ĐỒ DÂY TÍN HIỆU"
Subtitle verbatim: "ESP32-S3 N16R8 • Theo GPIO trong firmware • Tờ 1/2"
Composition: large central ESP32-S3 logical connector block with labeled GPIO ports, not a physical pinout. Two motor driver blocks at upper left, each with two separate motor icons, two servo blocks lower left; LCD block upper right; microphone and amplifier blocks lower right. Keep port rows aligned so every individual wire is traceable. Can use identical labeled net tags at both ends to avoid long crossing wires; make the legend explicit: "Nhãn giống nhau = cùng một dây". Do not draw joined lines unless electrically connected. No wire may pass through any block or unrelated label.
All exact connections required:
Motor driver 1 labeled "DRV8833 #1 • BÊN TRÁI":
ESP GPIO4 -> AIN1; GPIO5 -> AIN2; GPIO6 -> BIN1; GPIO7 -> BIN2.
AOUT1 and AOUT2 are the two separate wires to motor "LF • Trái trước".
BOUT1 and BOUT2 are the two separate wires to motor "LR • Trái sau".
Its VM port marked "+3V MOTOR" and GND marked "GND CHUNG".
Motor driver 2 labeled "DRV8833 #2 • BÊN PHẢI":
ESP GPIO8 -> AIN1; GPIO9 -> AIN2; GPIO10 -> BIN1; GPIO11 -> BIN2.
AOUT1 and AOUT2 are the two separate wires to motor "RF • Phải trước".
BOUT1 and BOUT2 are the two separate wires to motor "RR • Phải sau".
Its VM port marked "+3V MOTOR" and GND marked "GND CHUNG".
All four DC motors are independent outputs. No motor terminal goes to ESP or common ground. Both drivers have amber boxed text "nSLEEP: chưa gán GPIO (-1)" with NO actual nSLEEP wire. Shared amber note: "Kiểm tra nSLEEP trên module trước khi chạy".
Servo block "SERVO TRÁI": SIG <- GPIO40, V+ tag "+5V", GND tag "GND".
Servo block "SERVO PHẢI": SIG <- GPIO41, V+ tag "+5V", GND tag "GND".
LCD block "LCD ST7789 • 2.25 inch": SCLK <- GPIO12; MOSI <- GPIO13; DC <- GPIO14; CS <- GPIO15; RST <- GPIO16. GND tag "GND".
LCD VCC and BL must have NO power connection and NO assigned GPIO. Amber empty dashed ports with note "VCC / BL: cần xác nhận module"; BL firmware value -1. Never depict LCD VCC connected to either 3V3 or 5V, even tentatively.
Microphone block "MIC INMP441": SCK <- GPIO17, WS <- GPIO18, SD -> GPIO21. VDD tag "+3V3"; GND tag "GND"; L/R connects to GND. Text "L/R = GND • kênh LEFT".
Amplifier block "MAX98357A": BCLK <- GPIO17, LRC <- GPIO18, DIN <- GPIO39. VIN tag "+5V"; GND tag "GND".
Clock nets GPIO17 and GPIO18 are each shared between microphone and amplifier; represent with correctly labeled branches or matching net tags, never short GPIO17 to GPIO18.
Speaker icon "LOA 4Ω / 3W" connects ONLY between amplifier SPK+ and SPK− using two wires. Neither speaker terminal connected to GND.
Small note at amplifier: "SD / GAIN: theo module".
ESP block contains optional GPIO1 port with dashed tag "ADC pin • xem tờ 2", marked optional and not connected to raw pack.
ESP block power pins labeled "3V3", "GND" and "5V/VIN: xác nhận DevKit" only; no assumed physical header ordering.
Footer three short notes exact:
"GPIO là tên chân, không phải số thứ tự trên hàng header."
"Mọi GND thiết bị nối mass chung; xem sơ đồ nguồn ở tờ 2."
"Chưa xác minh phần cứng thực tế. Không nối 2S trực tiếp vào ESP32."
Color: signal blue/teal, driver output green, 5V red tags, 3V motor orange tags, 3V3 purple tags, ground dark gray, unresolved amber dashed boxes. Include small clear legend. All labels readable in Vietnamese with correct diacritics. Show each GPIO number exactly as listed and only once per ESP logical port (shared clocks have branches). Do not invent more pins or change GPIO assignments.
```

## Tờ 2 — Cấp nguồn (bản nháp không dùng làm tài liệu đấu dây)

```text
Use case: infographic-diagram
Asset type: high resolution landscape electrical power wiring poster in Vietnamese, sheet 2/2 of an ESP32-S3 robot wiring guide.
Primary request: Create a very clear accurate engineering diagram for WALL-E MINI power distribution and an OPTIONAL battery ADC inset. White background, spacious orthogonal wire layout, crisp large sans-serif labels, subtle panel borders, small flat module illustrations. Technical accuracy is essential. Match a professional signal wiring sheet visually. Favor simple explicit terminal labels over realistic photos of unknown modules.
Title: "WALL-E MINI • SƠ ĐỒ CẤP NGUỒN"
Subtitle: "ESP32-S3 N16R8 • Nguồn motor 3V / hệ thống 5V • Tờ 2/2"
Top main diagram composition left-to-right:
An enclosed "PIN 2S • 2 × 18650 nối tiếp" battery assembly marked "Tối đa 8,4V" goes into a "BMS 2S" symbolic block, NOT a physical terminal drawing. Within or under BMS: "Đấu các cell theo sơ đồ nhà sản xuất".
BMS has two protected outputs: positive "OUT+ được bảo vệ", negative "OUT− được bảo vệ". Raw battery negative must stop at BMS; do not bypass BMS with a ground wire. Do NOT draw physical B+/BM/B− pads because actual BMS is unspecified.
Protected positive wire goes through a fuse labeled "CẦU CHÌ", then "CÔNG TẮC", then splits into exactly two buck regulator IN+ terminals. Protected negative feeds both buck IN− terminals and the common downstream ground bus. Every fuse/switch connection must be electrically consistent and NOT short positive and negative.
Upper converter "BUCK MOTOR • CHỈNH 3V": IN+/IN− from protected pack. OUT+ orange wire labeled "+3V MOTOR" splits to "DRV8833 #1 • VM" and "DRV8833 #2 • VM". Both driver GND terminals connect common GND. The 4 TT motors are powered ONLY via these drivers' H-bridge outputs (can reference tờ 1 rather than draw motor detail). Do not feed motors straight from battery or 5V or ESP3V3.
Lower converter "BUCK HỆ THỐNG • CHỈNH 5V": IN+/IN− from protected pack. OUT+ red rail labeled "+5V" supplies exactly:
"SERVO TRÁI • V+"; "SERVO PHẢI • V+"; "MAX98357A • VIN".
Also a dashed red branch to "ESP32-S3 DevKit • 5V/VIN" containing amber condition note "Chỉ nối khi xác nhận đúng chân nguồn DevKit". This conditional branch must NOT connect +5V into 3V3 or any GPIO.
All GND ports of ESP, two servos, amplifier, drivers connect common dark gray ground bus from protected BMS OUT−. Both buck OUT− connect common ground.
From the powered ESP block, a separate purple line labeled "+3V3" from its "3V3" output powers "INMP441 • VDD". Mic GND and L/R connect common GND.
An isolated LCD block "LCD ST7789": GND connects common ground. VCC and BL are empty dashed amber ports, no wire to 5V or3V3. Label "VCC / BL: cần xác nhận module". Do not infer LCD module supply from controller name.
No speaker output wires or logic signal wires needed on this sheet, except mic L/R and optional ADC. Reference signal drawing "Dây GPIO: xem tờ 1".
Show a single well-defined common downstream ground bus "GND CHUNG • phía OUT− của BMS", with separate branches returning power loads and ESP/mic; don't daisy chain motor return through microphone.
Bottom-right inset, separate dashed panel:
Heading "TÙY CHỌN • ĐO PIN GPIO1"
Must show REAL correct circuit topology as a resistor divider:
"+2S sau BMS + công tắc" -> Rtop "100kΩ" -> ADC junction dot -> Rbottom "33kΩ" -> GND.
From that same ADC junction a blue wire goes to "ESP GPIO1 / ADC1_CH0".
A capacitor labeled "100nF" connects in parallel with Rbottom between ADC junction and GND (not in series, not shorted).
Use node dots only at actual connected nodes; keep capacitor branch separate from resistor.
Notes exact: "8,4V → ADC ≈ 2,085V"; "battery_ratio = 4.0303 (cần hiệu chuẩn)"; "battery_enabled = true chỉ sau khi có mạch"; "Mặc định firmware: đo pin TẮT".
This is an example optional circuit, NOT a confirmed existing part. Keep its supply as a net tag referencing the protected switched pack bus; NEVER connect raw2S directly to GPIO1.
Bottom-left concise notes:
"Đo 3V và 5V trước khi nối tải."
"Không nối 2S trực tiếp vào ESP32 / GPIO."
"Tránh cấp đồng thời USB 5V và buck khi chưa rõ mạch DevKit."
"nSLEEP DRV8833 chưa được firmware gán GPIO; cần kiểm tra module."
Legend: red +5V; orange +3V MOTOR; purple +3V3; charcoal GND; amber dashed = cần xác nhận / tùy chọn.
Footer: "Sơ đồ theo firmware; chưa xác minh phần cứng thực tế."
Avoid tiny unreadable text, decorative extra electronics, invented current ratings, invented LCD supply, physical pin positions, crossed power shorts, tying BMS rawcell negative directly to downstreamcommonGND. All Vietnamese text accurate and spacious. Render at high resolution landscape.
```

## Tờ 2 — Hiệu chỉnh bố cục v2

Thay các dây nguồn dài của bản nháp bằng nhãn net, tách riêng 5 V và 3,3 V, sửa chân L/R của mic. Bản nháp bị loại khỏi bộ hình sử dụng. Bản cuối dùng v3 sau [hiệu chỉnh ký hiệu buck](final-correction-prompt.md); v1 và v2 không dùng để đấu dây.

```text
Use case: precise-object-edit
Edit target: the attached WALL-E MINI power wiring sheet.
Primary request: Correct the electrical inaccuracies in this diagram. Keep Vietnamese typography, clean white engineering style, title, sheet 2/2, and the already correct OPTIONAL ADC inset at bottom right. REBUILD the entire upper main diagram into a spaced modular schematic with SHORT LOCAL WIRES + MATCHING NET TAGS, replacing all long power buses. Do not attempt to preserve any old long wiring. There must be ZERO possible short circuits. A color line touching a block is not enough: every stub must end precisely at its named electrical terminal.

CRITICAL ERRORS TO REMOVE FROM ORIGINAL:
- Red 5V line wrongly connected to purple 3V3 mic line: remove completely. +5V and +3V3 are DIFFERENT nets, never joined. ESP must have a separate 3V3 OUTPUT terminal.
- Protected battery positive wrongly meets ground below the motor buck: eliminate. Positive and ground must NEVER share a junction.
- Servo/amp red wires wrongly touch tops of blocks rather than named V+/VIN pins. Replace with local stubs ending at their labeled pins.
- Microphone wrongly labels L and R as DATA/CLK: remove those false labels. Mic has ONE L/R SELECT pin; connect it to GND. Do not draw any data or clock pins on this POWER sheet.
- Common ground must come from BMS protected OUT−, not raw cell negative.

New composition: modular 3-column grid of separate cards, connected by explicitly matching net labels. This IS a wiring diagram using conventional named nets; explain clearly "Nhãn giống nhau = nối chung một dây". Do not draw long wires between cards. Each terminal has a 1-2cm short straight wire terminating in a distinct rectangular colored net tag. NO continuous inter-card bus, NO wires crossing, NO red/purple touching. Each card has enough blank space around it.
At top full width, ONLY actual short linked chain with two noncrossing wires:
"PIN 2S • 2×18650 nối tiếp • Tối đa 8,4V" goes into symbolic "BMS 2S • Đấu cell theo sơ đồ nhà sản xuất".
BMS OUT+ -> inline "CẦU CHÌ" -> inline "CÔNG TẮC" -> net tag "+2S SAU BMS + CÔNG TẮC".
BMS OUT− -> net tag "GND CHUNG".
Battery negative terminates only on BMS input; no direct branch to GND bus. BMS physical terminal/pad layout NOT shown.
Below top chain, cards:
1. "BUCK MOTOR • chỉnh 3V"
IN+ short wire to net tag "+2S SAU BMS + CÔNG TẮC"; IN− short wire to tag "GND"; OUT+ short orange wire to tag "+3V MOTOR"; OUT− short dark gray wire to tag "GND". Keep inputs and outputs physically separate.
2. "BUCK HỆ THỐNG • chỉnh 5V"
IN+ short wire to net tag "+2S SAU BMS + CÔNG TẮC"; IN− -> "GND"; OUT+ short red wire -> "+5V"; OUT− -> "GND".
3. "ESP32-S3 DevKit"
pin "5V/VIN" <- short DASHED red wire from "+5V" tag; pin "GND" -> "GND" tag; separate pin "3V3 OUT" -> short purple wire -> "+3V3" tag.
Amber condition inside ESP card: "Chỉ nối 5V/VIN sau khi xác nhận DevKit". NEVER connect 5V to 3V3 OUT.
Next row cards:
4. "DRV8833 #1 + #2": each has its own VM -> orange "+3V MOTOR" tag; each GND -> dark gray "GND" tag. Label "Motor TT lấy nguồn qua OUT của driver • xem tờ 1". Do not draw motor direct supply.
5. "SERVO TRÁI + SERVO PHẢI": each separately V+ -> red "+5V" tag, GND -> dark gray "GND" tag. Short wires END at V+ and GND ports. No signal pins needed.
6. "MAX98357A": VIN -> red "+5V" tag, GND -> dark gray "GND" tag.
Next row small cards:
7. "INMP441": VDD -> purple "+3V3" tag; GND -> darkgray "GND" tag; ONE "L/R" pin -> "GND" tag. Note "L/R = GND • kênh LEFT". Do not show wrong L and R separate. No DATA or CLK pins in this power-only card.
8. "LCD ST7789": GND -> darkgray "GND" tag. VCC and BL empty unconnected amber dashed ports. Note "VCC / BL: cần xác nhận module". NO power wire to either LCD VCC or BL.
Correct optional ADC inset already in original: keep as-is with 100kΩ upper, 33kΩ lower and 100nF parallel to lower from ADC node to GND; GPIO1 / ADC1_CH0 from middle, supply "+2S sau BMS + công tắc"; values 8,4V -> ADC≈2,085V; battery_ratio=4.0303; measurement OFF by default.
Compress old warning block if necessary to maintain readable labels. Footer notes:
"GND = OUT− được bảo vệ của BMS. Không nối tắt BMS."
"Đo 3V và 5V trước khi nối tải. Không đưa 2S vào ESP/GPIO."
"USB 5V + buck: cần kiểm tra mạch DevKit. nSLEEP DRV8833 chưa gán GPIO."
"Sơ đồ theo firmware; chưa xác minh phần cứng thực tế."
Use identical net text wherever same net. +2S protected switched tag consistent. Dark gray grounds all same GND. The upper chain OUT− GND CHUNG and card GND same electricalnet via legend. Large crisp Vietnamese text. High resolution landscape; enlarge canvas if necessary. No ornamental wiring, no mysterious red stubs to nowhere, no joining unrelated rails. Main aim: unambiguous safe wiring labels with correct electrical separation.
```
