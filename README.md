# Hệ thống tưới cây thông minh và phát hiện bệnh cây

Repository này chứa toàn bộ mã nguồn cho hệ thống tưới cây tự động kết hợp **Arduino Nano**, **ESP32-CAM** và mô hình AI chạy trên máy tính.

Hệ thống có thể:

- Đo độ ẩm đất bằng cảm biến analog.
- Tự động bật/tắt máy bơm theo ngưỡng độ ẩm.
- Hiển thị độ ẩm, trạng thái bơm và quạt trên LCD 1602.
- Bảo vệ máy bơm khi cảm biến lỗi, hết nước hoặc bơm không làm độ ẩm tăng.
- Điều khiển quạt thông gió dựa trên kết quả phát hiện bệnh.
- Dùng ESP32-CAM để chụp ảnh cây qua Wi-Fi.
- Dùng 2 mô hình trên máy tính: YOLO26n tìm từng chiếc lá trong ảnh (bỏ qua quả, đất, chậu), rồi EfficientNet-B0/PyTorch chấm bệnh từng lá và bỏ phiếu để cảnh báo cây khỏe, nghi bệnh hoặc bị bệnh.
- Gửi kết quả AI về Arduino Nano và lưu ảnh, log kết quả theo thời gian.

> AI chỉ có mục đích cảnh báo sớm. Kết quả cần được kiểm tra lại bằng mắt thực tế tại vườn.

## Kiến trúc hệ thống

```text
ESP32-CAM ── Wi-Fi ──> Máy tính chạy AI ── USB Serial ──> Arduino Nano
     │                         │                              │
     │                         ├─ Phân tích ảnh cây            ├─ Cảm biến độ ẩm đất
     │                         ├─ Lưu ảnh và log               ├─ Relay máy bơm
     │                         └─ Gửi kết quả AI               ├─ Relay quạt
     │                                                        └─ LCD 1602
     └─ UART qua module chuyển mức BSS138 <────────────────────┘
```

## Cấu trúc thư mục

```text
.
└── Projects/
    └── heThongTuoiCayTT/
        ├── platformio.ini       # Firmware Arduino Nano
        ├── src/main.cpp         # Logic đo ẩm, tưới cây, LCD, relay và giao tiếp AI
        ├── include/             # Header của firmware Nano
        ├── esp32cam/             # Firmware ESP32-CAM
        │   ├── platformio.ini
        │   ├── src/
        │   ├── include/
        │   └── secrets.example.ini
        ├── ai/                   # Mã nguồn huấn luyện và chạy AI
        │   ├── service.py        # Dịch vụ chạy tự động hằng ngày
        │   ├── predict.py        # Phân tích ảnh/webcam (tìm lá + chấm từng lá)
        │   ├── detector.py       # Chạy model tìm lá (ONNX)
        │   ├── train_detector.py # Huấn luyện model tìm lá YOLO26n
        │   ├── train.py          # Huấn luyện mô hình
        │   ├── evaluate.py       # Đánh giá mô hình
        │   ├── prepare_data.py   # Chuẩn bị dữ liệu
        │   ├── prepare_detector_data.py  # Chuẩn bị dữ liệu tìm lá
        │   └── requirements.txt
        ├── docs/                 # Tài liệu đấu nối và vận hành
        └── test/                 # Test PlatformIO
```

## Yêu cầu phần cứng

- Arduino Nano ATmega328P.
- ESP32-CAM AI-Thinker và đế ESP32-CAM-MB.
- Cảm biến độ ẩm đất analog.
- Bơm nước mini và relay 5 V.
- Quạt thông gió và relay.
- LCD 1602.
- Module chuyển mức logic BSS138 cho giao tiếp UART giữa Nano và ESP32-CAM.
- Máy tính chạy Python để phân tích ảnh AI.
- Nguồn riêng 5 V tối thiểu 2 A cho ESP32-CAM khi chạy thực tế.

Chi tiết sơ đồ nối dây xem trong [`Projects/heThongTuoiCayTT/docs/`](Projects/heThongTuoiCayTT/docs/).

## Cài đặt công cụ

### PlatformIO

Cài VS Code cùng extension **PlatformIO IDE**, hoặc cài PlatformIO CLI:

```bash
python -m pip install platformio
```

### Python AI

Tạo môi trường ảo và cài thư viện:

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

### 1. Nạp firmware cho Arduino Nano

Thực hiện từ thư mục project PlatformIO:

```bash
cd Projects/heThongTuoiCayTT
pio run -t upload
```

Mở Serial Monitor ở tốc độ 9600 baud nếu cần theo dõi dữ liệu:

```bash
pio device monitor -b 9600
```

### 2. Cấu hình và nạp ESP32-CAM

Tạo file cấu hình bí mật từ file mẫu:

```bash
cd Projects/heThongTuoiCayTT/esp32cam
cp secrets.example.ini secrets.ini
```

Cập nhật mật khẩu OTA trong `secrets.ini`. Thông tin Wi-Fi được cấu hình trong phần `include/` của firmware theo hướng dẫn trong thư mục `esp32cam`.

Lần đầu nạp bằng USB:

```bash
cd Projects/heThongTuoiCayTT
pio run -d esp32cam -e esp32cam -t upload
pio device monitor -d esp32cam -e esp32cam
```

Nếu ESP32-CAM không kết nối được khi nạp, giữ nút **IO0**, bấm **RST**, sau đó thử lại.

Sau khi OTA đã hoạt động, có thể nạp qua Wi-Fi:

```bash
pio run -d esp32cam -t upload
```

Mặc định ESP32-CAM cung cấp các endpoint:

- `http://esp32cam.local/` — trang kiểm tra camera.
- `http://esp32cam.local/capture` — chụp và trả về ảnh JPEG.
- `http://esp32cam.local/status` — trạng thái Wi-Fi, camera, độ ẩm và kết quả AI.
- `POST /result` — nhận kết quả AI từ máy tính.

Nếu tên `esp32cam.local` không hoạt động, dùng địa chỉ IP hiển thị trong Serial Monitor.

### 3. Chạy AI

Từ thư mục `ai/`, sau khi đã bật môi trường ảo:

```bash
python service.py
```

Dịch vụ sẽ chạy liên tục, chụp ảnh theo lịch trong `config.py`, phân tích ảnh, gửi kết quả về Arduino Nano và lưu dữ liệu.

Một số lệnh hữu ích:

```bash
# Chạy một lần rồi thoát
python service.py --once

# Phân tích một ảnh có sẵn
python service.py --once --image anh_cay.jpg

# Chạy không cần kết nối Arduino Nano
python service.py --no-serial

# Chỉ định IP camera khi mDNS không hoạt động
python service.py --camera-url http://192.168.1.50/capture

# Chỉ định cổng Serial của Arduino Nano
python service.py --port /dev/ttyACM0
```

Trong lúc `service.py` đang chạy, không mở thêm Serial Monitor trên cùng cổng USB.

## Huấn luyện và đánh giá mô hình AI

Nếu đã có dữ liệu huấn luyện, chạy:

```bash
cd Projects/heThongTuoiCayTT/ai
source .venv/bin/activate

python prepare_detector_data.py
python train_detector.py
python prepare_data.py
python train.py
python evaluate.py
python evaluate.py --scenes
```

Model bệnh được lưu tại `models/best.pt`, model tìm lá tại `models/leaf_detector.onnx` (cách tải dữ liệu xem `ai/README.md`). Có thể thử dự đoán:

```bash
python predict.py data/test_real/Tomato_Late_blight/pd_02010.jpg
python predict.py anh_cay.jpg --save anh_cay_ai.jpg
python predict.py --webcam
```

Mô hình phân loại ảnh thành các mức cảnh báo chính:

- `KHOE`: xác suất bệnh thấp.
- `NGHI`: có dấu hiệu nghi ngờ, cần kiểm tra thêm.
- `BENH`: có dấu hiệu bệnh rõ hơn theo ngưỡng cấu hình.

Chi tiết về dữ liệu, các lớp bệnh, ngưỡng AI và quy trình cải thiện model xem tại [`Projects/heThongTuoiCayTT/ai/README.md`](Projects/heThongTuoiCayTT/ai/README.md).

## Cấu hình tưới cây

Các tham số chính nằm trong `Projects/heThongTuoiCayTT/src/main.cpp`:

- `MOISTURE_LOW`: ngưỡng bắt đầu tưới.
- `MOISTURE_HIGH`: ngưỡng dừng tưới.
- `airValue`, `waterValue`: giá trị hiệu chuẩn cảm biến.
- `PUMP_FLOW_ML_PER_MIN`: lưu lượng bơm thực tế.
- `MAX_PULSES`: số lần bơm tối đa trong một đợt.
- `REST_MS`: thời gian nghỉ sau một đợt tưới chưa đạt độ ẩm.
- `FAN_DISEASE_ON_MS`, `FAN_DISEASE_OFF_MS`: chu kỳ quạt khi phát hiện bệnh.

Sau khi thay đổi cấu hình, nạp lại firmware Arduino:

```bash
cd Projects/heThongTuoiCayTT
pio run -t upload
```

## Giao tiếp Serial

Arduino Nano nhận kết quả AI theo định dạng:

```text
AI:BENH 97%
```

Arduino gửi trạng thái độ ẩm sang ESP32-CAM theo định dạng:

```text
SOIL:<do_am>,<raw>,<bom>,<loi_cam_bien>,<quat>
```

Ví dụ:

```text
SOIL:45,812,0,0,1
```

Có thể kiểm tra nhanh mà không cần chạy Python bằng cách mở Serial Monitor ở 9600 baud và gửi:

```text
AI:Test 123
```

## Dữ liệu và log

Khi chạy AI service, các dữ liệu thường được lưu trong thư mục `ai/`:

- `captures/<ngày>/`: ảnh gốc và ảnh đã vẽ vùng bệnh.
- `logs/ai_results.csv`: lịch sử kết quả phân tích.
- `logs/moisture.csv`: lịch sử độ ẩm và trạng thái thiết bị.
- `models/best.pt`: model AI tốt nhất sau khi huấn luyện.

## Xử lý sự cố thường gặp

- **Không mở được cổng Serial:** kiểm tra cáp USB, tên cổng và đóng Serial Monitor khác.
- **LCD không hiện kết quả AI:** nạp lại firmware Nano và kiểm tra service có nhận phản hồi `AI OK` hay không.
- **Không truy cập được camera:** kiểm tra Wi-Fi 2.4 GHz, dùng IP thay cho `esp32cam.local`.
- **ESP32-CAM tự khởi động lại:** sử dụng nguồn 5 V tối thiểu 2 A, dây nguồn ngắn.
- **AI báo bệnh sai nhiều:** cải thiện góc chụp, đưa camera gần lá hơn và điều chỉnh ngưỡng trong `ai/config.py`.
- **Bơm bị khóa:** kiểm tra mực nước, ống dẫn, lưu lượng bơm và nhấn RESET Arduino sau khi xử lý.

## Lưu ý an toàn

- Không cấp trực tiếp điện áp cao cho Arduino, ESP32-CAM hoặc relay nếu chưa có mạch chuyển đổi phù hợp.
- Tách nguồn bơm/quạt khỏi nguồn logic khi cần để giảm nhiễu và sụt áp.
- Đảm bảo mạch điện được che chắn khỏi nước.
- Kiểm tra relay ở trạng thái tắt trước khi lắp bơm vào hệ thống.
