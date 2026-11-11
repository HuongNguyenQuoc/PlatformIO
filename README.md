# Hệ thống nông nghiệp thông minh tích hợp AI

Đề tài: **Nghiên cứu, phát triển hệ thống nông nghiệp thông minh có tích hợp AI**. Hệ thống tự tưới cây theo độ ẩm đất, phun sương theo giờ, và dùng AI phát hiện bệnh lá cà chua để điều chỉnh việc phun sương.

Toàn bộ hệ thống chạy bằng **mô phỏng**, không cần mạch thật:

- **Raspberry Pi Pico 2 W** (vi điều khiển có Wi-Fi) chạy trên trình mô phỏng [Wokwi](https://wokwi.com) trong VS Code, gồm cảm biến, LED (thay cho bơm và máy phun sương) và LCD.
- **AI** chạy trên máy tính.
- Hai bên trao đổi dữ liệu qua **MQTT**.

Hệ thống có thể:

- Đo độ ẩm đất, nhiệt độ và độ ẩm không khí (DHT22), ánh sáng (quang trở), mực nước bồn (cảm biến siêu âm HC-SR04).
- Tự tưới theo ngưỡng độ ẩm: tưới từng lần ngắn rồi chờ nước ngấm.
- Bảo vệ bơm: khóa bơm khi bơm nhiều lần mà độ ẩm không tăng, ngừng bơm khi bồn hết nước hoặc cảm biến lỗi.
- Phun sương theo giờ (8h–16h). Không phun khi không khí đã ẩm, và dừng phun khi AI báo lá bị bệnh nấm hoặc vi khuẩn.
- Lấy giờ thật qua NTP. Hiển thị số liệu và kết quả AI trên LCD 1602.
- Gửi số liệu lên máy tính qua MQTT mỗi 5 giây và nhận lệnh từ máy tính.
- Dùng 2 mô hình AI: YOLO26n tìm từng chiếc lá trong ảnh, rồi EfficientNet-B0 (PyTorch) chấm bệnh từng lá và bỏ phiếu để báo cây khỏe, nghi bệnh hay bị bệnh.
- Lưu ảnh đã phân tích, kết quả AI và số liệu cảm biến ra file CSV.

> AI chỉ có mục đích cảnh báo sớm. Kết quả cần được kiểm tra lại bằng mắt.

## Kiến trúc hệ thống

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

- Wi-Fi giả lập của Wokwi có Internet, nhưng chỉ kết nối ra ngoài được. Vì vậy Pico và máy tính đều kết nối ra cùng một MQTT broker công khai.
- Không có camera thật: mỗi lần phân tích, AI lấy ngẫu nhiên một ảnh lá trong thư mục ảnh mẫu.

## Cấu trúc thư mục

```text
.
└── Projects/
    └── heThongTuoiCayTT/
        ├── platformio.ini        # Build firmware Pico
        ├── wokwi.toml            # Cấu hình trình mô phỏng Wokwi
        ├── diagram.json          # Sơ đồ mạch mô phỏng
        ├── src/main.cpp          # Firmware Pico 2 W: cảm biến, LED, LCD, Wi-Fi, NTP, MQTT
        ├── include/config.h      # Chân Pico 2 W, Wi-Fi, MQTT
        ├── lib/FarmCore/         # Logic tưới và phun sương (C++ thuần, không phụ thuộc phần cứng)
        ├── ai/                   # AI phát hiện bệnh lá
        │   ├── service.py        # Dịch vụ: phân tích ảnh, gửi kết quả cho Pico qua MQTT, ghi log
        │   ├── predict.py        # Phân tích ảnh/webcam (tìm lá + chấm từng lá)
        │   ├── detector.py       # Chạy model tìm lá (ONNX)
        │   ├── train_detector.py # Huấn luyện model tìm lá YOLO26n
        │   ├── train.py          # Huấn luyện model bệnh
        │   ├── evaluate.py       # Đánh giá model
        │   ├── prepare_data.py   # Chuẩn bị dữ liệu
        │   ├── prepare_detector_data.py  # Chuẩn bị dữ liệu tìm lá
        │   ├── config.py         # Cấu hình AI, thư mục ảnh mẫu, MQTT
        │   └── requirements.txt
        └── docs/wokwi.md         # Hướng dẫn mô phỏng, linh kiện, logic điều khiển, kịch bản thử
```

## Cài đặt

1. Cài VS Code với 2 extension **PlatformIO IDE** và **Wokwi Simulator**.
2. Kích hoạt license Wokwi (miễn phí): nhấn `F1` → **Wokwi: Request a New License**.
3. Tạo môi trường Python cho AI:

```bash
cd Projects/heThongTuoiCayTT/ai
python -m venv .venv

# Linux/macOS
source .venv/bin/activate
# Windows PowerShell
# .venv\Scripts\Activate.ps1

python -m pip install --upgrade pip
pip install -r requirements.txt
```

## Chạy nhanh

**1. Build firmware và chạy mô phỏng:**

```bash
cd Projects/heThongTuoiCayTT
pio run
```

Sau đó nhấn `F1` → **Wokwi: Start Simulator**. Đợi terminal Wokwi in `Da ket noi MQTT` và LCD hiện giờ.

**2. Chạy AI** (từ thư mục `ai/`, đã bật venv và có model trong `ai/models/`):

```bash
python service.py --once          # phân tích 1 ảnh ngẫu nhiên, gửi kết quả cho Pico rồi thoát
python service.py --interval 2    # chạy liên tục, 2 phút 1 lần (để demo)
```

LCD mô phỏng sẽ hiện kết quả AI trong 20 giây, và chế độ phun sương đổi theo bệnh.

Cách thao tác với mạch, các lệnh và 11 kịch bản thử (tưới, khóa bơm, hết nước, lỗi cảm biến, phun theo giờ, AI báo bệnh): xem [`docs/wokwi.md`](Projects/heThongTuoiCayTT/docs/wokwi.md).

## Huấn luyện và đánh giá mô hình AI

```bash
cd Projects/heThongTuoiCayTT/ai
python prepare_detector_data.py
python train_detector.py
python prepare_data.py
python train.py
python evaluate.py
python evaluate.py --scenes
```

- Model bệnh lưu tại `models/best.pt`, model tìm lá tại `models/leaf_detector.onnx`. Cách tải dữ liệu xem `ai/README.md`.
- Thư mục ảnh mẫu mặc định cho `service.py` là `data/test_real`, do `prepare_data.py` tạo ra.

Mô hình báo 3 mức: `KHOE` (xác suất bệnh thấp), `NGHI` (nghi ngờ, cần kiểm tra thêm), `BENH` (có dấu hiệu bệnh rõ theo ngưỡng cấu hình).

Chi tiết về dữ liệu, các lớp bệnh, ngưỡng AI, kết quả đánh giá và cách cải thiện model: xem [`ai/README.md`](Projects/heThongTuoiCayTT/ai/README.md).

## Cấu hình

| Muốn đổi | Sửa ở đâu |
|---|---|
| Ngưỡng tưới, thời gian bơm, lịch phun sương, ngưỡng độ ẩm không khí, kích thước bồn | Đầu file `src/main.cpp` |
| Chân Pico 2 W | `include/config.h` (sửa cả `diagram.json`) |
| MQTT broker, tiền tố topic | `include/config.h` **và** `ai/config.py` (2 nơi phải giống nhau) |
| Ngưỡng AI, giờ phân tích, thư mục ảnh mẫu | `ai/config.py` |

## Dữ liệu và log

Khi chạy `service.py`, dữ liệu được lưu trong thư mục `ai/`:

- `captures/<ngày>/`: ảnh đã phân tích và ảnh có vẽ khung từng lá.
- `logs/ai_results.csv`: lịch sử kết quả phân tích.
- `logs/sensors.csv`: số liệu cảm biến và trạng thái bơm, phun sương, mỗi phút một dòng.

## Xử lý sự cố thường gặp

- **Wokwi báo thiếu firmware:** chạy `pio run` trước khi **Start Simulator**.
- **Terminal Wokwi không có `Da ket noi MQTT`:** kiểm tra Internet. Broker công khai đôi khi lỗi: đổi `MQTT_HOST` ở cả `include/config.h` và `ai/config.py`.
- **LCD hiện `--:--`:** Pico chưa lấy được giờ qua NTP (cần Wi-Fi), nên chưa phun sương. Có thể gõ `TIME:hh:mm:ss` để thử.
- **LCD không hiện kết quả AI:** xem service có in `[Pico] AI OK` không, và tiền tố topic 2 bên có giống nhau không.
- **Bơm bị khóa (`Bom:LOI`):** bấm nút xanh lá trong mạch mô phỏng, hoặc gõ `UNLOCK`.
