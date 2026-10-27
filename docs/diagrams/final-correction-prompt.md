# Hiệu chỉnh cuối sơ đồ nguồn

Công cụ: image_gen tích hợp. Bản đầu vào: `walle-power-wiring-v2.png`.
Thay hình PCB buck bằng khối chức năng để tránh ký hiệu cực tính sai trong hình minh họa.

```text
Use case: precise-object-edit
Edit target: the supplied WALL-E MINI power wiring sheet.
Make ONLY a correction to the TWO buck regulator cards in row 2. Preserve everything else EXACTLY: all text, layout, the ADC circuit, all other modules and their isolated named nets.
Replace the blue photographic PCB illustration inside EACH buck card with a neutral light gray rectangle labeled "BUCK DC–DC". There must be no tiny internal PCB pin labels, because the existing motor buck illustration incorrectly has a tiny lower-left IN+ even though the actual lower input is negative.
Keep FOUR correct external terminals on BOTH cards: upper left IN+, lower left IN−, upper right OUT+, lower right OUT−. Use short straight local connections with no crossings. IN+ to the existing protected switched +2S net label; IN− to GND; OUT− to GND. The MOTOR buck OUT+ connects only to +3V MOTOR, using orange wire. The SYSTEM buck OUT+ connects only to +5V, using RED wire. Preserve all existing net names and other short wires. Never join positive to ground. Keep the +5V and +3V3 nets separate. No edits outside these two buck cards. Preserve sharp Vietnamese text.
```
