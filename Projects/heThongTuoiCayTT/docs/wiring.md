# Sơ đồ nối dây – Hệ thống tưới cây TT

Hệ thống gồm Arduino Nano (tưới cây, LCD, 2 relay) và ESP32-CAM (chụp ảnh, nhận kết quả AI từ app). Hai bo nói chuyện với nhau qua 2 dây UART đi qua module BSS138: ESP chạy 3,3 V còn Nano chạy 5 V.

Bản này khớp với firmware [`src/main.cpp`](../src/main.cpp) (Nano) và [`esp32cam/src/main.cpp`](../esp32cam/src/main.cpp), kiểm tra ngày 08/10/2026. Đổi chân trong code thì sửa cả file này.

## 1. Chân Arduino Nano

| Chân Nano | Nối tới | Ghi chú |
|---|---|---|
| D12, D11, D5, D4, D3, D2 | LCD 1602: RS, E, D4, D5, D6, D7 | `LiquidCrystal lcd(12, 11, 5, 4, 3, 2)` |
| A0 | Cảm biến độ ẩm đất (AOUT) | Cảm biến điện dung |
| D7 | Relay bơm (IN1) | `RELAY_PIN` |
| D6 | Relay quạt (IN2 nếu dùng module 2 kênh) | `FAN_PIN` |
| D8 (RX) | BSS138 HV2 | Nhận từ ESP IO13 |
| D9 (TX) | BSS138 HV1 | Gửi sang ESP IO14 |
| 5V | Ray 5V của Nano | Khoảng 4,7 V sau diode 1N5819 |
| GND | GND chung | |

Cả 2 relay đều **kích mức LOW** (`RELAY_ACTIVE` và `FAN_RELAY_ACTIVE` đều là `LOW`). Nếu relay của bạn kích mức HIGH thì đổi 2 hằng này thành `HIGH`.

## 2. Chân ESP32-CAM (AI-Thinker)

| Chân ESP32-CAM | Nối tới | Ghi chú |
|---|---|---|
| 5V | Ray 5V-ADP (adapter chung) | Cắm tụ 470–1000 µF sát chân này, chân (+) vào 5V |
| GND (cạnh chân 5V) | GND chung | |
| 3V3 | BSS138 LV | Chỉ làm mức áp tham chiếu cho phía 3,3 V, không dùng để nuôi module khác |
| IO14 (RX) | BSS138 LV1 | Nhận dữ liệu từ Nano D9 |
| IO13 (TX) | BSS138 LV2 | Gửi dữ liệu tới Nano D8 |
| VCC | Để trống | Đây là ngõ ra, không phải chỗ cấp nguồn |
| IO4 | Để trống | Đã nối sẵn với đèn flash trên bo |
| IO0, U0R, U0T | Để trống | Chỉ dùng lúc nạp code |

Camera OV2640 cắm bằng cáp dẹt có sẵn trên bo, không phải nối dây. **Không cắm thẻ microSD**, vì IO13 và IO14 cũng là chân dữ liệu của khe thẻ.

## 3. Module BSS138 (4 kênh)

| Chân BSS138 | Nối tới | Ghi chú |
|---|---|---|
| HV | Ray 5V của Nano | Khoảng 4,7 V sau diode |
| LV | ESP32-CAM 3V3 | Khoảng 3,3 V |
| GND | GND chung | Module có 2 chân GND (mỗi bên 1 chân) thì nối cả hai |
| HV1 | Nano D9 (TX) | Chiều Nano → ESP |
| LV1 | ESP32-CAM IO14 (RX) | |
| HV2 | Nano D8 (RX) | Chiều ESP → Nano |
| LV2 | ESP32-CAM IO13 (TX) | |
| HV3, HV4, LV3, LV4 | Để trống | |

Không đảo HV với LV. Khi chưa truyền dữ liệu, HV1 và HV2 đo khoảng 4,7 V, còn LV1 và LV2 đo khoảng 3,3 V.

## 4. Relay bơm và relay quạt

Phía điều khiển (dùng module relay 2 kênh, hoặc 2 module 1 kênh):

| Chân relay | Nối tới |
|---|---|
| VCC | Ray 5V của Nano |
| GND | GND chung |
| IN1 | Nano D7 (bơm) |
| IN2 | Nano D6 (quạt) |

### Mạch bơm (phía tải của relay 1)

| Từ | Đến | Ghi chú |
|---|---|---|
| (+) nguồn bơm, qua jack DC | Relay 1 COM | Dây đỏ |
| Relay 1 NO | (+) bơm | Dùng NO để bơm tắt khi Nano mất nguồn |
| (−) bơm | (−) nguồn bơm | Dây đen |
| Diode 1N4007 | Hai cực bơm | Vạch (cathode) quay về (+) bơm. Lắp ngược sẽ chập nguồn bơm khi relay đóng |
| Tụ gốm 100 nF (mã 104) | Hai cực bơm | Không phân cực, hàn sát thân bơm để giảm nhiễu làm LCD ra ký tự rác |

Chân NC của relay để trống. Không lấy nguồn bơm từ chân 5V của Nano: dòng khởi động của bơm làm sụt áp, khiến Nano bị reset và LCD ra ký tự rác.

### Mạch quạt 5V 0,18 A (phía tải của relay 2)

| Từ | Đến | Ghi chú |
|---|---|---|
| Ray 5V-ADP (+) | Relay 2 COM | |
| Relay 2 NO | Dây đỏ của quạt | Quạt tắt khi Nano mất nguồn |
| Dây đen của quạt | GND chung | |
| Diode 1N4007 | Song song với quạt | Vạch (cathode) quay về dây đỏ |

Quạt lấy điện từ adapter, nên khi chỉ cắm USB laptop thì relay 2 vẫn kêu tách nhưng quạt không quay. Firmware bật quạt theo kết quả AI gần nhất: `BENH` bật 10 phút / tắt 20 phút, `NGHI` bật 5 phút / tắt 25 phút, các kết quả khác thì tắt.

## 5. Nguồn điện

Mạch dùng 3 nguồn. GND của USB, adapter, Nano, ESP32-CAM, BSS138 và quạt nối chung với nhau, riêng nguồn bơm thì không.

| Nguồn | Cấp cho | Nối vào GND chung? |
|---|---|---|
| USB laptop → cổng USB của Nano | Nano khi nạp code, và Serial 9600 tới laptop | Có, qua cáp USB |
| Adapter 5V 2A → ray 5V-ADP | Chân 5V của ESP32-CAM, chân 5V của Nano qua diode 1N5819, và quạt qua COM–NO của relay 2 | Có. Bắt buộc phải nối thì UART mới chạy |
| Adapter hoặc pin 5V riêng cho bơm | Bơm, đi qua COM–NO của relay 1 | Không, relay đã cách ly hai phía |

- **Diode 1N5819:** đầu không có vạch cắm vào ray 5V-ADP, đầu có vạch cắm vào ray 5V của Nano. Diode chặn USB và adapter đẩy dòng ngược vào nhau, nên cắm cả hai cùng lúc vẫn an toàn.
- **Ray 5V của Nano** (khoảng 4,7 V sau diode) nuôi LCD, biến trở, cảm biến, cuộn hút của 2 relay và chân HV của BSS138.
- **Tụ 470–1000 µF** nằm giữa 5V và GND, sát chân ESP32-CAM: chân dài (+) vào 5V, phía có vạch (−) vào GND.
- **Dòng trên adapter** khoảng 0,65 A: ESP32-CAM khoảng 0,25 A (hơn khi bật flash), ray Nano khoảng 0,2 A, quạt 0,18 A. Adapter 2 A dư sức. Diode 1N5819 chịu 1 A, chỉ phải gánh phần ray Nano.
- **Đổi nguồn từ USB sang adapter có thể làm giá trị Raw của cảm biến lệch đi.** Ghi lại Raw lúc khô và lúc nhúng nước, rồi sửa `airValue` và `waterValue` trong `src/main.cpp` nếu cần.

## 6. Nạp code

Nano: cắm cáp USB, chạy `pio run -t upload` ở thư mục gốc `heThongTuoiCayTT`.

ESP32-CAM có 2 cách nạp:

**Qua WiFi (OTA, mặc định):** chạy `pio run -d esp32cam -t upload`, không cần tháo ESP khỏi mạch. Chạy `./esp32cam/watch_upload.sh` thì cứ lưu file code là tự build và nạp. Mật khẩu OTA nằm trong `esp32cam/secrets.ini` (copy từ `secrets.example.ini`). Cách này chỉ dùng được khi ESP đang online và đang chạy firmware có OTA.

**Qua cáp:** chạy `pio run -d esp32cam -e esp32cam -t upload`. Lần đầu bật OTA bắt buộc phải nạp cách này, vì phải ghi lại bảng phân vùng flash. Nếu code mới làm ESP treo trước khi vào được WiFi thì cũng phải nạp lại qua cáp. Có 2 cách nối:

- **Đế ESP32-CAM-MB:** rút ESP ra khỏi dây dupont, cắm lên đế rồi cắm USB. Nếu báo lỗi `Failed to connect`, giữ nút IO0 trên đế, bấm RST, rồi nạp lại.
- **Mạch USB-TTL (gạt sang 5V):** rút adapter trước, để ESP chỉ lấy nguồn từ USB-TTL. Nối như sau:

| USB-TTL | ESP32-CAM |
|---|---|
| 5V | 5V |
| GND | GND |
| TX | U0R (IO3) |
| RX | U0T (IO1) |
| GND | IO0, chỉ nối trong lúc nạp. Nạp xong thì rút dây này rồi bấm RST |

## 7. Dữ liệu trên 2 dây UART

Cả 2 bên chạy 9600 baud, 8N1:
- Nano: `SoftwareSerial espSerial(8, 9)`.
- ESP: `Serial2.begin(9600, SERIAL_8N1, 14, 13)`.

| Chiều | Dòng gửi | Khi nào |
|---|---|---|
| ESP → Nano | `AI:<tối đa 16 ký tự>`, vd `AI:BENH 99%` | Khi app gửi `POST /result`, sau đó gửi lại mỗi 60 giây |
| Nano → ESP | `AI OK: <text>` | Trả lời mỗi dòng `AI:` |
| Nano → ESP | `SOIL:<độ ẩm %>,<raw>,<bơm 0/1>,<lỗi cảm biến 0/1>,<quạt 0/1>` | Mỗi 5 giây; ESP đưa lên `/status` |

Nano cũng nhận dòng `AI:` qua cáp USB (từ `ai/service.py` hoặc Serial Monitor), nên vẫn chạy được khi chưa có ESP.

### Kiểm tra sau khi nối

1. Cắm adapter, chưa cần app. Đo áp khi nghỉ: HV1 và HV2 khoảng 4,7 V, LV1 và LV2 khoảng 3,3 V.
2. **Chiều ESP → Nano:** từ laptop cùng WiFi, chạy
   ```bash
   curl -X POST -H "Content-Type: text/plain" --data-binary "TEST 123" http://<IP>/result
   ```
   Dòng 2 của LCD phải luân phiên giữa `Bom:... R:...` và `TEST 123` mỗi 3 giây. Dùng `TEST` để thử vì gửi `BENH` hoặc `NGHI` sẽ bật quạt. Gửi `--data-binary ""` để xóa kết quả.
3. **Chiều Nano → ESP:** mở `http://<IP>/status`. Mục `soil` phải khác `null` và `age_s` nhỏ hơn 10.

| Hiện tượng | Kiểm tra |
|---|---|
| LCD không hiện `TEST 123` | Dây IO13 – LV2, HV2 – D8, GND chung giữa adapter và Nano |
| `/status` có `"soil":null`, hoặc `age_s` tăng mãi | Dây D9 – HV1, LV1 – IO14 |
| Cả 2 chiều đều không chạy | Chân HV/LV của BSS138 có bị đảo không, HV có 4,7 V và LV có 3,3 V không, GND chung |
| LCD hiện `MAT KET NOI ESP` | Nano không nhận được dòng `AI:` nào trong 3 phút: ESP mất nguồn hoặc treo, hoặc dây D8 lỏng |
