# Bản nháp thiết kế giao diện app và web

Ngày rà soát: 11/11/2026. Cơ sở: firmware trong repo PlatformIO và mã nguồn app DetectDisease hiện có.

Tài liệu này chuẩn bị nội dung cho Figma. Phạm vi các màn hình app cần sửa còn chờ người dùng chọn; đây chưa phải bản thiết kế đã chốt hoặc chức năng đã triển khai.

## 1. Mục tiêu và trình tự

- Hệ thống nông nghiệp thông minh chạy mô phỏng bằng Pico 2 W trên Wokwi, AI chạy trên máy tính hoặc điện thoại.
- App và web dùng chung thuật ngữ, màu trạng thái và cách trình bày số liệu.
- Rà soát các màn hình app cần sửa → thiết kế các luồng và màn hình trên Figma → triển khai giao diện theo thiết kế.
- Web theo hướng đã chọn: Next.js + FastAPI + SQLite. Desktop vẫn là một thành phần cần hoàn thiện riêng trong yêu cầu Firmware + Web + Mobile + Desktop.
- Chip mô phỏng đất và việc mở rộng model sang nhiều cây trồng là các công việc riêng đang để sau.

## 2. Hiện trạng đã đối chiếu với mã nguồn

| Thành phần | Hiện có | Điều cần xử lý khi thiết kế |
| --- | --- | --- |
| Firmware | Pico 2 W, cảm biến, tưới tự động, phun theo giờ, LCD, MQTT | Dùng đúng các số đo và lệnh đang hỗ trợ |
| App Android | Trang chủ, camera điện thoại, chọn ảnh, kết quả AI, lịch sử, cài đặt | Trang chủ và cài đặt vẫn phụ thuộc ESP32-CAM qua HTTP |
| AI Python | Phân tích ảnh mẫu, gửi kết quả và chế độ phun qua MQTT | Kết quả trên MQTT hiện là hai dòng LCD, chưa phải bản ghi đầy đủ kèm ảnh |
| Web | Chưa có mã nguồn web trong repo | Thiết kế trước trang tổng quan và lịch sử |

Nguồn đối chiếu:

- `src/main.cpp`: `handleCommand()` và `publishTelemetry()`.
- `include/config.h`: Pico 2 W và cấu hình MQTT.
- `ai/config.py`, `ai/service.py`: lớp nhận diện và giao tiếp AI.
- Repo bên cạnh `DetectDisease`: `HomeFragment.kt`, `EspClient.kt`, `DetectionRecord.kt`, `FreshnessClassifier.kt`, các layout trong `main/res/layout/`.

## 3. Các màn hình app đề xuất rà soát

| Màn hình | Nội dung thiết kế | Thay đổi chức năng đi kèm |
| --- | --- | --- |
| Trang chủ / Tổng quan | Trạng thái thiết bị, lần cập nhật, 5 số đo, trạng thái bơm và phun, cảnh báo, kết quả AI gần nhất | Thay đọc `/status` từ ESP32-CAM bằng nhận dữ liệu của Pico qua MQTT |
| Chụp và chọn ảnh | Camera điện thoại hoặc thư viện ảnh; tiến trình phân tích; cách chọn lại ảnh | Thay luồng “Chụp từ camera vườn”; Pico mô phỏng hiện không cung cấp ảnh camera |
| Chi tiết kết quả | Ảnh, tình trạng lá, chất lượng quả, độ tin cậy, nguồn ảnh, thời gian | Phân biệt kết quả phân tích tại điện thoại với nội dung đã gửi tới thiết bị |
| Lịch sử | Danh sách ảnh đã phân tích, thời gian, nguồn, trạng thái và màn chi tiết | Lịch sử app hiện lưu cục bộ; đồng bộ với web là chức năng cần triển khai riêng |
| Cài đặt | Cấu hình kết nối hệ thống và trạng thái kiểm tra | Thay cấu hình IP/Wi-Fi ESP32-CAM; cấu hình kỹ thuật nằm ở cài đặt, không đưa lên màn chính |

Đề xuất giữ camera điện thoại, chọn ảnh và lịch sử đang có. Luồng tự động chụp từ ESP32-CAM cần được thay thế có chủ đích; việc đổi tên nút không đủ để luồng này chạy với mô phỏng.

## 4. Màn hình web cần thiết kế trên Figma

### Tổng quan

Thứ tự nội dung: tên hệ thống và trạng thái kết nối → cảnh báo cần xử lý → số đo → bơm/phun → kết quả AI → biểu đồ gần đây.

- Số đo: độ ẩm đất, nhiệt độ, độ ẩm không khí, ánh sáng, mực nước bồn.
- Bơm: trạng thái tự động, số nhịp bơm, nút mở khóa khi phù hợp.
- Phun: chế độ bình thường / dừng / tăng cường; trạng thái hoạt động và lý do đang dừng.
- Kết quả AI: nội dung mới nhất có thể lấy từ giao thức hiện tại. Ảnh và phân tích chi tiết chỉ hiển thị khi backend đã nhận được những dữ liệu đó.
- Điều khiển: phản hồi đang gửi, đã được thiết bị xác nhận, hết thời gian chờ hoặc lỗi.
- Chỉnh giờ thử nghiệm đặt ở phần tùy chọn phục vụ mô phỏng.

### Lịch sử

- Khoảng thời gian 1 giờ / 6 giờ / 24 giờ / 7 ngày.
- Biểu đồ cảm biến và các khoảng bơm/phun hoạt động.
- Danh sách kết quả AI đã nhận và nhật ký lệnh/phản hồi.
- Trạng thái chưa có dữ liệu, đang tải, lỗi và mất kết nối.

## 5. Dữ liệu và thao tác phải khớp firmware

| Nội dung giao diện | Trường hoặc lệnh hiện có | Quy tắc hiển thị |
| --- | --- | --- |
| Độ ẩm đất | `moisture`, `soil_fault` | Có lỗi cảm biến thì hiển thị lỗi, không coi số đo là hợp lệ |
| Nhiệt độ / độ ẩm không khí | `temperature`, `air_humidity` | `null` hiển thị “Chưa có dữ liệu” |
| Ánh sáng | `lux` | Đơn vị lux |
| Bồn nước | `tank_level`, `tank_empty` | Phân biệt thiếu số đo với cảnh báo hết nước |
| Máy bơm | `pump`, `pump_state`, `pulses` | Có các trạng thái bơm, chờ ngấm, nghỉ, khóa, hết nước, lỗi cảm biến |
| Phun sương | `mist`, `mist_mode`, `mist_reason` | Chế độ và trạng thái bật/tắt là hai thông tin khác nhau |
| Thời gian | `time`, `uptime_s` | Giờ thiết bị có thể chưa đồng bộ; thời điểm nhận do backend/app ghi riêng |
| Chọn chế độ phun | `MIST:1`, `MIST:2`, `MIST:3` | Chờ phản hồi `MIST OK ...`; chế độ 3 vẫn phụ thuộc lịch và điều kiện cảm biến |
| Mở khóa bơm | `UNLOCK` | Chờ `UNLOCK OK`; mở khóa không có nghĩa bơm sẽ chạy ngay |
| Chỉnh giờ thử nghiệm | `TIME:hh:mm:ss` | Chờ `TIME OK` hoặc báo lỗi |
| Gửi nội dung AI | `AI:<dòng 1>\|<dòng 2>` | `AI OK` xác nhận nhận nội dung; không xác nhận độ đúng của AI |

MQTT dùng tiền tố `tuoicaytt/55afb0`, với các topic `telemetry`, `cmd`, `ack`, `status`. Telemetry được gửi mỗi 5 giây. Trạng thái kết nối broker và trạng thái thiết bị cần theo dõi riêng; dữ liệu cũ cần có nhãn thời gian cập nhật.

Firmware chưa có lệnh bật/tắt bơm thủ công hoặc sửa ngưỡng tưới từ xa. Không thiết kế những thao tác đó như chức năng đã hoạt động. Các phản hồi hiện chưa có mã định danh yêu cầu; khi triển khai cần xử lý trường hợp nhiều ứng dụng cùng gửi lệnh.

## 6. Hướng trình bày ban đầu

Kế thừa màu và phong cách Material của app hiện tại để hai giao diện thống nhất:

| Vai trò | Giá trị khởi đầu |
| --- | --- |
| Màu chính | Xanh `#2E7D32` |
| Nền | Trắng `#FFFFFF`, nền phụ `#F7F9F7` |
| Chữ | Chính `#1A1C1A`, phụ `#5F6B60` |
| Cảnh báo / lỗi | `#D35400` / `#C62828`, luôn kèm chữ và biểu tượng |
| Viền | `#E3E8E3` |
| Bo góc | Thẻ 20, thành phần nhỏ 14, kế thừa app |

Giao diện tiếng Việt có dấu. Ưu tiên số đo dễ đọc và thao tác rõ ràng. Bản thiết kế cần có bố cục điện thoại và desktop; chế độ tối là biến thể cần thiết kế riêng trước khi triển khai.

Các component Figma: thẻ cảm biến, nhãn trạng thái, cảnh báo, thẻ bơm/phun, nút và bộ chọn chế độ, thẻ kết quả AI, hàng lịch sử, trạng thái trống/lỗi/đang tải. Dùng Auto Layout và biến thể trạng thái để bàn giao sang code.

## 7. Luồng prototype cần kiểm tra

1. Mở tổng quan → nhận số liệu → thấy cảnh báo hết nước → xem lý do bơm/phun dừng.
2. Chọn dừng phun → đang gửi → thiết bị xác nhận; nhánh khác là không nhận được phản hồi.
3. Thấy bơm bị khóa → mở khóa → nhận phản hồi và cập nhật trạng thái thực tế.
4. Chụp/chọn ảnh trên app → phân tích → kết quả → mở lại trong lịch sử.
5. Ảnh quá tối, không thấy lá hoặc phân tích lỗi → giải thích và chọn ảnh khác.
6. Mất kết nối → hiển thị thời điểm dữ liệu cuối → kết nối lại và cập nhật.

## 8. Điều kiện bàn giao sang code

- Xác định các màn hình app thực sự cần sửa và file Figma dùng làm nguồn thiết kế.
- Có màn hình chính cùng các trạng thái trống, tải, lỗi và mất kết nối.
- Mỗi nút có hành vi và nguồn dữ liệu tương ứng; dữ liệu mẫu được nhận diện rõ là mẫu.
- Thiết kế dùng thuật ngữ cây trồng tổng quát nhưng không tuyên bố đã nhận diện được mọi loại cây: AI Python hiện có 10 lớp cà chua, phần quả Android vẫn lọc cà chua.
- Web triển khai sau thiết kế bằng Next.js + FastAPI + SQLite, đối chiếu với giao thức thực tế nêu trên. Backend cần bổ sung giao thức dữ liệu AI nếu muốn đồng bộ ảnh/kết quả chi tiết giữa các nền tảng.
- Figma chưa được kết nối tại thời điểm viết tài liệu; chưa tạo hoặc sửa file Figma nào.
