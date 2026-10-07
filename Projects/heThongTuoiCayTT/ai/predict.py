"""Phan tich 1 anh xem cay co dau hieu benh khong.

Model duoc train tren anh tung chiec la, con ESP32-CAM chup ca cay (1600x1200). Vi vay anh lon duoc chia
luoi o chong lan (config.TILE_GRID); o nao it mau la cay (dat, troi, chau...) thi bo qua. Toan anh + cac o
con lai duoc model cham diem, xac suat benh cua anh = cao nhat trong cac vung.

Cach chay:
    python predict.py anh.jpg                   Phan tich 1 anh
    python predict.py a.jpg b.jpg c.jpg         Phan tich nhieu anh (in gon moi anh 1 dong)
    python predict.py anh.jpg --save out.jpg    Luu them anh co ve khung cac vung da phan tich
    python predict.py --webcam                  Chup 1 khung hinh tu webcam laptop roi phan tich
"""
import argparse
from datetime import datetime
from pathlib import Path

import numpy as np
import torch
from PIL import Image, ImageDraw, ImageFont, ImageOps

import config
from model import IMG_SIZE, get_transforms, load_trained_model

TILE_OVERLAP = 0.25  # Moi o rong hon 25% so voi chia deu -> cac o chong len nhau, khong cat doi vet benh


def make_tiles(image: Image.Image, grid: int) -> list[tuple[tuple, tuple]]:
    """Chia anh thanh luoi grid x grid. Tra ve list (khung o chong lan de phan tich, khung o luoi de ve).
    Anh nho (o co canh ngan < IMG_SIZE) thi khong chia, vi phong to vung nho chi lam anh mo them."""
    if grid < 2:
        return []
    w, h = image.size
    tw = int(w / grid * (1 + TILE_OVERLAP))
    th = int(h / grid * (1 + TILE_OVERLAP))
    if min(tw, th) < IMG_SIZE:
        return []
    tiles = []
    for row in range(grid):
        for col in range(grid):
            x = round(col * (w - tw) / (grid - 1))
            y = round(row * (h - th) / (grid - 1))
            cell = (col * w // grid, row * h // grid, (col + 1) * w // grid, (row + 1) * h // grid)
            tiles.append(((x, y, x + tw, y + th), cell))
    return tiles


def color_stats(image: Image.Image) -> tuple[float, float]:
    """Tra ve (ti le pixel co mau la cay, do sang trung binh 0-255)."""
    hsv = np.asarray(image.resize((64, 64)).convert("HSV"), dtype=np.int16)
    h, s, v = hsv[..., 0], hsv[..., 1], hsv[..., 2]
    # Hue cua PIL tu 0-255 (tuong ung 0-360 do). 25..130 ~ 35..185 do: tu vang (la benh) den xanh la.
    # Bo pixel nhat mau (nen xam, troi trang) va pixel qua toi.
    plant = (h >= 25) & (h <= 130) & (s >= 50) & (v >= 40)
    return float(plant.mean()), float(v.mean())


class Predictor:
    """Doc model 1 lan, sau do phan tich bao nhieu anh cung duoc."""

    def __init__(self, model_path: Path = config.MODEL_PATH, device: torch.device | None = None):
        if not Path(model_path).exists():
            raise SystemExit(f"Chua co model {model_path}. Hay chay 'python train.py' truoc.")
        self.device = device or torch.device("cuda" if torch.cuda.is_available() else "cpu")
        self.model, self.class_names = load_trained_model(model_path, self.device)
        self.transform = get_transforms(train=False)
        self.healthy_idx = [i for i, name in enumerate(self.class_names) if name.endswith("_healthy")]

    @torch.no_grad()
    def _classify(self, crops: list[Image.Image]) -> torch.Tensor:
        """Cham diem nhieu vung anh cung luc. Tra ve tensor [so vung, so lop] xac suat."""
        batch = torch.stack([self.transform(c) for c in crops]).to(self.device)
        return torch.softmax(self.model(batch).float(), dim=1).cpu()

    def analyze(self, image: Image.Image) -> dict:
        """Phan tich 1 anh. Ket qua la dict, xem cac khoa trong lenh return cuoi ham."""
        image = ImageOps.exif_transpose(image).convert("RGB")
        leaf_ratio, brightness = color_stats(image)
        tiles = make_tiles(image, config.TILE_GRID)
        base = {"size": image.size, "leaf_ratio": leaf_ratio, "brightness": brightness,
                "n_tiles": len(tiles), "regions": [], "level": "none", "is_diseased": False,
                "disease_prob": 0.0, "disease_class": None, "disease_class_prob": 0.0}

        if brightness < config.DARK_MAX_BRIGHTNESS:
            return {**base, "status": "too_dark"}

        # Vung dau tien luon la toan anh (cell = None: khong ve), sau do la cac o phan lon la la cay
        areas = [((0, 0, *image.size), None)]
        areas += [(box, cell) for box, cell in tiles
                  if color_stats(image.crop(box))[0] >= config.TILE_LEAF_MIN_RATIO]
        if leaf_ratio < config.LEAF_MIN_RATIO and len(areas) == 1:
            return {**base, "status": "no_leaf"}

        probs = self._classify([image.crop(box) for box, _ in areas])
        disease_probs = 1 - probs[:, self.healthy_idx].sum(dim=1)  # P(benh) = 1 - tong P(cac lop khoe)

        regions = []
        for (box, cell), p, d in zip(areas, probs, disease_probs):
            p_disease = p.clone()
            p_disease[self.healthy_idx] = 0  # Chi xet cac lop benh de biet "neu benh thi la benh gi"
            k = int(p_disease.argmax())
            regions.append({"box": box, "cell": cell, "disease_prob": float(d), "level": disease_level(float(d)),
                            "disease_class": self.class_names[k], "disease_class_prob": float(p[k])})

        worst = max(regions, key=lambda r: r["disease_prob"])
        return {**base, "status": "ok", "regions": regions,
                "level": worst["level"],  # "healthy" / "suspect" / "diseased"
                "is_diseased": worst["level"] == "diseased",
                "disease_prob": worst["disease_prob"],
                "disease_class": worst["disease_class"],
                "disease_class_prob": worst["disease_class_prob"]}


def disease_level(prob: float) -> str:
    """Xep muc theo xac suat benh: duoi SUSPECT_THRESHOLD la khoe, tu DISEASE_THRESHOLD la benh, o giua la nghi."""
    if prob >= config.DISEASE_THRESHOLD:
        return "diseased"
    if prob >= config.SUSPECT_THRESHOLD:
        return "suspect"
    return "healthy"


def lcd_text(result: dict) -> str:
    """Chuoi toi da 13 ky tu, Arduino hien sau chu 'AI:' tren dong 2 cua LCD."""
    if result["status"] == "too_dark":
        return "Anh qua toi"
    if result["status"] == "no_leaf":
        return "Khong thay la"
    prob = result["disease_prob"]
    code = config.CLASS_INFO.get(result["disease_class"], ("???", ""))[0]
    # int() lam tron xuong cho khop nguong: 89.7% (muc "Nghi") hien "89%", lam tron len thanh "Nghi 90%" de gay nham
    if result["level"] == "diseased":
        return f"Benh {int(prob * 100)}% {code}"
    if result["level"] == "suspect":
        return f"Nghi {int(prob * 100)}% {code}"
    return f"Khoe {int((1 - prob) * 100)}%"


def describe(result: dict) -> str:
    """Mo ta ket qua bang vai dong chu de in ra man hinh."""
    w, h = result["size"]
    leaf_tiles = len(result["regions"]) - 1 if result["regions"] else 0
    lines = [f"Anh {w}x{h}, do sang {result['brightness']:.0f}/255, "
             f"{result['leaf_ratio']:.0%} dien tich mau la, {leaf_tiles}/{result['n_tiles']} o co la"]
    if result["status"] == "too_dark":
        lines.append("Ket qua: anh qua toi, khong phan tich")
    elif result["status"] == "no_leaf":
        lines.append("Ket qua: khong thay la cay trong anh (camera lech huong?)")
    else:
        verdict = {"diseased": "CO DAU HIEU BENH", "suspect": "NGHI NGO, nen ra xem cay",
                   "healthy": "Khoe manh"}[result["level"]]
        lines.append(f"Ket qua: {verdict} - xac suat benh {int(result['disease_prob'] * 100)}% "
                     f"(nghi tu {config.SUSPECT_THRESHOLD:.0%}, benh tu {config.DISEASE_THRESHOLD:.0%})")
        name = config.CLASS_INFO.get(result["disease_class"], ("", result["disease_class"]))[1]
        lines.append(f"Neu benh, kha nang nhat: {name} [{result['disease_class']}] "
                     f"{result['disease_class_prob']:.0%}")
    return "\n".join(lines)


LEVEL_COLORS = {"diseased": (230, 40, 40), "suspect": (255, 160, 0), "healthy": (40, 200, 60)}


def annotate(image: Image.Image, result: dict) -> Image.Image:
    """Ve o luoi da phan tich len anh: do = benh, cam = nghi ngo, xanh = khoe. O khong ve = it la, bo qua."""
    image = ImageOps.exif_transpose(image).convert("RGB")
    draw = ImageDraw.Draw(image)
    font = ImageFont.load_default(size=max(14, image.width // 45))
    line_w = max(2, image.width // 400)
    for region in result["regions"]:
        if region["cell"] is None:  # Vung toan anh, khong ve
            continue
        color = LEVEL_COLORS[region["level"]]
        x0, y0, x1, y1 = region["cell"]
        draw.rectangle((x0 + line_w, y0 + line_w, x1 - line_w, y1 - line_w), outline=color, width=line_w)
        code = "OK" if region["level"] == "healthy" else config.CLASS_INFO.get(region["disease_class"], ("?",))[0]
        draw.text((x0 + 3 * line_w, y0 + 2 * line_w), f"{region['disease_prob']:.0%} {code}", fill=color,
                  font=font, stroke_width=3, stroke_fill=(0, 0, 0))
    draw.text((10, image.height - font.size - 14), "AI:" + lcd_text(result), fill=(255, 255, 255), font=font,
              stroke_width=3, stroke_fill=(0, 0, 0))
    return image


def capture_webcam(index: int = 0) -> Image.Image:
    """Chup 1 khung hinh tu webcam (mac dinh /dev/video0)."""
    import cv2  # Chi can khi dung webcam

    cap = cv2.VideoCapture(index)
    if not cap.isOpened():
        raise SystemExit(f"Khong mo duoc webcam so {index}")
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)
    ok, frame = False, None
    for _ in range(15):  # Bo vai khung dau de camera tu chinh do sang
        ok, frame = cap.read()
    cap.release()
    if not ok:
        raise SystemExit("Webcam khong tra ve anh")
    return Image.fromarray(cv2.cvtColor(frame, cv2.COLOR_BGR2RGB))


def main():
    parser = argparse.ArgumentParser(description="Phan tich anh cay bang model da train")
    parser.add_argument("images", nargs="*", type=Path, help="Duong dan anh can phan tich")
    parser.add_argument("--webcam", action="store_true", help="Chup tu webcam laptop thay vi doc file")
    parser.add_argument("--save", type=Path, help="Luu anh co ve khung vao file nay (chi khi phan tich 1 anh)")
    parser.add_argument("--model", type=Path, default=config.MODEL_PATH)
    args = parser.parse_args()
    if not args.images and not args.webcam:
        parser.error("Can it nhat 1 duong dan anh, hoac --webcam")

    predictor = Predictor(args.model)

    if args.webcam:
        image = capture_webcam()
        config.CAPTURES_DIR.mkdir(parents=True, exist_ok=True)
        raw_path = config.CAPTURES_DIR / f"webcam_{datetime.now():%Y%m%d_%H%M%S}.jpg"
        image.save(raw_path, quality=95)
        print(f"Da luu anh webcam: {raw_path}")
        jobs = [(raw_path, image)]
    else:
        jobs = [(path, Image.open(path)) for path in args.images]

    if len(jobs) == 1:
        path, image = jobs[0]
        result = predictor.analyze(image)
        print(describe(result))
        print(f"LCD: AI:{lcd_text(result)}")
        save_path = args.save or (path.with_name(path.stem + "_ai.jpg") if args.webcam else None)
        if save_path:
            annotate(image, result).save(save_path, quality=90)
            print(f"Da luu anh ve khung: {save_path}")
        return

    for path, image in jobs:
        result = predictor.analyze(image)
        print(f"{str(path):<60} AI:{lcd_text(result)}")


if __name__ == "__main__":
    main()