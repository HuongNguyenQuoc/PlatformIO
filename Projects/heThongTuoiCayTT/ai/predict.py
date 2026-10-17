"""Phan tich 1 anh xem cay co dau hieu benh khong.

Model benh duoc train tren anh tung chiec la, con ESP32-CAM chup ca vuon: nhieu la, qua, dat, chau...
Neu dua ca anh (hay tung o luoi) vao model benh, qua ca chua hay dat nau cung bi doan thanh 1 benh nao do.
Vi vay lam 2 buoc:
  1. Model tim la (YOLO26n, detector.py) khoanh tung chiec la. Qua, dat, troi... khong phai la nen bi bo.
  2. Cat tung la dua vao model benh, roi gop bang bo phieu: phai co it nhat config.MIN_VOTES la cung benh
     moi bao "Benh" (chi 1 la benh thi bao "Nghi"). Ten benh = benh duoc cac la bi nghi chon nhieu nhat.

Cach chay:
    python predict.py anh.jpg                   Phan tich 1 anh
    python predict.py a.jpg b.jpg c.jpg         Phan tich nhieu anh (in gon moi anh 1 dong)
    python predict.py anh.jpg --save out.jpg    Luu them anh co ve khung tung la da phan tich
    python predict.py --webcam                  Chup 1 khung hinh tu webcam laptop roi phan tich
"""
import argparse
from datetime import datetime
from pathlib import Path

import numpy as np
import torch
from PIL import Image, ImageDraw, ImageFont, ImageOps

import config
from detector import LeafDetector, leaf_crop_box
from model import get_transforms, load_trained_model


def brightness(image: Image.Image) -> float:
    """Do sang trung binh 0-255 (kenh V cua HSV)."""
    return float(np.asarray(image.resize((64, 64)).convert("HSV"))[..., 2].mean())


def disease_level(prob: float) -> str:
    """Xep muc theo xac suat benh: duoi SUSPECT_THRESHOLD la khoe, tu DISEASE_THRESHOLD la benh, o giua la nghi."""
    if prob >= config.DISEASE_THRESHOLD:
        return "diseased"
    if prob >= config.SUSPECT_THRESHOLD:
        return "suspect"
    return "healthy"


def vote(disease_probs: list[float]) -> tuple[str, float]:
    """Gop xac suat benh cua tung la thanh (muc, xac suat benh) cua ca anh.
    Lay la "benh" thu MIN_VOTES (vd la benh nhi): no vuot nguong nghia la co it nhat MIN_VOTES la vuot nguong.
    Anh chi co 1 la thi la do tu quyet."""
    ranked = sorted(disease_probs, reverse=True)
    score = ranked[min(config.MIN_VOTES, len(ranked)) - 1]
    level = disease_level(score)
    if level == "healthy" and ranked[0] >= config.DISEASE_THRESHOLD:
        return "suspect", ranked[0]  # Chi 1 la benh ro: chua du phieu bao "Benh", nhung nen ra xem cay
    return level, score


class Predictor:
    """Doc 2 model 1 lan, sau do phan tich bao nhieu anh cung duoc."""

    def __init__(self, model_path: Path = config.MODEL_PATH, detector_path: Path = config.DETECTOR_PATH,
                 device: torch.device | None = None):
        if not Path(model_path).exists():
            raise SystemExit(f"Chua co model {model_path}. Hay chay 'python train.py' truoc.")
        self.device = device or torch.device("cuda" if torch.cuda.is_available() else "cpu")
        self.model, self.class_names = load_trained_model(model_path, self.device)
        self.detector = LeafDetector(detector_path)
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
        base = {"size": image.size, "brightness": brightness(image), "n_found": 0, "regions": [],
                "level": "none", "is_diseased": False, "disease_prob": 0.0, "disease_class": None,
                "disease_class_prob": 0.0, "healthy_class": None}

        if base["brightness"] < config.DARK_MAX_BRIGHTNESS:
            return {**base, "status": "too_dark"}

        # Buoc 1: tim la. Bo la qua nho (mo, model benh khong nhin ro), giu toi da MAX_LEAVES la chac chan nhat
        found = self.detector.detect(image, config.DET_CONF)
        leaves = [(box, score) for box, score in found
                  if min(box[2] - box[0], box[3] - box[1]) >= config.LEAF_MIN_SIDE][:config.MAX_LEAVES]
        base["n_found"] = len(found)
        if not leaves:
            return {**base, "status": "no_leaf"}

        # Buoc 2: cham diem tung la
        probs = self._classify([image.crop(leaf_crop_box(box, image.size)) for box, _ in leaves])
        disease_probs = 1 - probs[:, self.healthy_idx].sum(dim=1)  # P(benh) = 1 - tong P(cac lop khoe)
        p_disease_only = probs.clone()
        p_disease_only[:, self.healthy_idx] = 0  # Chi xet cac lop benh de biet "neu benh thi la benh gi"

        regions = []
        for (box, score), p, d in zip(leaves, p_disease_only, disease_probs):
            k = int(p.argmax())
            regions.append({"box": box, "score": score, "disease_prob": float(d), "level": disease_level(float(d)),
                            "disease_class": self.class_names[k], "disease_class_prob": float(p[k])})

        # Buoc 3: bo phieu
        level, disease_prob = vote(disease_probs.tolist())
        # Benh gi: trung binh xac suat cac lop benh cua nhung la bi nghi (khong co la nao bi nghi thi lay ca anh)
        voters = [i for i, d in enumerate(disease_probs.tolist()) if d >= config.SUSPECT_THRESHOLD] \
            or list(range(len(leaves)))
        p_vote = p_disease_only[voters].mean(dim=0)
        k = int(p_vote.argmax())
        # Cay gi (khi khoe): lop "khoe" co trung binh xac suat cao nhat tren moi la
        h = self.healthy_idx[int(probs[:, self.healthy_idx].mean(dim=0).argmax())]
        return {**base, "status": "ok", "regions": regions,
                "level": level,  # "healthy" / "suspect" / "diseased"
                "is_diseased": level == "diseased",
                "disease_prob": disease_prob,
                "disease_class": self.class_names[k],
                "disease_class_prob": float(p_vote[k]),
                "healthy_class": self.class_names[h]}


def lcd_text(result: dict) -> str:
    """Chuoi toi da 16 ky tu, Arduino hien nguyen dong 2 cua LCD, vd "BENH 99%"."""
    if result["status"] == "too_dark":
        return "ANH QUA TOI"
    if result["status"] == "no_leaf":
        return "KHONG THAY LA"
    prob = result["disease_prob"]
    # int() lam tron xuong cho khop nguong: 89.7% (muc "Nghi") hien "89%", lam tron len thanh "NGHI 90%" de gay nham
    if result["level"] == "diseased":
        return f"BENH {int(prob * 100)}%"
    if result["level"] == "suspect":
        return f"NGHI {int(prob * 100)}%"
    return f"KHOE {int((1 - prob) * 100)}%"


def describe(result: dict) -> str:
    """Mo ta ket qua bang vai dong chu de in ra man hinh."""
    w, h = result["size"]
    regions = result["regions"]
    n_sick = sum(r["level"] != "healthy" for r in regions)
    lines = [(f"Anh {w}x{h}, do sang {result['brightness']:.0f}/255, tim thay {result['n_found']} la, "
              f"cham diem {len(regions)} la ({n_sick} la nghi/benh)")]
    if result["status"] == "too_dark":
        lines.append("Ket qua: anh qua toi, khong phan tich")
    elif result["status"] == "no_leaf":
        lines.append("Ket qua: khong thay la cay trong anh (camera lech huong? la qua nho?)")
    else:
        verdict = {"diseased": "CO DAU HIEU BENH", "suspect": "NGHI NGO, nen ra xem cay",
                   "healthy": "Khoe manh"}[result["level"]]
        lines.append(f"Ket qua: {verdict} - xac suat benh {int(result['disease_prob'] * 100)}% "
                     f"(nghi tu {config.SUSPECT_THRESHOLD:.0%}, benh tu {config.DISEASE_THRESHOLD:.0%}, "
                     f"can {config.MIN_VOTES} la)")
        name = config.CLASS_INFO.get(result["disease_class"], ("", result["disease_class"]))[1]
        lines.append(f"Neu benh, kha nang nhat: {name} [{result['disease_class']}] "
                     f"{result['disease_class_prob']:.0%}")
    return "\n".join(lines)


LEVEL_COLORS = {"diseased": (230, 40, 40), "suspect": (255, 160, 0), "healthy": (40, 200, 60)}


def annotate(image: Image.Image, result: dict) -> Image.Image:
    """Ve khung tung la da phan tich len anh: do = benh, cam = nghi ngo, xanh = khoe."""
    image = ImageOps.exif_transpose(image).convert("RGB")
    draw = ImageDraw.Draw(image)
    font = ImageFont.load_default(size=max(14, image.width // 45))
    line_w = max(2, image.width // 400)
    for region in result["regions"]:
        color = LEVEL_COLORS[region["level"]]
        x0, y0, x1, y1 = region["box"]
        draw.rectangle((x0, y0, x1, y1), outline=color, width=line_w)
        code = "OK" if region["level"] == "healthy" else config.CLASS_INFO.get(region["disease_class"], ("?",))[0]
        draw.text((x0 + 2 * line_w, y0 + line_w), f"{region['disease_prob']:.0%} {code}", fill=color,
                  font=font, stroke_width=3, stroke_fill=(0, 0, 0))
    draw.text((10, image.height - font.size - 14), lcd_text(result), fill=(255, 255, 255), font=font,
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
    parser.add_argument("--detector", type=Path, default=config.DETECTOR_PATH)
    args = parser.parse_args()
    if not args.images and not args.webcam:
        parser.error("Can it nhat 1 duong dan anh, hoac --webcam")

    predictor = Predictor(args.model, args.detector)

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
