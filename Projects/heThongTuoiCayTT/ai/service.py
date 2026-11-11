"""Dich vu chay tren may tinh: lay anh cay, AI phan tich, gui ket qua cho Pico (mo phong Wokwi) qua MQTT.

He thong chi chay mo phong nen khong co camera that: moi lan kiem tra lay ngau nhien 1 anh trong thu muc
anh mau (config.SAMPLE_IMAGES_DIR, mac dinh data/test_real), hoac anh chi dinh bang --image.

Cach chay (Pico dang chay tren Wokwi, xem docs/wokwi.md):
    python service.py                          Chay lien tuc: phan tich ngay khi bat, sau do theo config.CAPTURE_TIMES
    python service.py --interval 2             Chay lien tuc, 2 phut phan tich 1 lan (de demo)
    python service.py --once                   Phan tich 1 anh ngau nhien, gui cho Pico roi thoat
    python service.py --once --image anh.jpg   Dung anh chi dinh
    python service.py --image-dir thu_muc      Lay anh ngau nhien tu thu muc khac (tim ca thu muc con)
    python service.py --no-mqtt                Chi phan tich, khong ket noi Pico

Ket qua luu tai:
    captures/<ngay>/<gio>.jpg      anh da phan tich   captures/<ngay>/<gio>_ai.jpg   anh co ve khung tung la
    logs/ai_results.csv            ket qua AI         logs/sensors.csv              so lieu cam bien Pico gui len
"""
import argparse
import csv
import json
import random
import secrets
import threading
import time
from datetime import datetime
from pathlib import Path

import paho.mqtt.client as mqtt
from PIL import Image, UnidentifiedImageError

import config
from predict import Predictor, annotate, describe, lcd_text

AI_LOG = config.LOGS_DIR / "ai_results.csv"
AI_LOG_HEADER = ["thoi_gian", "anh", "trang_thai", "xac_suat_benh", "lop_benh", "ten_benh", "lcd"]
SENSOR_LOG = config.LOGS_DIR / "sensors.csv"
# Truong trong JSON Pico gui len (topic .../telemetry) -> cot trong sensors.csv
SENSOR_FIELDS = {
    "time": "gio_thiet_bi", "moisture": "do_am_dat", "soil_fault": "loi_cam_bien", "temperature": "nhiet_do",
    "air_humidity": "do_am_kk", "lux": "anh_sang_lux", "tank_level": "muc_nuoc", "pump_state": "bom",
    "mist": "phun_suong", "mist_mode": "che_do_phun",
}
IMAGE_EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}
# Che do phun suong gui xuong Pico ("MIST:<1-3>"), khop MistMode trong lib/FarmCore. KEEP: giu che do dang chay
MIST_KEEP, MIST_NORMAL, MIST_STOP, MIST_BOOST = 0, 1, 2, 3


def append_csv(path: Path, header: list[str], row: list) -> None:
    """Them 1 dong vao file CSV, tu tao file + dong tieu de neu chua co."""
    path.parent.mkdir(parents=True, exist_ok=True)
    is_new = not path.exists()
    with open(path, "a", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        if is_new:
            writer.writerow(header)
        writer.writerow(row)


class DeviceLink:
    """Ket noi MQTT voi Pico: nhan so lieu cam bien (ghi CSV), gui lenh (AI:, MIST:) xuong.
    Mat mang thi paho tu ket noi lai."""

    def __init__(self, host: str, port: int, prefix: str):
        self.host, self.prefix = host, prefix
        self.connected = threading.Event()
        self.last_sensor_log = 0.0
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"tuoicaytt-ai-{secrets.token_hex(3)}")
        self.client.on_connect = self._on_connect
        self.client.on_disconnect = self._on_disconnect
        self.client.on_message = self._on_message
        self.client.reconnect_delay_set(min_delay=1, max_delay=30)
        print(f"Dang ket noi MQTT {host}:{port}...")
        self.client.connect_async(host, port, keepalive=30)
        self.client.loop_start()

    def _on_connect(self, client, userdata, flags, reason_code, properties) -> None:
        if reason_code.is_failure:
            print(f"Ket noi MQTT loi: {reason_code}")
            return
        print(f"Da ket noi MQTT {self.host}. Topic: {self.prefix}/...")
        client.subscribe([(f"{self.prefix}/telemetry", 0), (f"{self.prefix}/ack", 0), (f"{self.prefix}/status", 1)])
        self.connected.set()

    def _on_disconnect(self, client, userdata, flags, reason_code, properties) -> None:
        self.connected.clear()
        if reason_code != 0:
            print(f"Mat ket noi MQTT ({reason_code}), dang thu lai...")

    def _on_message(self, client, userdata, msg) -> None:
        kind = msg.topic.rsplit("/", 1)[-1]
        payload = msg.payload.decode("utf-8", "replace")
        if kind == "telemetry":
            self._handle_telemetry(payload)
        elif kind == "status":
            print(f"[Pico] {'dang chay' if payload == 'online' else 'mat ket noi'} ({payload})")
        else:
            print(f"[Pico] {payload}")  # Tra loi lenh, vd "AI OK", "MIST OK 2"

    def _handle_telemetry(self, payload: str) -> None:
        """So lieu den moi 5 giay: chi in + ghi CSV moi SENSOR_LOG_SECONDS giay."""
        try:
            data = json.loads(payload)
        except json.JSONDecodeError:
            return
        now = time.time()
        if now - self.last_sensor_log < config.SENSOR_LOG_SECONDS:
            return
        self.last_sensor_log = now
        print(f"[Pico] {data.get('time') or '--:--:--'}  Dat {data.get('moisture')}%  "
              f"KK {data.get('temperature')}C {data.get('air_humidity')}%  As {data.get('lux')} lx  "
              f"Be {data.get('tank_level')}%  Bom {data.get('pump_state')}  Phun {'ON' if data.get('mist') else 'OFF'}")
        append_csv(SENSOR_LOG, ["thoi_gian", *SENSOR_FIELDS.values()],
                   [datetime.now().isoformat(timespec="seconds"), *(data.get(k) for k in SENSOR_FIELDS)])

    def wait_connected(self, timeout: float) -> bool:
        return self.connected.wait(timeout)

    def send(self, line: str) -> bool:
        """Gui 1 lenh cho Pico, vd 'AI:BENH 97%|Ca chua - moc suong', 'MIST:2'. Tra ve True neu gui duoc."""
        if not self.connected.is_set():
            print(f"Chua ket noi MQTT, khong gui duoc: {line}")
            return False
        info = self.client.publish(f"{self.prefix}/cmd", line, qos=1)
        try:
            info.wait_for_publish(timeout=5)
        except (RuntimeError, ValueError) as e:
            print(f"Loi gui MQTT: {e}")
            return False
        return info.is_published()

    def close(self) -> None:
        self.client.loop_stop()
        self.client.disconnect()


def pick_image(image_path: Path | None, image_dir: Path) -> Path:
    """Anh chi dinh, hoac 1 anh ngau nhien trong image_dir (tim ca thu muc con)."""
    if image_path:
        return image_path
    images = [p for p in image_dir.rglob("*") if p.suffix.lower() in IMAGE_EXTS] if image_dir.is_dir() else []
    if not images:
        raise FileNotFoundError(f"Khong co anh nao trong {image_dir}. Chay prepare_data.py, "
                                "hoac chi dinh --image / --image-dir")
    return random.choice(images)


def mist_mode(result: dict) -> int:
    """Che do phun suong theo tinh trang la."""
    if result["status"] != "ok":
        return MIST_KEEP  # Anh toi, khong thay la: khong biet la ra sao
    if result["level"] == "healthy":
        return MIST_NORMAL
    if result["level"] == "suspect":
        return MIST_STOP  # Chua chac benh gi: dung cho an toan
    if result["disease_class"] == "Tomato_Spider_mites":
        return MIST_BOOST  # Nhen do so am
    if result["disease_class"] in ("Tomato_Mosaic", "Tomato_Yellow_Curl"):
        return MIST_NORMAL  # Virus lay qua con trung / tiep xuc, do am la khong lam benh nang them
    return MIST_STOP  # Nam, vi khuan: la uot lam benh lan nhanh


def lcd_detail(result: dict) -> str:
    """Dong 2 tren LCD: ten benh goi y (khong dau, dai hon 16 ky tu thi Pico chay chu)."""
    if result["status"] != "ok":
        return ""
    if result["level"] == "healthy":
        return "La ca chua khoe"
    name = config.CLASS_INFO.get(result["disease_class"], ("", ""))[1]
    return name if result["level"] == "diseased" else f"Nghi: {name}"


def run_check(predictor: Predictor, image_path: Path | None, image_dir: Path) -> tuple[str, str, int]:
    """Lay anh -> luu -> AI phan tich -> ghi log. Tra ve (dong 1 LCD, dong 2 LCD, che do phun)."""
    now = datetime.now()
    stamp = now.isoformat(timespec="seconds")
    try:
        source = pick_image(image_path, image_dir)
        data = source.read_bytes()
        image = Image.open(source)
        image.load()
    except (OSError, UnidentifiedImageError) as e:
        print(f"\n[{now:%Y-%m-%d %H:%M:%S}] {e}")
        append_csv(AI_LOG, AI_LOG_HEADER, [stamp, "", "image_error", "", "", "", "LOI ANH"])
        return "LOI ANH", "", MIST_KEEP

    day_dir = config.CAPTURES_DIR / f"{now:%Y-%m-%d}"
    day_dir.mkdir(parents=True, exist_ok=True)
    raw_path = day_dir / f"{now:%H%M%S}{source.suffix.lower()}"
    raw_path.write_bytes(data)

    result = predictor.analyze(image)
    annotate(image, result).save(day_dir / f"{now:%H%M%S}_ai.jpg", quality=90)
    text, detail = lcd_text(result), lcd_detail(result)

    print(f"\n[{now:%Y-%m-%d %H:%M:%S}] Anh: {source}")
    print(describe(result))
    print(f"LCD: {text} | {detail}")
    name = config.CLASS_INFO.get(result["disease_class"], ("", ""))[1] if result["is_diseased"] else ""
    append_csv(AI_LOG, AI_LOG_HEADER, [stamp, raw_path, result["status"], f"{result['disease_prob']:.3f}",
                                       result["disease_class"] if result["is_diseased"] else "", name, text])
    return text, detail, mist_mode(result)


def main():
    parser = argparse.ArgumentParser(description="AI phan tich anh cay, gui ket qua cho Pico mo phong qua MQTT")
    parser.add_argument("--once", action="store_true", help="Phan tich 1 lan roi thoat")
    parser.add_argument("--image", type=Path, help="Phan tich anh nay thay vi chon ngau nhien")
    parser.add_argument("--image-dir", type=Path, default=config.SAMPLE_IMAGES_DIR,
                        help=f"Thu muc lay anh ngau nhien (mac dinh {config.SAMPLE_IMAGES_DIR})")
    parser.add_argument("--interval", type=float, metavar="PHUT",
                        help="Phan tich moi PHUT phut thay vi theo config.CAPTURE_TIMES")
    parser.add_argument("--no-mqtt", action="store_true", help="Khong ket noi Pico")
    args = parser.parse_args()

    predictor = Predictor()
    link = None if args.no_mqtt else DeviceLink(config.MQTT_HOST, config.MQTT_PORT, config.MQTT_TOPIC_PREFIX)
    if link is not None and not link.wait_connected(15):
        print("Chua ket noi duoc MQTT sau 15 giay, van tiep tuc va tu thu lai")

    mist = MIST_NORMAL  # Che do phun dang chay: chua co ket qua thi phun binh thuong

    def publish(text: str, detail: str, mode: int) -> None:
        """Gui che do phun + ket qua moi: Pico hien len LCD 20 giay."""
        nonlocal mist
        if mode != MIST_KEEP:
            mist = mode
        if link is not None and link.send(f"MIST:{mist}") and link.send(f"AI:{text}|{detail}"):
            print(f"Da gui cho Pico: AI:{text}|{detail}  (phun che do {mist})")

    def check() -> None:
        publish(*run_check(predictor, args.image, args.image_dir))

    try:
        if args.once:
            check()
            time.sleep(2)  # Cho Pico tra loi "AI OK" truoc khi thoat
            return

        schedule = f"{args.interval:g} phut 1 lan" if args.interval else f"luc {', '.join(config.CAPTURE_TIMES)}"
        print(f"Service dang chay, phan tich {schedule}. Nhan Ctrl+C de dung.")
        check()
        last_check = last_sync = time.time()
        done_slots = set()  # Cac (ngay, gio) da phan tich, tranh lam 2 lan trong cung 1 phut
        while True:
            now = datetime.now()
            slot = now.strftime("%H:%M")
            if args.interval:
                due = time.time() - last_check >= args.interval * 60
            else:
                due = slot in config.CAPTURE_TIMES and (now.date(), slot) not in done_slots
                if due:
                    done_slots.add((now.date(), slot))
            if due:
                check()
                last_check = last_sync = time.time()
            elif link is not None and time.time() - last_sync >= config.RESEND_SECONDS:
                link.send(f"MIST:{mist}")  # Gui lai dinh ky: Pico khoi dong lai van lay lai che do phun
                last_sync = time.time()
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nDa dung service.")
    finally:
        if link is not None:
            link.close()


if __name__ == "__main__":
    main()
