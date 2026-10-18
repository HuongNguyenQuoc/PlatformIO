"""Tim tung chiec la ca chua trong anh bang model YOLO26n (file ONNX do train_detector.py xuat ra).
Model chia 2 lop: 0 = la ca chua, 1 = la cay khac. Chi tra ve lop 0, la cay khac bi bo.

Chi can onnxruntime, khong can ultralytics. App Android (LeafDetector.kt) lam y het cac buoc o day.
"""
from pathlib import Path

import numpy as np
import onnxruntime as ort
from PIL import Image

import config

PAD_COLOR = (114, 114, 114)  # Mau to phan thua khi dua anh ve hinh vuong, giong luc ultralytics train
CROP_PAD = 0.1  # Cat la rong hon khung 10% moi phia cho khoi mat mep la
TOMATO_CLASS = 0  # Lop "la ca chua" cua model tim la


def leaf_crop_box(box: tuple, size: tuple[int, int]) -> tuple[int, int, int, int]:
    """Vung cat 1 chiec la de dua vao model benh: hinh vuong quanh khung (canh = canh dai + 2*10%).
    Model benh cat giua anh 224x224; neu cat sat khung, la dai se bi cat mat 2 dau."""
    x0, y0, x1, y1 = box
    w, h = size
    side = max(x1 - x0, y1 - y0) * (1 + 2 * CROP_PAD)
    side = min(side, w, h)
    cx = min(max((x0 + x1) / 2, side / 2), w - side / 2)  # Day o vuong vao trong anh neu la sat mep
    cy = min(max((y0 + y1) / 2, side / 2), h - side / 2)
    return round(cx - side / 2), round(cy - side / 2), round(cx + side / 2), round(cy + side / 2)


class LeafDetector:
    def __init__(self, model_path: Path = config.DETECTOR_PATH):
        if not Path(model_path).exists():
            raise SystemExit(f"Chua co model tim la {model_path}. Hay chay 'python train_detector.py' truoc.")
        self.session = ort.InferenceSession(str(model_path), providers=["CPUExecutionProvider"])
        inp = self.session.get_inputs()[0]
        self.input_name = inp.name  # "images"
        self.size = inp.shape[2]    # 640

    def detect(self, image: Image.Image, conf: float) -> list[tuple[tuple[int, int, int, int], float]]:
        """Tra ve cac la ca chua: list (khung (x0, y0, x1, y1) tren anh goc, diem tin cay 0..1), diem cao truoc."""
        # Thu nho giu ti le cho canh dai = 640, dat vao giua o vuong 640x640 to xam (letterbox)
        w, h = image.size
        scale = min(self.size / w, self.size / h)
        nw, nh = round(w * scale), round(h * scale)
        px, py = (self.size - nw) // 2, (self.size - nh) // 2
        canvas = Image.new("RGB", (self.size, self.size), PAD_COLOR)
        canvas.paste(image.resize((nw, nh), Image.BILINEAR), (px, py))
        x = (np.asarray(canvas, dtype=np.float32) / 255.0).transpose(2, 0, 1)[None]  # [1, 3, 640, 640] RGB

        # Dau ra [1, 300, 6]: x0, y0, x1, y1 (toa do tren o 640x640), diem, lop (0 = la ca chua, 1 = la khac)
        out = self.session.run(None, {self.input_name: x})[0][0]
        leaves = []
        for x0, y0, x1, y1, score, cls in out:
            if score < conf or int(cls) != TOMATO_CLASS:
                continue
            # Doi ve toa do anh goc
            box = (max(0, round((x0 - px) / scale)), max(0, round((y0 - py) / scale)),
                   min(w, round((x1 - px) / scale)), min(h, round((y1 - py) / scale)))
            if box[2] > box[0] and box[3] > box[1]:
                leaves.append((box, float(score)))
        return sorted(leaves, key=lambda leaf: -leaf[1])
