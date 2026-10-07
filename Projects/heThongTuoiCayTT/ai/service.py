"""Dich vu chay lien tuc tren laptop: chup anh tu ESP32-CAM theo gio, AI phan tich, gui ket qua xuong Arduino.

Cach chay:
    python service.py                          Chay lien tuc: phan tich ngay khi bat, sau do theo config.CAPTURE_TIMES
    python service.py --once                   Chup + phan tich 1 lan, gui xuong Arduino roi thoat
    python service.py --once --image anh.jpg   Dung anh co san thay vi chup tu ESP32-CAM
    python service.py --camera-url http://192.168.1.50/capture   Chi dinh IP ESP32-CAM (khi esp32cam.local loi)
    python service.py --no-serial              Khong gui xuong Arduino (khi chua cam Nano)
    python service.py --port /dev/ttyACM0      Doi cong Serial (mac dinh config.SERIAL_PORT)

Ket qua luu tai:
    captures/<ngay>/<gio>.jpg      anh goc        captures/<ngay>/<gio>_ai.jpg   anh co ve khung vung benh
    logs/ai_results.csv            ket qua AI     logs/moisture.csv              do am Arduino gui len

Luu y: luc service dang mo cong Serial thi khong mo duoc Serial Monitor cua PlatformIO
(service tu in ra cac dong Arduino gui len, dang "[Arduino] ...").
"""
import argparse
import csv
import io
import re
import threading
import time
from datetime import datetime
from pathlib import Path

import requests
import serial
from PIL import Image, UnidentifiedImageError

import config
from predict import Predictor, annotate, describe, lcd_text

AI_LOG = config.LOGS_DIR / "ai_results.csv"
AI_LOG_HEADER = ["thoi_gian", "anh", "trang_thai", "xac_suat_benh", "lop_benh", "ten_benh", "lcd"]
MOISTURE_LOG = config.LOGS_DIR / "moisture.csv"
MOISTURE_LOG_HEADER = ["thoi_gian", "raw", "do_am", "bom", "loi_cam_bien"]

# Dong Arduino in ra moi 0.5 s, vd "Raw: 812  -->  Do am: 45%  Bom: OFF" (+ "  [LOI CAM BIEN]")
MOISTURE_RE = re.compile(r"Raw:\s*(\d+)\s*-->\s*Do am:\s*(\d+)%\s*Bom:\s*(ON|OFF)")
# Mo cong Serial lam Nano reset: cho bootloader + man hinh chao 1.5 s cua setup() xong moi gui
BOOT_WAIT_SECONDS = 3.5


def append_csv(path: Path, header: list[str], row: list) -> None:
    """Them 1 dong vao file CSV, tu tao file + dong tieu de neu chua co."""
    path.parent.mkdir(parents=True, exist_ok=True)
    is_new = not path.exists()
    with open(path, "a", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        if is_new:
            writer.writerow(header)
        writer.writerow(row)


class ArduinoLink:
    """Giu ket noi Serial voi Arduino: doc do am gui len (ghi CSV), gui ket qua AI xuong.
    Rut cap USB ra cam lai thi tu ket noi lai."""

    def __init__(self, port: str, baud: int):
        self.port, self.baud = port, baud
        self.ser = None
        self.ready_at = 0.0
        self.lock = threading.Lock()
        self.last_moisture_log = 0.0
        self._connect()
        threading.Thread(target=self._reader, daemon=True).start()

    def _connect(self) -> None:
        try:
            # serial_for_url nhan ca duong dan /dev/ttyUSB0 lan URL test nhu "loop://"
            self.ser = serial.serial_for_url(self.port, self.baud, timeout=1)
            self.ready_at = time.time() + BOOT_WAIT_SECONDS
            print(f"Da mo cong {self.port}, cho Arduino khoi dong lai...")
        except serial.SerialException as e:
            self.ser = None
            print(f"Chua mo duoc cong {self.port}: {e}")

    def _close(self) -> None:
        if self.ser is not None:
            try:
                self.ser.close()
            except serial.SerialException:
                pass
        self.ser = None

    def send(self, text: str) -> bool:
        """Gui 'AI:<text>' xuong Arduino. Tra ve True neu gui duoc."""
        with self.lock:
            if self.ser is None:
                return False
            wait = self.ready_at - time.time()
            if wait > 0:
                time.sleep(wait)
            try:
                self.ser.write(f"AI:{text}\n".encode("ascii", "replace"))
                self.ser.flush()
                return True
            except (serial.SerialException, OSError) as e:
                print(f"Loi gui Serial: {e}")
                self._close()
                return False

    def _reader(self) -> None:
        """Chay nen: doc tung dong Arduino gui len."""
        while True:
            ser = self.ser
            if ser is None:
                time.sleep(5)
                with self.lock:
                    if self.ser is None:
                        self._connect()
                continue
            try:
                line = ser.readline().decode("ascii", "replace").strip()
            except (serial.SerialException, OSError, TypeError, AttributeError):
                print(f"Mat ket noi {self.port}, se thu lai sau 5 giay")
                with self.lock:
                    self._close()
                continue
            if line:
                self._handle_line(line)

    def _handle_line(self, line: str) -> None:
        match = MOISTURE_RE.search(line)
        if not match:
            print(f"[Arduino] {line}")  # Vd "AI OK: Benh 97% TLB" xac nhan da nhan ket qua
            return
        # Dong do am den moi 0.5 s: chi in + ghi CSV moi MOISTURE_LOG_SECONDS giay
        now = time.time()
        if now - self.last_moisture_log < config.MOISTURE_LOG_SECONDS:
            return
        self.last_moisture_log = now
        print(f"[Arduino] {line}")
        raw, moisture, pump = match.groups()
        append_csv(MOISTURE_LOG, MOISTURE_LOG_HEADER,
                   [datetime.now().isoformat(timespec="seconds"), raw, moisture, pump, int("LOI CAM BIEN" in line)])


def capture_from_camera(urls: list[str]) -> bytes:
    """Tai 1 anh JPEG tu ESP32-CAM. Thu lan luot tung dia chi, toi da 3 vong."""
    params = {"flash": "1"} if config.CAMERA_FLASH else {}
    errors = []
    for attempt in range(3):
        if attempt:
            time.sleep(5)
        for url in urls:
            try:
                response = requests.get(url, params=params, timeout=config.CAMERA_TIMEOUT)
                response.raise_for_status()
                content_type = response.headers.get("Content-Type", "")
                if not content_type.startswith("image/"):
                    raise ValueError(f"khong phai anh (Content-Type: {content_type})")
                return response.content
            except (requests.RequestException, ValueError) as e:
                errors.append(f"{url}: {e}")
    raise RuntimeError("Khong lay duoc anh tu ESP32-CAM:\n  " + "\n  ".join(errors[-len(urls):]))


def run_check(predictor: Predictor, camera_urls: list[str], image_path: Path | None = None) -> str:
    """Lay anh (camera hoac file) -> luu -> AI phan tich -> ghi log. Tra ve chuoi hien LCD."""
    now = datetime.now()
    stamp = now.isoformat(timespec="seconds")
    try:
        data = image_path.read_bytes() if image_path else capture_from_camera(camera_urls)
        image = Image.open(io.BytesIO(data))
        image.load()
    except (RuntimeError, OSError, UnidentifiedImageError) as e:
        print(f"\n[{now:%Y-%m-%d %H:%M:%S}] {e}")
        append_csv(AI_LOG, AI_LOG_HEADER, [stamp, "", "camera_error", "", "", "", "Loi camera"])
        return "Loi camera"

    day_dir = config.CAPTURES_DIR / f"{now:%Y-%m-%d}"
    day_dir.mkdir(parents=True, exist_ok=True)
    raw_path = day_dir / f"{now:%H%M%S}{image_path.suffix.lower() if image_path else '.jpg'}"
    raw_path.write_bytes(data)

    result = predictor.analyze(image)
    annotate(image, result).save(day_dir / f"{now:%H%M%S}_ai.jpg", quality=90)
    text = lcd_text(result)

    print(f"\n[{now:%Y-%m-%d %H:%M:%S}] Anh: {raw_path}")
    print(describe(result))
    print(f"LCD: AI:{text}")
    name = config.CLASS_INFO.get(result["disease_class"], ("", ""))[1] if result["is_diseased"] else ""
    append_csv(AI_LOG, AI_LOG_HEADER, [stamp, raw_path, result["status"], f"{result['disease_prob']:.3f}",
                                       result["disease_class"] if result["is_diseased"] else "", name, text])
    return text


def main():
    parser = argparse.ArgumentParser(description="Chup anh ESP32-CAM, AI phan tich, gui ket qua xuong Arduino")
    parser.add_argument("--once", action="store_true", help="Chay 1 lan roi thoat")
    parser.add_argument("--image", type=Path, help="Dung anh co san thay vi chup tu ESP32-CAM")
    parser.add_argument("--camera-url", help="Dia chi chup anh, vd http://192.168.1.50/capture "
                                             "(mac dinh: cac dia chi trong config.CAMERA_URLS)")
    parser.add_argument("--no-serial", action="store_true", help="Khong ket noi Arduino")
    parser.add_argument("--port", default=config.SERIAL_PORT, help="Cong Serial cua Arduino")
    args = parser.parse_args()
    camera_urls = [args.camera_url] if args.camera_url else config.CAMERA_URLS

    predictor = Predictor()
    link = None if args.no_serial else ArduinoLink(args.port, config.SERIAL_BAUD)

    def publish(text: str) -> None:
        if link is not None and link.send(text):
            print(f"Da gui xuong Arduino: AI:{text}")

    if args.once:
        publish(run_check(predictor, camera_urls, args.image))
        time.sleep(1.5)  # Cho Arduino tra loi "AI OK: ..." truoc khi thoat
        return

    print(f"Service dang chay. Gio chup moi ngay: {', '.join(config.CAPTURE_TIMES)}. Nhan Ctrl+C de dung.")
    last_text = run_check(predictor, camera_urls, args.image)
    publish(last_text)
    last_sent = time.time()
    done_slots = set()  # Cac (ngay, gio) da chup, tranh chup 2 lan trong cung 1 phut
    try:
        while True:
            now = datetime.now()
            slot = now.strftime("%H:%M")
            if slot in config.CAPTURE_TIMES and (now.date(), slot) not in done_slots:
                done_slots.add((now.date(), slot))
                last_text = run_check(predictor, camera_urls, args.image)
                publish(last_text)
                last_sent = time.time()
            elif time.time() - last_sent >= config.RESEND_SECONDS:
                publish(last_text)  # Gui lai dinh ky: Nano co bi reset thi LCD van co ket qua
                last_sent = time.time()
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nDa dung service.")


if __name__ == "__main__":
    main()
