"""Gom anh PlantVillage + PlantDoc thanh 5 tap de train va danh gia model.

Dau ra (moi lop la 1 thu muc con):
    data/train/<lop>/      80% PlantVillage + toan bo PlantDoc train + tung la cat tu PlantDoc Object Detection
                           + 70% anh tu chup
    data/val/<lop>/        10% PlantVillage
    data/test_pv/<lop>/    10% PlantVillage
    data/test_real/<lop>/  PlantDoc test + 30% anh tu chup (anh chup ngoai vuon that)
    data/test_leaf/<lop>/  Tung la cat tu anh PlantDoc test (dung kieu anh model tim la dua vao model benh)

La cat tu PlantDoc Object Detection (neu da tai, xem prepare_detector_data.py): moi khung la co ten benh rieng,
cat bang dung ham leaf_crop_box() (detector.py) ma predict.py dung. Anh trung test_real chi vao test_leaf, khong vao train.

Anh tu chup (khong bat buoc) dat tai data/raw/own/<ten lop trong config.CLASSES>/*.jpg
"""
import random
import shutil
from pathlib import Path

from PIL import Image, ImageOps
from tqdm import tqdm

import config
from detector import leaf_crop_box
from prepare_detector_data import plantdoc_od_images, read_voc

SPLITS = ["train", "val", "test_pv", "test_real", "test_leaf"]
IMAGE_EXTS = {".jpg", ".jpeg", ".png"}

def list_images(folder: Path) -> list[Path]:
    """Lay danh sach file anh trong 1 thu muc (sap xep theo ten de ket qua on dinh)."""
    if not folder.exists():
        return []
    return sorted([f for f in folder.iterdir() if f.suffix.lower() in IMAGE_EXTS])

def plantdoc_leaves() -> dict[str, list[tuple[Path, tuple, str]]]:
    """Cac khung la PlantDoc Object Detection thuoc cac lop trong config.CLASSES: {lop: [(anh, vung cat, tap)]}.
    Chua tai bo nay thi tra ve rong (van chay duoc nhu cu)."""
    leaves = {cls: [] for cls in config.CLASSES}
    if not config.PD_OD_DIR.exists():
        print(f"Khong co {config.PD_OD_DIR}: bo qua la cat tu PlantDoc Object Detection")
        return leaves
    pd_to_cls = {pd_name: cls for cls, (_, pd_name) in config.CLASSES.items() if pd_name}
    for xml_path, img_path, in_test in plantdoc_od_images():
        with Image.open(img_path) as img:
            size = img.size
        for name, box in read_voc(xml_path, size) or []:
            if name in pd_to_cls and min(box[2] - box[0], box[3] - box[1]) >= config.LEAF_MIN_SIDE:
                leaves[pd_to_cls[name]].append((img_path, leaf_crop_box(box, size), "test_leaf" if in_test else "train"))
    return leaves


def save_image(src: Path, dst: Path, crop: tuple | None = None) -> bool:
    """Doc anh, chuyen ve RGB, (cat 1 vung neu co crop), thu nho neu qua lon roi luu thanh JPEG.
    Tra ve False neu anh bi hong khong doc duoc."""
    try:
        with Image.open(src) as img:
            if crop is not None:
                # Khung la PlantDoc ve tren pixel goc, chua xoay theo EXIF -> cat truoc, khong xoay
                img = img.convert("RGB").crop(crop)
            elif (transposed := ImageOps.exif_transpose(img)) is not None:
                img = transposed # xoay anh dung chieu (anh dien thoai hay bi nam ngang)
            img = img.convert("RGB") # PNG trong suot / anh xam -> RGB 3 kenh mau
            img.thumbnail((config.MAX_SIZE, config.MAX_SIZE)) # just resize into smaller size, keep aspect ratio
            img.save(dst, format="JPEG", quality=95) # luu thanh JPEG de giam dung luong
        return True

    except Exception as e:
        print(f" Khong doc duoc anh {src}: {e}")
        return False

def main():
    rng = random.Random(config.SEED)

    # 1. Xoa ket qua cu cua lan chay truoc (chi 4 thu muc dau ra, KHONG dong vao data/raw)
    for split in SPLITS:
        shutil.rmtree(config.DATA_DIR / split, ignore_errors=True)

    # 2. Tao du thu muc cho moi lop trong MOI tap, ke ca khi rong,
    #    de so thu tu lop (0..9) giong het nhau o moi tap
    for split in SPLITS:
        for cls in config.CLASSES.keys():
            (config.DATA_DIR / split / cls).mkdir(parents=True, exist_ok=True)

    counts = {cls: dict.fromkeys(SPLITS, 0) for cls in config.CLASSES.keys()}
    pd_leaves = plantdoc_leaves()

    for cls, (pv_name, pd_name) in config.CLASSES.items():
        # 3. Lap danh sach viec can lam: (file nguon, tap dich, tien to ten file[, vung cat])
        jobs = []

        # PlantVillage: tron ngau nhien roi chia 80/10/10 NGAY TRONG TUNG LOP
        pv_files = list_images(config.PV_DIR / pv_name)
        rng.shuffle(pv_files)
        n_val = round(len(pv_files) * config.VAL_RATIO)
        n_test = round(len(pv_files) * config.TEST_RATIO)
        jobs += [(f, "val", "pv") for f in pv_files[:n_val]]
        jobs += [(f, "test_pv", "pv") for f in pv_files[n_val:n_val+n_test]]
        jobs += [(f, "train", "pv") for f in pv_files[n_val + n_test:]]

        # PlantDoc: tac gia da chia san train/test, dua thang vao train va test_real
        if pd_name is not None:
            jobs += [(f, "train", "pd") for f in list_images(config.PD_DIR / "train" / pd_name)]
            jobs += [(f, "test_real", "pd") for f in list_images(config.PD_DIR / "test" / pd_name)]

        # Anh tu chup (neu co data/raw/own/<lop>/): 30% vao test_real, con lai vao train
        own_files = list_images(config.OWN_DIR / cls)
        rng.shuffle(own_files)
        n_own_test = round(len(own_files) * config.OWN_TEST_RATIO)
        jobs += [(f, "test_real", "own") for f in own_files[:n_own_test]]
        jobs += [(f, "train", "own") for f in own_files[n_own_test:]]

        # Tung la cat tu PlantDoc Object Detection
        jobs += [(f, split, "leaf", crop) for f, crop, split in pd_leaves[cls]]

        # 4. Luu tung anh vao dung thu muc, ten file: pv_00012.jpg / pd_00345.jpg / leaf_01234.jpg
        for i, (src, split, prefix, *crop) in enumerate(tqdm(jobs, desc=cls, ncols=90)):
            dst = config.DATA_DIR / split / cls / f"{prefix}_{i:05d}.jpg"
            if save_image(src, dst, crop[0] if crop else None):
                counts[cls][split] += 1

    # 5. In bang thong ke so anh moi lop trong moi tap
    print(f"\n{'Class:':<30}" + "".join(f"{split:<12}" for split in SPLITS))
    for cls, c in counts.items():
        print(f"{cls:<30}" + "".join(f"{c[s]:>12}" for s in SPLITS))
    totals = [sum(c[s] for c in counts.values()) for s in SPLITS]
    print(f"{'Total:':<30}" + "".join(f"{total:>12}" for total in totals))

if __name__ == "__main__":
    main()
