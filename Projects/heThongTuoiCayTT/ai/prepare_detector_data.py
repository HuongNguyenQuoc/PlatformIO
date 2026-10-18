"""Chuyen bo PlantDoc Object Detection (anh vuon that, moi chiec la co 1 khung) sang dinh dang YOLO
de train model tim la (train_detector.py). Chi chia 2 lop: 0 = la ca chua, 1 = la cay khac (ot, khoai tay, tao,
nho...). predict.py chi lay lop 0, nen la cay khac bi bo thay vi bi model benh ca chua doan bua.
Benh gi la viec cua model benh.

Tai du lieu (1 lan, ~1.9 GB):
    git clone --depth 1 https://github.com/pratikkayal/PlantDoc-Object-Detection-Dataset.git data/raw/plantdoc_od/repo

Dau ra:
    data/detector/images/{train,val}/*.jpg   anh da thu nho (canh dai toi da DET_MAX_SIZE)
    data/detector/labels/{train,val}/*.txt   moi dong "<lop> cx cy w h" (toa do 0..1)
    data/detector/leaf.yaml                  file mo ta du lieu cho ultralytics

Anh nao trung voi test_real (PlantDoc test) deu bi bo, de diem cham tren test_real van la anh model chua thay.
"""
import hashlib
import random
import shutil
import xml.etree.ElementTree as ET
from pathlib import Path

from PIL import Image
from tqdm import tqdm

import config

DET_MAX_SIZE = 1024  # Thu nho anh truoc cho train nhanh (YOLO train o 640, anh goc PlantDoc toi 4000 px)
VAL_RATIO = 0.1
# Ten lop PlantDoc cua la ca chua (vd "Tomato leaf late blight") -> lop 0; moi ten khac -> lop 1
TOMATO_PD_NAMES = {pd_name for _, pd_name in config.CLASSES.values() if pd_name}

Box = tuple[float, float, float, float]


def md5(path: Path) -> str:
    return hashlib.md5(path.read_bytes()).hexdigest()


def find_image(xml_path: Path) -> Path | None:
    """Anh di kem file .xml: cung ten, duoi bat ky (ten tai tu web co ca '?', vd 'a.jpg?itok=x.jpg')."""
    for p in xml_path.parent.glob(f"{xml_path.stem}.*"):
        if p.suffix != ".xml":
            return p
    return None


def read_voc(xml_path: Path, size: tuple[int, int]) -> list[tuple[str, Box]] | None:
    """Doc cac khung la (ten lop PlantDoc, khung) trong file .xml. Tra ve None neu kich thuoc ghi trong .xml
    khac anh that (nhan duoc ve tren anh ban khac -> khung lech, bo ca anh)."""
    root = ET.parse(xml_path).getroot()
    w, h = size
    if (int(float(root.findtext("size/width"))), int(float(root.findtext("size/height")))) != (w, h):
        return None
    objects = []
    for obj in root.iter("object"):
        b = obj.find("bndbox")
        x0, y0, x1, y1 = (float(b.findtext(k)) for k in ("xmin", "ymin", "xmax", "ymax"))
        x0, x1 = max(0.0, min(x0, x1)), min(float(w), max(x0, x1))
        y0, y1 = max(0.0, min(y0, y1)), min(float(h), max(y0, y1))
        if x1 - x0 >= 4 and y1 - y0 >= 4:  # Bo khung loi (dien tich ~0)
            objects.append(((obj.findtext("name") or "").strip(), (x0, y0, x1, y1)))
    return objects


def plantdoc_od_images() -> list[tuple[Path, Path, bool]]:
    """Moi anh PlantDoc Object Detection: (file .xml, file anh, co trung anh test_real khong).
    So trung theo ca ten file lan noi dung (1 so anh trung noi dung nhung khac ten)."""
    if not config.PD_OD_DIR.exists():
        raise SystemExit(f"Chua co {config.PD_OD_DIR}, xem cach tai o dau file prepare_detector_data.py")
    test_files = [p for p in (config.PD_DIR / "test").rglob("*") if p.is_file()]
    test_names = {p.name for p in test_files}
    test_hashes = {md5(p) for p in test_files}
    images = []
    for folder in ("TRAIN", "TEST"):
        for xml_path in sorted((config.PD_OD_DIR / folder).glob("*.xml")):
            img_path = find_image(xml_path)
            if img_path is not None:
                images.append((xml_path, img_path, img_path.name in test_names or md5(img_path) in test_hashes))
    return images


def main():
    # Bo anh trung test_real. TEST cua PlantDoc -> val; TRAIN cua PlantDoc -> 10% val, con lai train
    jobs = [(xml_path, img_path) for xml_path, img_path, in_test in plantdoc_od_images() if not in_test]
    rng = random.Random(config.SEED)
    rng.shuffle(jobs)
    n_val = round(sum(xml_path.parent.name == "TRAIN" for xml_path, _ in jobs) * VAL_RATIO)

    out = config.DETECTOR_DATA_DIR
    shutil.rmtree(out, ignore_errors=True)
    for split in ("train", "val"):
        (out / "images" / split).mkdir(parents=True)
        (out / "labels" / split).mkdir(parents=True)

    counts = {"train": [0, 0, 0], "val": [0, 0, 0]}  # [so anh, so la ca chua, so la khac]
    skipped = 0
    for i, (xml_path, img_path) in enumerate(tqdm(jobs, ncols=90)):
        if xml_path.parent.name == "TEST":
            split = "val"
        else:
            split = "val" if n_val > 0 else "train"
            n_val -= split == "val"
        try:
            # KHONG xoay anh theo EXIF: nhan duoc ve tren pixel goc. Luu lai khong kem EXIF
            # de ultralytics cung khong xoay -> anh va khung luon khop nhau.
            with Image.open(img_path) as img:
                objects = read_voc(xml_path, img.size)
                if objects is None:
                    skipped += 1
                    continue
                w, h = img.size
                img = img.convert("RGB")
                img.thumbnail((DET_MAX_SIZE, DET_MAX_SIZE))
                img.save(out / "images" / split / f"pd_{i:05d}.jpg", quality=92)
        except OSError as e:
            print(f" Khong doc duoc anh {img_path}: {e}")
            continue
        lines = [f"{0 if name in TOMATO_PD_NAMES else 1} "
                 f"{(x0 + x1) / 2 / w:.6f} {(y0 + y1) / 2 / h:.6f} {(x1 - x0) / w:.6f} {(y1 - y0) / h:.6f}"
                 for name, (x0, y0, x1, y1) in objects]
        (out / "labels" / split / f"pd_{i:05d}.txt").write_text("\n".join(lines) + "\n" if lines else "")
        counts[split][0] += 1
        counts[split][1] += sum(line.startswith("0 ") for line in lines)
        counts[split][2] += sum(line.startswith("1 ") for line in lines)

    (out / "leaf.yaml").write_text(f"path: {out}\ntrain: images/train\nval: images/val\nnames:\n  0: tomato_leaf\n  1: other_leaf\n")

    for split, (n_img, n_tomato, n_other) in counts.items():
        print(f"{split:<6} {n_img:>5} anh  {n_tomato:>6} la ca chua  {n_other:>6} la khac")
    print(f"Bo qua {skipped} anh co nhan lech kich thuoc")
    print(f"Xong: {out / 'leaf.yaml'}")


if __name__ == "__main__":
    main()
