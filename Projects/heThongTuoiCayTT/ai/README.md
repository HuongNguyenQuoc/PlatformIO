# AI phát hiện bệnh cây – Hệ thống tưới cây TT

ESP32-CAM chụp ảnh cây mỗi ngày. Laptop dùng CNN (EfficientNet-B0, PyTorch) để phân tích, rồi gửi kết quả xuống Arduino Nano. Nano hiện kết quả lên LCD 20 giây rồi quay về màn hình chính (độ ẩm, giờ, bơm, phun sương), và dừng phun sương khi lá bị bệnh nấm hoặc vi khuẩn. App Android trong repo DetectDisease làm được việc tương tự laptop, gửi kết quả qua ESP32-CAM.

```text
ESP32-CAM (ngoài vườn) ──WiFi: GET /capture──▶ Laptop: ai/service.py
                                                 ├─ predict.py: chia ô, CNN chấm từng vùng lá
                                                 ├─ captures/<ngày>/  ảnh gốc + ảnh có khung
                                                 ├─ logs/ai_results.csv, logs/moisture.csv
                                                 └─ USB Serial "AI:BENH 97%\n" ──▶ Nano ──▶ LCD (20 giây)
```

## Các file

| File | Việc làm |
|---|---|
| `config.py` | Mọi cấu hình: đường dẫn, 15 lớp, ngưỡng bệnh, giờ chụp, cổng Serial, địa chỉ camera |
| `prepare_data.py` | Gom PlantVillage và PlantDoc thành `data/train`, `val`, `test_pv`, `test_real` |
| `model.py` | Tạo EfficientNet-B0, augmentation, đọc model đã train |
| `train.py` | Train 2 pha (đóng băng backbone, rồi fine-tune toàn bộ) và lưu `models/best.pt` |
| `evaluate.py` | Chấm điểm trên tập test: accuracy, F1, ma trận nhầm lẫn, chỉ số khỏe/bệnh |
| `predict.py` | Phân tích ảnh hoặc webcam, chia ô, vẽ khung vùng bệnh |
| `service.py` | Chạy liên tục: chụp theo giờ, phân tích, ghi log, gửi xuống Arduino |
| `../src/main.cpp` | Nano: tưới, phun sương theo giờ và theo AI, nhận kết quả AI qua Serial và hiện lên LCD |
| `../esp32cam/` | Firmware ESP32-CAM: WiFi, mDNS `esp32cam.local`, các đường dẫn `/capture` và `/status` |

Mọi lệnh Python bên dưới đều chạy trong thư mục `ai/`, sau khi đã bật venv:

```bash
cd ~/Documents/PlatformIO/Projects/heThongTuoiCayTT/ai
source .venv/bin/activate
```

## 1. Huấn luyện model (làm 1 lần)

```bash
python prepare_data.py   # ~35 giây
python train.py          # ~22 phút trên RTX 2050, lưu models/best.pt
python evaluate.py       # chấm điểm trên test_pv và test_real
```

Kết quả lần train ngày 07/10/2026:

| Tập | Số ảnh | Đoán đúng tên bệnh (15 lớp) | Báo "Benh" đúng (ngưỡng 90%) |
|---|---|---|---|
| `val` (PlantVillage) | 2279 | 99,8% | – |
| `test_pv` (PlantVillage) | 2279 | 99,7% | 99,8% (phát hiện 1953/1957 ảnh bệnh, báo nhầm 0/322 ảnh khỏe) |
| `test_real` (PlantDoc, ảnh thật) | 102 | **58,8%** | **92,2%** (phát hiện 78/86 ảnh bệnh, báo nhầm 0/16 ảnh khỏe) |

- Ngoài vườn, model **phân biệt khỏe hay bệnh khá tốt**, nhưng **hay nhầm giữa các bệnh trông giống nhau**.
  - Ví dụ: đốm vi khuẩn cà chua bị nhầm thành Septoria, bệnh sớm khoai tây bị nhầm thành bệnh muộn.
  - Vì vậy, LCD chỉ báo mức Khỏe / Nghi / Bệnh. Tên bệnh (in ra màn hình và trên app) chỉ là **gợi ý**. Xem chi tiết trong `logs/evaluate.log`.
- **Vì sao ngưỡng "Benh" đặt cao (90%):**
  - Ở ngưỡng 60%, model phát hiện được 84/86 ảnh bệnh, nhưng báo nhầm 5/16 ảnh cà chua khỏe ngoài thực tế. Những ảnh khỏe bị nhầm này có xác suất bệnh 64–87%.
  - Với ngưỡng 90%, các ảnh đó rơi vào mức "Nghi" thay vì "Benh".
  - Ngưỡng được chọn dựa trên chỉ 16 ảnh khỏe, nên khi có ảnh vườn của bạn, hãy chỉnh lại.

> **Điểm `test_real` mới là điểm thật.** PlantVillage là ảnh một chiếc lá đặt trên nền xám, chụp trong phòng lab, nên model đạt gần 100% trên đó. Ảnh ngoài vườn khó hơn nhiều. Cách cải thiện tốt nhất là tự gom ảnh vườn của bạn (xem mục 8).

## 2. Thử model

```bash
python predict.py data/test_real/Tomato_Late_blight/pd_02010.jpg
python predict.py data/test_real/Tomato_healthy/*.jpg           # nhiều ảnh, mỗi ảnh 1 dòng
python predict.py anh_cay.jpg --save anh_cay_ai.jpg              # lưu ảnh có vẽ khung vùng bệnh
python predict.py --webcam                                       # chụp bằng webcam laptop (đưa 1 chiếc lá lại gần)
```

**Cách đọc kết quả** (xác suất bệnh là mức cao nhất trong các vùng lá):

| LCD hiện | Nghĩa |
|---|---|
| `KHOE 85%` | Xác suất bệnh dưới 60%. Số hiện là mức tin cây khỏe |
| `NGHI 75%` | 60–90%: nghi ngờ, nên ra xem cây |
| `BENH 97%` | Từ 90% trở lên: có dấu hiệu bệnh. Tên bệnh xem trên màn hình laptop hoặc app; mã bệnh (bảng dưới) vẽ trên ảnh `_ai.jpg` |
| `KHONG THAY LA` | Ảnh có quá ít màu lá, có thể camera bị lệch hướng |
| `ANH QUA TOI` | Chụp lúc trời tối |
| `LOI CAMERA` | Laptop không lấy được ảnh từ ESP32-CAM |

Chữ trên hiện ở dòng 1 của LCD trong 20 giây, dòng 2 là `Khong ro qua` vì laptop chỉ có model lá. App Android gửi đủ 2 dòng, ghi rõ tên cây, ví dụ `La ca chua: Moc suong 97%` / `Qua ca chua: Tuoi 92%`. Dòng dài hơn 16 ký tự sẽ chạy chữ.

| Mã | Bệnh | Mã | Bệnh |
|---|---|---|---|
| PBS | Ớt – đốm vi khuẩn | TLM | Cà chua – nấm mốc lá |
| PEB | Khoai tây – đốm vòng (bệnh sớm) | TMV | Cà chua – virus khảm lá |
| PLB | Khoai tây – mốc sương (bệnh muộn) | TSL | Cà chua – đốm lá Septoria |
| TBS | Cà chua – đốm vi khuẩn | TSM | Cà chua – nhện đỏ |
| TEB | Cà chua – đốm vòng (bệnh sớm) | TTS | Cà chua – đốm mục tiêu |
| TLB | Cà chua – mốc sương (bệnh muộn) | TYC | Cà chua – virus xoăn vàng lá |

**Ảnh lớn được chia ô.** Ảnh ESP32-CAM (1600×1200) được chia lưới 3×3, các ô chồng lên nhau một phần. Chỉ những ô có ít nhất 50% là lá mới được chấm điểm. Lý do: đất nâu rất dễ bị model nhầm thành vết bệnh hoại tử. Ảnh `*_ai.jpg` vẽ khung đỏ cho ô bệnh, cam cho ô nghi ngờ, xanh cho ô khỏe. Ô không có khung là ô ít lá nên bị bỏ qua.

## 3. Arduino Nano

Nạp firmware mới (chạy ở thư mục gốc `heThongTuoiCayTT`):

```bash
pio run -t upload
```

Nano vẫn tưới như cũ. Phần mới:

**Màn hình chính** (lúc bật nguồn, và sau khi hết 20 giây hiện kết quả AI):

```text
Do am: 45% 14:05      độ ẩm đất + giờ (--:-- khi chưa biết giờ); cảm biến lỗi thì cả dòng là LOI CAM BIEN DAT
Bom:CHO Phun:OFF      bơm: ON / OFF / CHO (chờ nước ngấm) / LOI (bơm bị khóa, bấm RESET); phun sương: ON / OFF
```

**Kết quả AI** hiện 20 giây mỗi lần có ảnh mới phân tích xong: dòng 1 là lá, dòng 2 là quả. Dòng dài hơn 16 ký tự thì chạy chữ.

**Phun sương** (relay ở chân D6), chỉ phun khi đã biết giờ:

- Lá khỏe: 30 giây, 30 phút 1 lần (đúng :00 và :30), chỉ từ 8h đến trước 16h.
- Lá bệnh nấm / vi khuẩn, hoặc nghi bệnh: dừng hẳn, đến khi AI báo lá khỏe lại (lá ướt làm bệnh lan nhanh).
- Nhện đỏ (≥ 90%): 30 giây, 15 phút 1 lần (nhện đỏ sợ ẩm). Virus khảm / xoăn lá: phun bình thường.
- Đổi thời gian ở các hằng `MIST_*` trong `../src/main.cpp`.

Nano nhận lệnh từ 2 nguồn: cáp USB (laptop) và chân D8 (ESP32-CAM):

| Lệnh | Việc làm |
|---|---|
| `AI:<dòng lá>\|<dòng quả>` | Kết quả mới, hiện 20 giây. Nano trả lời `AI OK`. Không có `\|` thì dòng 2 là `Khong ro qua`. `AI:` để trống thì thôi hiện |
| `MIST:<1-3>` | Chế độ phun sương: 1 bình thường, 2 dừng, 3 phun tăng |
| `TIME:hh:mm:ss` | Giờ thật. Nano tự đếm tiếp giữa 2 lần nhận |

Mỗi 5 giây Nano gửi `SOIL:<độ ẩm %>,<raw>,<bơm 0/1>,<lỗi cảm biến 0/1>,<phun sương 0/1>` sang ESP32-CAM qua chân D9, để app xem qua `/status`.

**Test không cần Python:** mở Serial Monitor (9600 baud, chọn kết thúc dòng là *Newline*), gõ lần lượt:

1. `TIME:08:29:50`: dòng 1 hiện `08:29`. Khoảng 10 giây sau là 08:30, dòng 2 thành `Phun:ON` trong 30 giây.
2. `AI:La ca chua: Moc suong 97%|Qua ca chua: Tuoi 92%`: LCD hiện kết quả 20 giây, dòng 1 chạy chữ.
3. `MIST:2`: dừng phun. `MIST:1`: phun lại bình thường.

## 4. ESP32-CAM

1. Mở `../esp32cam/include/secrets.h` và điền tên, mật khẩu WiFi nhà, cùng `AP_PASSWORD` (8–63 ký tự) cho WiFi riêng `TuoiCay-CAM`. ESP32 **chỉ bắt được WiFi 2.4 GHz**. Copy `../esp32cam/secrets.example.ini` thành `secrets.ini` rồi đặt mật khẩu nạp code qua WiFi. Cả 2 file đã nằm trong `.gitignore`.
2. Cắm ESP32-CAM lên đế **ESP32-CAM-MB**, cắm USB vào laptop rồi nạp code qua cáp:
   ```bash
   cd ~/Documents/PlatformIO/Projects/heThongTuoiCayTT
   pio run -d esp32cam -e esp32cam -t upload
   pio device monitor -d esp32cam -e esp32cam     # xem log, ghi lại địa chỉ IP
   ```
   Nếu báo lỗi `Failed to connect`, giữ nút IO0 trên đế MB, bấm RST, rồi nạp lại.
   Từ lần sau có thể nạp qua WiFi bằng `pio run -d esp32cam -t upload` (xem `docs/wiring.md`, mục 6).
3. Trên trình duyệt, mở `http://esp32cam.local/`. Trang sẽ hiện ảnh chụp thử.
   - Nếu không mở được, dùng địa chỉ IP đã ghi lại ở bước 2, ví dụ `http://192.168.1.50/`.
   - Nên đặt IP cố định cho ESP32-CAM trong trang quản lý router (DHCP reservation).
4. Khi chạy thật, cấp **nguồn 5V ≥ 2A riêng** cho ESP32-CAM. Đặt camera **cách lá khoảng 30–60 cm**: lá phải chiếm phần lớn khung hình thì AI mới soi được vết bệnh.

Các đường dẫn ESP32-CAM cung cấp:

| Đường dẫn | Trả về |
|---|---|
| `/capture` | Ảnh JPEG 1600×1200 |
| `/capture?flash=1` | Ảnh JPEG, có bật đèn flash lúc chụp |
| `/status` | JSON gồm uptime, `time` (giờ ESP, `null` khi chưa biết giờ), `wifi_ok`/`ip`/`rssi`/`bssid` của WiFi nhà, `ap_ip`/`ap_clients` của WiFi `TuoiCay-CAM`, heap, `leaf`/`fruit`/`mist_mode` (kết quả AI gần nhất) và `soil` (độ ẩm, bơm, phun sương do Nano gửi lên, `null` nếu chưa nhận được) |
| `POST /result` | App gửi kết quả AI dạng text 4 dòng: dòng lá, dòng quả (mỗi dòng tối đa 40 ký tự), chế độ phun (0 giữ nguyên, 1–3), giờ Unix của điện thoại. ESP lưu vào bộ nhớ và gửi xuống Nano 1 lần. Gửi 1 dòng (vd `BENH 99%`) vẫn được |
| `/` | Trang xem thử |

**Giờ của ESP.** Vào được WiFi có Internet thì ESP lấy giờ qua mạng (NTP, múi giờ Việt Nam). Chỉ phát `TuoiCay-CAM` (không có Internet) thì ESP lấy giờ của điện thoại ở lần app gửi kết quả đầu tiên. Mỗi 60 giây ESP gửi giờ và chế độ phun xuống Nano.

**Hai mạng WiFi cùng lúc.** ESP vừa vào WiFi nhà, vừa tự phát WiFi `TuoiCay-CAM` (mật khẩu `AP_PASSWORD`, IP luôn là `192.168.4.1`), nên vẫn dùng được khi router hỏng hoặc mang đi nơi khác: điện thoại vào `TuoiCay-CAM`, bấm "Vẫn kết nối" khi Android báo không có Internet, app tự gọi `192.168.4.1`. Laptop cũng dùng được: `python service.py --camera-url http://192.168.4.1/capture`.

- Khi có máy đang vào `TuoiCay-CAM`, ESP tạm ngừng dò tìm router để sóng không bị ngắt. Mất WiFi nhà quá 2 phút thì 1 phút mới thử lại 1 lần; quá 30 phút và không ai vào `TuoiCay-CAM` thì ESP khởi động lại.
- Nếu router và bộ kích sóng phát cùng tên, ESP vào điểm mạnh nhất. Điểm nào vào được mà 10 giây không cấp IP (bộ kích sóng không chuyển dữ liệu của ESP) thì ESP bỏ qua 10 phút và vào điểm khác. Xem `bssid` trong `/status` để biết ESP đang dùng điểm nào.

ESP32-CAM nói chuyện với Nano qua UART 9600 baud, đi qua module chuyển mức BSS138: IO13 (TX) → Nano D8, Nano D9 → IO14 (RX), GND chung. Thử không cần app: `curl -X POST -H "Content-Type: text/plain" --data-binary $'La ca chua: Khoe 98%\nQua ca chua: Tuoi 92%\n0' http://<IP>/result` (phải có `Content-Type: text/plain`, nếu không ESP sẽ không nhận được nội dung; số `0` ở cuối giữ nguyên chế độ phun sương).

## 5. Chạy dịch vụ hằng ngày

Cắm Nano vào laptop qua USB. Bật ESP32-CAM, rồi chạy:

```bash
python service.py --once                       # chụp + phân tích 1 lần, gửi xuống Nano rồi thoát
python service.py                              # chạy liên tục: phân tích ngay, sau đó theo giờ trong config
python service.py --once --image anh.jpg       # dùng ảnh có sẵn, không cần ESP32-CAM
python service.py --no-serial                  # chưa cắm Nano
python service.py --camera-url http://192.168.1.50/capture   # khi esp32cam.local không hoạt động
python service.py --port /dev/ttyACM0          # Nano chính hãng thường là ttyACM0
```

- Service **giữ cổng Serial mở suốt**. Mỗi lần cổng được mở, Nano sẽ tự reset. Trong lúc service chạy, **không mở được Serial Monitor** của PlatformIO; service tự in lại các dòng Nano gửi lên, dạng `[Arduino] ...`.
- Kết quả gửi xuống Nano được gửi lại mỗi 60 giây. Nhờ vậy, nếu Nano bị reset thì LCD vẫn có kết quả.
- Lưu trữ:
  - `captures/<ngày>/<giờ>.jpg` là ảnh gốc, `<giờ>_ai.jpg` là ảnh có vẽ khung.
  - `logs/ai_results.csv` ghi kết quả từng lần.
  - `logs/moisture.csv` ghi độ ẩm, mỗi phút một dòng.

## 6. Tùy chỉnh (`config.py`)

| Biến | Mặc định | Ý nghĩa |
|---|---|---|
| `CAPTURE_TIMES` | `["08:00", "16:00"]` | Giờ chụp mỗi ngày. Nên chụp lúc trời sáng, tránh nắng gắt buổi trưa |
| `DISEASE_THRESHOLD` / `SUSPECT_THRESHOLD` | 0.9 / 0.6 | Ngưỡng báo "Benh" và "Nghi". Tăng lên nếu báo nhầm nhiều, giảm xuống nếu hay bỏ sót |
| `TILE_LEAF_MIN_RATIO` | 0.5 | Tỉ lệ lá tối thiểu để một ô được chấm điểm |
| `CAMERA_URLS` | `esp32cam.local` | Có thể thêm IP dự phòng, ví dụ `"http://192.168.1.50/capture"` |
| `CAMERA_FLASH` | False | Bật đèn flash khi chụp (trong nhà, thiếu sáng) |
| `SERIAL_PORT` | `/dev/ttyUSB0` | Cổng Serial của Nano. Xem bằng lệnh `ls /dev/ttyUSB* /dev/ttyACM*` |

## 7. Xử lý sự cố

| Hiện tượng | Cách xử lý |
|---|---|
| `Chua mo duoc cong /dev/ttyUSB0` | Kiểm tra cáp USB và tên cổng (`ls /dev/ttyUSB* /dev/ttyACM*`). Tắt Serial Monitor đang mở |
| LCD không hiện kết quả AI | Xem service có in `[Arduino] AI OK` không. Nếu không, Nano đang chạy firmware cũ: nạp lại bằng `pio run -t upload` |
| `LOI CAMERA` | Mở `http://esp32cam.local/capture` trên trình duyệt. Nếu không được, dùng IP với `--camera-url` |
| ESP32-CAM khởi động lại liên tục | Thường do nguồn yếu (brownout). Dùng adapter 5V ≥ 2A và dây ngắn |
| Ảnh bị lật ngược | Xoay camera, hoặc thêm `s->set_vflip(s, 1)` vào `initCamera()` |
| Báo bệnh nhầm nhiều | Đặt camera gần lá hơn, tăng `DISEASE_THRESHOLD`, và xem ảnh `*_ai.jpg` để biết ô nào bị chấm nhầm |
| VS Code báo `Import "torch" could not be resolved` | Bấm `Ctrl+Shift+P` → *Python: Select Interpreter* → chọn `ai/.venv/bin/python` |

## 8. Cải thiện độ chính xác cho vườn của bạn

Model chỉ được học từ ảnh trên mạng. Muốn chính xác với vườn của bạn thì cần thêm ảnh của chính vườn đó:

1. Để service chạy vài tuần, ảnh sẽ được gom trong `captures/`.
2. Cắt riêng phần lá trong ảnh rồi phân loại theo lớp (khỏe, hoặc từng loại bệnh).
3. Chép vào `data/raw/own/<tên lớp>/`, với tên lớp đúng như trong `config.CLASSES`, ví dụ `data/raw/own/Tomato_healthy/`.
   - `prepare_data.py` tự lấy 70% số ảnh này vào tập train và 30% vào tập `test_real`.
   - Tên file mới có tiền tố `own_`.
4. Chạy lại `prepare_data.py` → `train.py` → `evaluate.py`.

Kết quả AI chỉ để **cảnh báo sớm**, không thay được việc ra xem cây tận mắt.
