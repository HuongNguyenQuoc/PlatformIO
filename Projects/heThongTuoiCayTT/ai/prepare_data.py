"""Gom anh PlantVillage + PlantDoc thanh 4 tap de train va danh gia model.

Dau ra (moi lop la 1 thu muc con):
    data/train/<lop>/      80% PlantVillage + toan bo PlantDoc train + 70% anh tu chup
    data/val/<lop>/        10% PlantVillage
    data/test_pv/<lop>/    10% PlantVillage
    data/test_real/<lop>/  PlantDoc test + 30% anh tu chup (anh chup ngoai vuon that)

Anh tu chup (khong bat buoc) dat tai data/raw/own/<ten lop trong config.CLASSES>/*.jpg
"""
import random
import shutil
from pathlib import Path

from PIL import Image, ImageOps
from tqdm import tqdm

import config

SPLITS = ["train", "val", "test_pv", "test_real"]
IMAGE_EXTS = {".jpg", ".jpeg", ".png"}

def list_images(folder: Path) -> list[Path]:
    """Lay danh sach file anh trong 1 thu muc (sap xep theo ten de ket qua on dinh)."""
    if not folder.exists():
        return []
    return sorted([f for f in folder.iterdir() if f.suffix.lower() in IMAGE_EXTS])

def save_image(src: Path, dst: Path) -> bool:
    """Doc anh, chuyen ve RGB, thu nho neu qua lon roi luu thanh JPEG.
    Tra ve False neu anh bi hong khong doc duoc."""
    try:
        with Image.open(src) as img:
            transposed = ImageOps.exif_transpose(img) # xoay anh dung chieu (anh dien thoai hay bi nam ngang)
            if transposed is not None:
                img = transposed
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

    # 2. Tao du 15 thu muc lop trong MOI tap, ke ca khi rong,
    #    de so thu tu lop (0..14) giong het nhau o moi tap
    for split in SPLITS:
        for cls in config.CLASSES.keys():
            (config.DATA_DIR / split / cls).mkdir(parents=True, exist_ok=True)

    counts = {cls: dict.fromkeys(SPLITS, 0) for cls in config.CLASSES.keys()}

    for cls, (pv_name, pd_name) in config.CLASSES.items():
        # 3. Lap danh sach viec can lam: (file nguon, tap dich, tien to ten file)
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

        # 4. Luu tung anh vao dung thu muc, ten file: pv_00012.jpg / pd_00345.jpg
        for i, (src, split, prefix) in enumerate(tqdm(jobs, desc=cls, ncols=90)):
            dst = config.DATA_DIR / split / cls / f"{prefix}_{i:05d}.jpg"
            if save_image(src, dst):
                counts[cls][split] += 1

    # 5. In bang thong ke so anh moi lop trong moi tap
    print(f"\n{'Class:':<30}" + "".join(f"{split:<12}" for split in SPLITS))
    for cls, c in counts.items():
        print(f"{cls:<30}" + "".join(f"{c[s]:>12}" for s in SPLITS))
    totals = [sum(c[s] for c in counts.values()) for s in SPLITS]
    print(f"{'Total:':<30}" + "".join(f"{total:>12}" for total in totals))

if __name__ == "__main__":
    main()
