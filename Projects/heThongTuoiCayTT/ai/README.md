# AI phát hiện bệnh cây – Hệ thống tưới cây TT

Máy tính dùng 2 model để phân tích ảnh lá cà chua: YOLO26n tìm từng chiếc lá, rồi CNN (EfficientNet-B0, PyTorch) chấm bệnh từng lá. Kết quả được gửi qua MQTT cho Pico 2 W đang chạy mô phỏng trên Wokwi. Pico 2 W hiện kết quả lên LCD 20 giây, rồi quay về màn hình chính (độ ẩm, giờ, bơm, phun sương). Khi lá bị bệnh nấm hoặc vi khuẩn, Pico 2 W dừng phun sương.

Hệ thống chỉ chạy mô phỏng, không có camera thật. Mỗi lần phân tích, service lấy ngẫu nhiên một ảnh trong thư mục ảnh mẫu (mặc định `data/test_real`).

```text
data/test_real/ (ảnh mẫu) ──▶ Máy tính: ai/service.py
                                ├─ predict.py: YOLO tìm lá, CNN chấm từng lá, bỏ phiếu
                                ├─ captures/<ngày>/  ảnh đã phân tích + ảnh có khung
                                ├─ logs/ai_results.csv, logs/sensors.csv
                                └─ MQTT "MIST:2", "AI:BENH 97%|Ca chua - moc suong" ──▶ Pico 2 W (Wokwi) ──▶ LCD (20 giây)
```

## Các file

| File | Việc làm |
|---|---|
| `config.py` | Mọi cấu hình: đường dẫn, 10 lớp lá cà chua, ngưỡng bệnh, giờ phân tích, thư mục ảnh mẫu, MQTT |
| `prepare_data.py` | Gom PlantVillage và PlantDoc thành `data/train`, `val`, `test_pv`, `test_real`, `test_leaf` |
| `prepare_detector_data.py` | Chuyển PlantDoc Object Detection (khung từng lá) sang dạng YOLO: `data/detector/` |
| `train_detector.py` | Train model tìm lá YOLO26n, xuất `models/leaf_detector.onnx` |
| `detector.py` | Chạy model tìm lá bằng onnxruntime, cắt vùng lá |
| `model.py` | Tạo EfficientNet-B0, augmentation, đọc model đã train |
| `train.py` | Train 2 pha (đóng băng backbone, rồi fine-tune toàn bộ) và lưu `models/best.pt` |
| `evaluate.py` | Chấm điểm trên tập test: accuracy, F1, ma trận nhầm lẫn, chỉ số khỏe/bệnh |
| `predict.py` | Phân tích ảnh hoặc webcam: tìm lá, chấm từng lá, bỏ phiếu, vẽ khung từng lá |
| `service.py` | Chạy liên tục: lấy ảnh mẫu theo giờ, phân tích, ghi log, gửi kết quả cho Pico 2 W qua MQTT |
| `../src/main.cpp` | Pico 2 W (Wokwi): cảm biến, tưới, phun sương theo giờ và theo AI, LCD, MQTT. Xem `../docs/wokwi.md` |

Mọi lệnh Python bên dưới đều chạy trong thư mục `ai/`, sau khi đã bật venv:

```bash
cd ~/Documents/PlatformIO/Projects/heThongTuoiCayTT/ai
source .venv/bin/activate
```

## 1. Huấn luyện model (làm 1 lần)

Tải dữ liệu PlantDoc Object Detection (ảnh vườn thật, mỗi chiếc lá có 1 khung, ~1.9 GB) một lần:

```bash
git clone --depth 1 https://github.com/pratikkayal/PlantDoc-Object-Detection-Dataset.git data/raw/plantdoc_od/repo
pip install -r requirements.txt   # thêm onnxruntime, ultralytics
```

```bash
python prepare_detector_data.py   # ~1 phút, tạo data/detector/
python train_detector.py          # ~40 phút trên RTX 2050, xuất models/leaf_detector.onnx (2 lớp: lá cà chua / lá khác)
python prepare_data.py            # ~3 phút
python train.py                   # ~25 phút trên RTX 2050, lưu models/best.pt
python evaluate.py                # chấm từng ảnh/lá trên test_pv, test_real, test_leaf
python evaluate.py --scenes       # chấm cả quy trình predict.py (tìm lá + bỏ phiếu) trên ảnh test_real
```

Ảnh nào trùng với `test_real` đều bị loại khỏi dữ liệu train của cả 2 model, để điểm chấm vẫn là điểm thật.

Kết quả ngày 18/10/2026, **chỉ cà chua**: model bệnh 10 lớp, model tìm lá 2 lớp (lá cà chua / lá cây khác). Các tập test chỉ còn ảnh cà chua; số trong ngoặc là model 15 lớp ngày 17/10 chấm trên đúng các ảnh đó:

| Tập | Số ảnh | Đoán đúng tên bệnh (10 lớp) | Báo "Benh" đúng (ngưỡng 90%) |
|---|---|---|---|
| `test_pv` (PlantVillage) | 1816 | 99,5% | phát hiện 1654/1657 ảnh bệnh, báo nhầm 0/159 ảnh khỏe |
| `test_real` (PlantDoc, cả ảnh) | 69 | **72,5%** (60,9%) | phát hiện 53/61 ảnh bệnh, báo nhầm 0/8 ảnh khỏe |
| `test_leaf` (từng lá cắt từ ảnh test_real) | 195 | **74,4%** (64,6%) | phát hiện 144/168 lá bệnh, báo nhầm 1/27 lá khỏe |

**Cả quy trình** (`python evaluate.py --scenes`: tìm lá → chấm từng lá → bỏ phiếu) trên 69 ảnh cà chua của `test_real`:

| | Cách cũ: chia ô 3×3, model 07/10 | Tìm lá 1 lớp + model 10 lớp | **Đang dùng**: tìm lá 2 lớp + model 10 lớp |
|---|---|---|---|
| 61 ảnh bệnh → Bệnh / Nghi / Khỏe / Không thấy lá | 53 / 5 / 2 / 1 | 59 / 2 / 0 / 0 | 56 / 3 / 0 / 2 |
| 8 ảnh khỏe → Bệnh / Nghi / Khỏe / Không thấy lá | 0 / 5 / 3 / 0 | 1 / 2 / 5 / 0 | 0 / 3 / 4 / 1 |
| Đúng tên bệnh (61 ảnh bệnh) | 37 | 47 | 45 |
| Lá cây khác (tập val) bị coi là lá cà chua | – | 76,9% | **4,0%** |

- Model tìm lá 2 lớp bỏ được gần hết lá cây khác: trong 299 ảnh val chỉ có cây khác, 276 ảnh ra "không thấy lá". Đổi lại, nó nhận nhầm vài lá cà chua thật thành "lá khác": 3/69 ảnh cà chua thành `KHONG THAY LA`.
- Ảnh minh họa lá + quả cà chua ở `captures/images.jpeg` cũng bị coi là lá cây khác (90%) nên ra `KHONG THAY LA`. Với model tìm lá 1 lớp thì ảnh này ra `KHOE 74%`.
- Muốn quay lại model tìm lá 1 lớp (không loại lá cây khác): chép `models/leaf_detector_1lop.onnx` đè lên `models/leaf_detector.onnx` (và vào `assets/` của app), đặt `DET_CONF = 0.35`.
- Ảnh camera chụp nhầm trần nhà: trước báo `NGHI 75%`, nay là `KHONG THAY LA`.

- Ngoài vườn, model **phân biệt khỏe hay bệnh khá tốt**, nhưng **hay nhầm giữa các bệnh trông giống nhau**.
  - Ví dụ: đốm vi khuẩn bị nhầm thành Septoria, khảm lá bị nhầm thành nấm mốc lá.
  - Vì vậy, LCD chỉ báo mức Khỏe / Nghi / Bệnh. Tên bệnh (in ra màn hình và trên app) chỉ là **gợi ý**. Xem chi tiết trong `logs/evaluate.log`.
- **Vì sao ngưỡng "Benh" đặt cao (90%):** ảnh lá khỏe ngoài vườn hay bị chấm xác suất bệnh 60–90%. Với ngưỡng 90%, các ảnh đó rơi vào mức "Nghi" thay vì "Benh". Ngưỡng được chọn dựa trên rất ít ảnh khỏe (8 ảnh cà chua), nên khi có ảnh vườn của bạn, hãy chỉnh lại.

> **Điểm `test_real` mới là điểm thật.** PlantVillage là ảnh một chiếc lá đặt trên nền xám, chụp trong phòng lab, nên model đạt gần 100% trên đó. Ảnh ngoài vườn khó hơn nhiều. Cách cải thiện tốt nhất là tự gom ảnh vườn của bạn (xem mục 7).

## 2. Thử model

```bash
python predict.py data/test_real/Tomato_Late_blight/pd_02010.jpg
python predict.py data/test_real/Tomato_healthy/*.jpg           # nhiều ảnh, mỗi ảnh 1 dòng
python predict.py anh_cay.jpg --save anh_cay_ai.jpg              # lưu ảnh có vẽ khung vùng bệnh
python predict.py --webcam                                       # chụp bằng webcam laptop (đưa 1 chiếc lá lại gần)
```

**Cách đọc kết quả** (xác suất bệnh là của lá bệnh thứ 2, xem "Tìm lá trước, rồi bỏ phiếu" bên dưới):

| LCD hiện | Nghĩa |
|---|---|
| `KHOE 85%` | Xác suất bệnh dưới 60%. Số hiện là mức tin cây khỏe |
| `NGHI 75%` | 60–90%: nghi ngờ, nên ra xem cây |
| `BENH 97%` | Từ 90% trở lên: có dấu hiệu bệnh. Tên bệnh hiện ở dòng 2 của LCD; mã bệnh (bảng dưới) vẽ trên ảnh `_ai.jpg` |
| `KHONG THAY LA` | Model tìm lá không thấy chiếc lá nào đủ rõ: ảnh không có lá cà chua, hoặc lá quá nhỏ/quá xa |
| `ANH QUA TOI` | Ảnh chụp lúc trời tối |
| `LOI ANH` | Service không đọc được ảnh (thư mục ảnh mẫu trống, file hỏng) |

Chữ trên hiện ở dòng 1 của LCD trong 20 giây. Dòng 2 là tên bệnh gợi ý, ví dụ `Ca chua - moc suong (benh muon)` hoặc `Nghi: Ca chua - nhen do`, hay `La ca chua khoe`. Dòng dài hơn 16 ký tự sẽ chạy chữ.

**Chỉ dùng cho cà chua** (từ 18/10/2026): model lá chỉ còn 10 lớp lá cà chua (bỏ ớt, khoai tây).

| Mã | Bệnh lá cà chua | Mã | Bệnh lá cà chua |
|---|---|---|---|
| TBS | Đốm vi khuẩn | TMV | Virus khảm lá |
| TEB | Đốm vòng (bệnh sớm) | TSL | Đốm lá Septoria |
| TLB | Mốc sương (bệnh muộn) | TSM | Nhện đỏ |
| TLM | Nấm mốc lá | TTS | Đốm mục tiêu |
|  |  | TYC | Virus xoăn vàng lá |

**Tìm lá trước, rồi bỏ phiếu.** Model bệnh chỉ học ảnh từng chiếc lá. Nếu đưa cả khung hình vào, quả cà chua, đất nâu hay thân cây cũng bị đoán thành một bệnh nào đó. Vì vậy:

1. Model YOLO26n khoanh từng chiếc lá và chia 2 loại: lá cà chua / lá cây khác. Chỉ lá cà chua được giữ; lá cây khác, quả, đất, trời, chậu bị bỏ. Lá quá nhỏ (cạnh dưới 32 px) cũng bị bỏ vì quá mờ. Chỉ giữ tối đa 12 lá chắc chắn nhất.
2. Mỗi lá được cắt ra (hình vuông, rộng hơn khung 10%) và chấm điểm riêng.
3. Bỏ phiếu: cần **ít nhất 2 lá** cùng vượt ngưỡng 90% mới báo `BENH`. Nếu chỉ 1 lá bệnh rõ thì báo `NGHI`. Ảnh chỉ có 1 lá thì lá đó tự quyết. Tên bệnh là bệnh được các lá bị nghi chọn nhiều nhất.

Ảnh `*_ai.jpg` vẽ khung đỏ cho lá bệnh, cam cho lá nghi ngờ, xanh cho lá khỏe.

## 3. Pico 2 W mô phỏng trên Wokwi

Firmware Pico 2 W nằm ở `../src/main.cpp` và `../lib/FarmCore/`. Cách chạy mô phỏng, linh kiện, LCD, logic tưới và phun sương, các lệnh MQTT: xem [`../docs/wokwi.md`](../docs/wokwi.md).

Chế độ phun sương theo kết quả AI do hàm `mist_mode()` trong `service.py` quyết định, rồi gửi cho Pico 2 W bằng lệnh `MIST:<1-3>`:

| Kết quả AI | Lệnh | Phun sương |
|---|---|---|
| Lá khỏe | `MIST:1` | Bình thường: 30 giây, 30 phút một lần, 8h–16h |
| Nghi bệnh, hoặc bệnh nấm / vi khuẩn | `MIST:2` | Dừng: lá ướt làm bệnh lan nhanh |
| Nhện đỏ (≥ 90%) | `MIST:3` | Phun tăng: 15 phút một lần (nhện đỏ sợ ẩm) |
| Virus khảm lá, xoăn vàng lá | `MIST:1` | Bình thường |
| Ảnh tối, không thấy lá, lỗi ảnh | (không gửi) | Giữ chế độ đang chạy |

## 4. Chạy dịch vụ

Bật mô phỏng Pico 2 W trên Wokwi trước (xem `../docs/wokwi.md`), rồi chạy:

```bash
python service.py --once                       # phân tích 1 ảnh ngẫu nhiên, gửi cho Pico 2 W rồi thoát
python service.py --interval 2                 # chạy liên tục, 2 phút 1 lần (để demo)
python service.py                              # chạy liên tục: phân tích ngay, sau đó theo giờ trong config
python service.py --once --image anh.jpg       # dùng ảnh chỉ định
python service.py --image-dir thu_muc_anh      # lấy ảnh ngẫu nhiên từ thư mục khác (tìm cả thư mục con)
python service.py --no-mqtt                    # chỉ phân tích, không gửi cho Pico 2 W
```

- Service kết nối tới MQTT broker `config.MQTT_HOST`. Mỗi phút nó in một dòng số liệu Pico 2 W gửi lên, dạng `[Pico] 14:05:12  Dat 57% ...`, và in câu trả lời của Pico 2 W, ví dụ `[Pico] AI OK`.
- Chế độ phun sương được gửi lại mỗi 60 giây. Nhờ vậy, nếu mô phỏng Pico 2 W khởi động lại thì vẫn lấy lại được chế độ.
- Lưu trữ:
  - `captures/<ngày>/<giờ>.jpg` là ảnh đã phân tích, `<giờ>_ai.jpg` là ảnh có vẽ khung.
  - `logs/ai_results.csv` ghi kết quả từng lần.
  - `logs/sensors.csv` ghi số liệu cảm biến (độ ẩm đất, nhiệt độ, độ ẩm không khí, ánh sáng, mực nước, bơm, phun sương), mỗi phút một dòng.

## 5. Tùy chỉnh (`config.py`)

| Biến | Mặc định | Ý nghĩa |
|---|---|---|
| `CAPTURE_TIMES` | `["08:00", "16:00"]` | Giờ phân tích mỗi ngày (khi không dùng `--interval`) |
| `SAMPLE_IMAGES_DIR` | `data/test_real` | Thư mục ảnh mẫu, mỗi lần lấy ngẫu nhiên 1 ảnh |
| `DISEASE_THRESHOLD` / `SUSPECT_THRESHOLD` | 0.9 / 0.6 | Ngưỡng báo "Benh" và "Nghi". Tăng lên nếu báo nhầm nhiều, giảm xuống nếu hay bỏ sót |
| `MIN_VOTES` | 2 | Số lá phải cùng vượt ngưỡng mới báo "Benh". Tăng lên nếu báo nhầm nhiều |
| `DET_CONF` | 0.25 | Điểm tin cậy tối thiểu để một khung được coi là lá cà chua. Tăng lên nếu khoanh nhầm vật khác, giảm nếu hay báo `KHONG THAY LA` |
| `LEAF_MIN_SIDE` / `MAX_LEAVES` | 32 / 12 | Bỏ lá nhỏ hơn 32 px; chỉ chấm tối đa 12 lá mỗi ảnh |
| `MQTT_HOST` / `MQTT_PORT` | `broker.hivemq.com` / 1883 | MQTT broker. Đổi thì sửa cả `../include/config.h` rồi build lại firmware |
| `MQTT_TOPIC_PREFIX` | `tuoicaytt/55afb0` | Tiền tố topic, phải giống `MQTT_TOPIC_PREFIX` trong `../include/config.h` |
| `SENSOR_LOG_SECONDS` | 60 | Ghi số liệu cảm biến vào `logs/sensors.csv` tối đa 1 lần mỗi chừng này giây |

## 6. Xử lý sự cố

| Hiện tượng | Cách xử lý |
|---|---|
| `Chua ket noi duoc MQTT` | Kiểm tra Internet. Broker công khai đôi khi chậm hoặc lỗi: đổi `MQTT_HOST` (ví dụ `test.mosquitto.org`) ở cả `config.py` và `../include/config.h`, rồi build lại firmware |
| LCD không hiện kết quả AI | Xem service có in `[Pico] AI OK` không. Nếu không: terminal Wokwi phải có dòng `Da ket noi MQTT`, và tiền tố topic 2 bên phải giống nhau |
| `Khong co anh nao trong ...test_real` | Chạy `prepare_data.py` để tạo thư mục ảnh mẫu, hoặc dùng `--image` / `--image-dir` |
| Báo bệnh nhầm nhiều | Tăng `MIN_VOTES` hoặc `DISEASE_THRESHOLD`, và xem ảnh `*_ai.jpg` để biết lá nào bị chấm nhầm |
| Báo `KHONG THAY LA` dù có lá | Lá phải to hơn 32 px trên ảnh. Dùng ảnh chụp gần hơn, hoặc giảm `DET_CONF` |
| VS Code báo `Import "torch" could not be resolved` | Bấm `Ctrl+Shift+P` → *Python: Select Interpreter* → chọn Python trong `ai/.venv` |

## 7. Cải thiện độ chính xác

Model chỉ được học từ ảnh trên mạng. Muốn chính xác hơn với một vườn cụ thể thì cần thêm ảnh của chính vườn đó:

1. Chụp ảnh lá cà chua của vườn (ví dụ bằng điện thoại).
2. Cắt riêng phần lá trong ảnh rồi phân loại theo lớp (khỏe, hoặc từng loại bệnh).
3. Chép vào `data/raw/own/<tên lớp>/`, với tên lớp đúng như trong `config.CLASSES`, ví dụ `data/raw/own/Tomato_healthy/`.
   - `prepare_data.py` tự lấy 70% số ảnh này vào tập train và 30% vào tập `test_real`.
   - Tên file mới có tiền tố `own_`.
4. Chạy lại `prepare_data.py` → `train.py` → `evaluate.py`.

Kết quả AI chỉ để **cảnh báo sớm**, không thay được việc ra xem cây tận mắt.
