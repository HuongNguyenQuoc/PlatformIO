# Mô phỏng hệ thống trên Wokwi

Toàn bộ hệ thống chạy bằng mô phỏng, không cần mạch thật:

- **Raspberry Pi Pico 2 W** chạy trên Wokwi trong VS Code, gồm cảm biến, LED (thay cho bơm và máy phun sương) và LCD.
- **AI** chạy trên máy tính (`ai/service.py`).
- Hai bên nói chuyện qua **MQTT**, qua một broker công khai trên Internet.

```text
 Wokwi (VS Code)                                  Internet                    Máy tính
┌────────────────────────────┐                ┌───────────────────┐       ┌──────────────────────────┐
│ Raspberry Pi Pico 2 W       │── telemetry ──▶│                   │──────▶│ ai/service.py            │
│  cảm biến đất, DHT22,       │   mỗi 5 giây   │ broker.hivemq.com │       │  ghi logs/sensors.csv    │
│  ánh sáng, mực nước bồn     │                │      (MQTT)       │       │  AI phân tích ảnh mẫu    │
│  LED bơm / phun / cảnh báo  │◀── cmd ────────│                   │◀──────│  gửi MIST: và AI:        │
│  LCD 1602                   │  MIST:, AI:    └───────────────────┘       └──────────────────────────┘
└────────────────────────────┘
```

Vì sao dùng MQTT: Wi-Fi giả lập của Wokwi (`Wokwi-GUEST`) có Internet, nhưng chỉ kết nối **ra ngoài** được, máy tính không kết nối ngược vào Pico được. Pico và máy tính đều chủ động kết nối ra cùng một broker, nên vẫn trao đổi dữ liệu được.

## 1. Cài đặt (làm một lần)

1. Cài 2 extension VS Code: **Wokwi Simulator** (`wokwi.wokwi-vscode`) và **PlatformIO IDE** (`platformio.platformio-ide`).
2. Kích hoạt license Wokwi (miễn phí): nhấn `F1`, chọn **Wokwi: Request a New License**, đăng nhập trên trình duyệt rồi quay lại VS Code.

## 2. Chạy mô phỏng

1. Build firmware:
   ```bash
   cd Projects/heThongTuoiCayTT
   pio run
   ```
   Hoặc bấm **Build** trên thanh PlatformIO.
   - Lần đầu PlatformIO sẽ tự tải nền tảng Raspberry Pi và core Arduino-Pico, mất vài phút.
   - Nền tảng chính thức của PlatformIO chưa có Pico 2 W, nên `platformio.ini` dùng bản cộng đồng `maxgerhardt/platform-raspberrypi`. Đây là bản mà tài liệu Arduino-Pico khuyên dùng.
2. Nhấn `F1`, chọn **Wokwi: Start Simulator**. Mở `diagram.json` để xem mạch.
3. Terminal Wokwi hiện Serial của Pico. Đợi tới khi có các dòng `Da ket noi Wi-Fi` và `Da ket noi MQTT`. Khi lấy được giờ qua NTP, LCD hiện giờ Việt Nam.

Sửa code thì chạy lại `pio run`. Nếu mô phỏng không tự nạp firmware mới thì chạy lại **Wokwi: Start Simulator**.

## 3. Linh kiện trong mạch

| Linh kiện (`id`) | Chân Pico 2 W | Vai trò |
|---|---|---|
| Thanh trượt (`pot1`) | GP26 (ADC0) | Độ ẩm đất. Vị trí thanh trượt gần bằng độ ẩm (%). Kéo về trái cùng (dưới ~5%) để giả lập tuột dây cảm biến |
| DHT22 (`dht1`) | GP15 | Nhiệt độ, độ ẩm không khí. Bấm vào linh kiện để chỉnh |
| Cảm biến ánh sáng (`ldr1`) | GP27 (ADC1), chân AO | Độ sáng (lux) |
| HC-SR04 (`sonar1`) | TRIG GP10, ECHO GP11 | Mực nước bồn: khoảng cách 100 cm là cạn, 10 cm là đầy |
| LCD 1602 I2C (`lcd1`) | SDA GP4, SCL GP5 | Màn hình, địa chỉ 0x27 |
| LED xanh dương `BOM` (`led1`) | GP16 | Máy bơm |
| LED trắng `PHUN` (`led2`) | GP17 | Máy phun sương |
| LED đỏ `CANH BAO` (`led3`) | GP18 | Bơm bị khóa, hết nước, hoặc lỗi cảm biến đất |
| Nút xanh lá (`btn1`) | GP14 | Mở khóa bơm |

- Chân được khai báo trong [`include/config.h`](../include/config.h). Đổi chân thì sửa cả `diagram.json`.
- Pico 2 W chỉ có 3 chân đọc analog (GP26, GP27, GP28): 2 chân đang dùng, còn trống GP28.
- Wokwi chỉ mô phỏng 1 trong 2 nhân của RP2350. Firmware này chỉ dùng 1 nhân nên không bị ảnh hưởng.

## 4. Màn hình LCD

Màn hình chính có 2 trang, mỗi trang hiện 4 giây:

```text
Dat: 57%   14:05      độ ẩm đất + giờ (--:-- khi chưa có giờ). Cảm biến lỗi: LOI CAM BIEN DAT
Bom:CHO Phun:OFF      trạng thái bơm (bảng dưới) + phun sương ON/OFF

T:30.0C  H: 60%       nhiệt độ, độ ẩm không khí (DHT22)
Nuoc: 78%  800lx      mực nước bồn + độ sáng
```

| Mã bơm | Nghĩa |
|---|---|
| `OFF` | Không tưới |
| `ON` | Bơm đang chạy |
| `CHO` | Đang trong đợt tưới, chờ nước ngấm giữa 2 lần bơm |
| `NGI` | Đợt trước chưa đủ ẩm, đang nghỉ 60 giây |
| `LOI` | Bơm bị khóa: bơm đủ 8 lần mà độ ẩm không tăng (hỏng bơm, tuột ống). Bấm nút xanh lá để mở khóa |
| `HET` | Bồn hết nước (dưới 10%) |
| `---` | Cảm biến đất lỗi |

Khi có kết quả AI, LCD hiện 2 dòng kết quả trong 20 giây (dòng dài hơn 16 ký tự sẽ chạy chữ) rồi quay về màn hình chính.

## 5. Logic điều khiển

Logic nằm trong [`lib/FarmCore`](../lib/FarmCore/src/FarmCore.h), là C++ thuần, không phụ thuộc phần cứng. Các thông số đặt ở đầu [`src/main.cpp`](../src/main.cpp).

**Tưới:**
- Độ ẩm dưới 50% liên tục 10 giây thì bắt đầu một đợt tưới.
- Mỗi lần bơm chạy 6 giây, rồi chờ nước ngấm 5 giây. Lên tới 65% thì dừng.
- Bơm đủ 8 lần mà chưa đủ ẩm:
  - Độ ẩm tăng dưới 5% thì **khóa bơm** (nghi hỏng bơm hoặc tuột ống).
  - Nếu có tăng thì nghỉ 60 giây rồi tưới tiếp. 3 đợt liền chưa đủ ẩm cũng khóa bơm.
- Bồn dưới 10% thì không bơm, lên lại 15% mới bơm tiếp. Cảm biến đất lỗi thì không bơm.
- Thời gian chờ nước ngấm và thời gian nghỉ đã được rút ngắn để xem mô phỏng nhanh. Vườn thật nên để khoảng 60 giây và 30 phút.

**Phun sương:**

| Điều kiện | Phun sương |
|---|---|
| Lá khỏe hoặc chưa có kết quả AI, 8h–16h | 30 giây, 30 phút một lần (đúng :00 và :30) |
| Sau 16h và ban đêm | Không phun, để lá kịp khô trước tối |
| AI báo bệnh nấm hoặc vi khuẩn, hoặc nghi bệnh bất kỳ (`MIST:2`) | Dừng hẳn, đến khi AI báo lá khỏe lại |
| AI chắc chắn nhện đỏ (`MIST:3`) | 30 giây, 15 phút một lần, vẫn chỉ 8h–16h |
| Virus khảm lá, xoăn vàng lá | Phun bình thường |
| Không khí ẩm từ 85% (DHT22) | Không phun |
| Bồn hết nước | Không phun |
| Chưa có giờ | Không phun |

## 6. Lệnh và MQTT

Gõ lệnh vào terminal Wokwi, hoặc gửi tới topic `<tiền tố>/cmd`:

| Lệnh | Việc làm |
|---|---|
| `AI:<dòng 1>\|<dòng 2>` | Kết quả AI, LCD hiện 20 giây. Pico trả lời `AI OK`. `AI:` để trống thì thôi hiện |
| `MIST:<1-3>` | Chế độ phun: 1 bình thường, 2 dừng (lá bệnh), 3 phun tăng (nhện đỏ) |
| `TIME:hh:mm:ss` | Chỉnh giờ bằng tay để thử nhanh. NTP sẽ chỉnh lại giờ thật sau tối đa 1 giờ |
| `UNLOCK` | Mở khóa bơm, giống bấm nút |

Tiền tố topic hiện là `tuoicaytt/55afb0`. Nó được khai báo ở 2 nơi phải giống nhau: `MQTT_TOPIC_PREFIX` trong [`include/config.h`](../include/config.h) và trong [`ai/config.py`](../ai/config.py).

| Topic | Chiều | Nội dung |
|---|---|---|
| `.../telemetry` | Pico → máy tính | JSON, mỗi 5 giây (ví dụ bên dưới) |
| `.../cmd` | máy tính → Pico | Lệnh như bảng trên |
| `.../ack` | Pico → máy tính | Trả lời từng lệnh, ví dụ `AI OK`, `MIST OK 2` |
| `.../status` | Pico → máy tính | `online`, hoặc `offline` (broker tự gửi khi Pico mất kết nối). Giữ lại trên broker |

```json
{"moisture":57,"soil_raw":2457,"soil_fault":false,"temperature":30,"air_humidity":60,"lux":800,
 "tank_level":78,"tank_empty":false,"pump":false,"pump_state":"off","pulses":0,"mist":false,
 "mist_mode":1,"mist_reason":"outside_hours","time":"14:05:12","uptime_s":95}
```

Muốn xem dữ liệu mà không cần chạy AI, dùng một MQTT client bất kỳ, ví dụ [HiveMQ WebSocket Client](https://www.hivemq.com/demos/websocket-client/). Kết nối `broker.hivemq.com` rồi subscribe `tuoicaytt/55afb0/#`.

## 7. Kịch bản thử

| # | Làm gì | Kết quả đúng |
|---|---|---|
| 1 | Kéo thanh trượt xuống ~40% | Sau 10 giây LED bơm sáng 6 giây, tắt 5 giây, lặp lại. LCD hiện `Bom:ON` ↔ `Bom:CHO` |
| 2 | Trong lúc đang tưới, kéo lên ~70% | Dừng tưới, LCD `Bom:OFF` |
| 3 | Kéo xuống ~40% và để yên | Bơm đủ 8 lần mà độ ẩm không tăng: `Bom:LOI`, LED đỏ sáng. Bấm nút xanh lá để mở khóa |
| 4 | Bấm vào HC-SR04, kéo khoảng cách lên trên 91 cm | `Bom:HET`, LED đỏ sáng, không bơm, không phun. Kéo về 30 cm thì chạy lại |
| 5 | Kéo thanh trượt về trái cùng | `LOI CAM BIEN DAT`, LED đỏ sáng, bơm tắt |
| 6 | Khi LCD đã có giờ, gõ `TIME:07:59:50` | 10 giây sau LED phun sáng 30 giây |
| 7 | Chỉnh DHT22 độ ẩm lên 90%, gõ `TIME:08:29:50` | Không phun (lý do `air_humid` trong log) |
| 8 | Gõ `MIST:2` rồi `TIME:08:29:50` | Không phun (lá bệnh) |
| 9 | Gõ `MIST:3` rồi `TIME:08:14:50` | Phun lúc 08:15 (15 phút một lần) |
| 10 | Gõ `AI:BENH 97%\|Ca chua - moc suong` | LCD hiện 20 giây, dòng 2 chạy chữ, Pico trả lời `AI OK` |
| 11 | Chạy `python service.py --once` (mục 8) | LCD hiện kết quả AI của một ảnh lá ngẫu nhiên, chế độ phun đổi theo bệnh |

Nếu terminal Wokwi không hiện chữ bạn gõ, dùng Serial Monitor của PlatformIO qua cổng RFC2217 (có hiện chữ đang gõ):

```bash
pio device monitor -p rfc2217://localhost:4000 -b 115200 --echo --eol LF
```

## 8. Nối AI trên máy tính

Khi mô phỏng đang chạy, từ thư mục `ai/` (đã bật venv và cài `requirements.txt`):

```bash
python service.py --once          # phân tích 1 ảnh ngẫu nhiên trong data/test_real, gửi cho Pico rồi thoát
python service.py --interval 2    # chạy liên tục, 2 phút phân tích 1 lần (để demo)
python service.py                 # chạy liên tục, phân tích lúc 08:00 và 16:00 (config.CAPTURE_TIMES)
python service.py --once --image anh_la.jpg
```

Không có camera thật, nên mỗi lần phân tích service chọn ngẫu nhiên một ảnh trong `data/test_real` (ảnh vườn thật, không dùng để train). Service in số liệu Pico gửi lên mỗi phút và ghi vào `ai/logs/sensors.csv`.

## 9. Giới hạn

- **Không có camera:** ảnh đầu vào lấy từ thư mục ảnh mẫu.
- **Độ ẩm không tự tăng khi bơm chạy**, vì thanh trượt chỉnh bằng tay. Để yên thì sau 8 lần bơm firmware sẽ khóa bơm. Muốn mô phỏng vòng kín thì bước tiếp theo là viết custom chip "đất + cây" (Wokwi Chips API).
- **Broker công khai:** ai biết topic đều đọc và gửi lệnh được. Chỉ dùng để demo.
- **Wokwi miễn phí** dùng Public Gateway: Pico chỉ kết nối ra ngoài được, máy tính không gọi thẳng vào Pico.
