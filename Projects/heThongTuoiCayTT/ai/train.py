"""Huan luyen CNN EfficientNet-B0 (transfer learning) nhan dien 15 lop benh la ca chua / ot / khoai tay.

Cach chay:  python train.py                              (mac dinh: 3 epoch pha 1 + 12 epoch pha 2, ~22 phut)
            python train.py --epochs-head 1 --epochs 1   (chay thu nhanh ~3 phut)
"""
import argparse
import time

import torch
import torch.nn as nn
from sklearn.metrics import f1_score
from torch.utils.data import DataLoader, WeightedRandomSampler
from torchvision import datasets
from tqdm import tqdm

import config
from model import IMG_SIZE, build_model, get_transforms, set_backbone_trainable


def make_loaders(batch_size: int, workers: int):
    """Tao DataLoader cho tap train va val."""
    train_ds = datasets.ImageFolder(config.DATA_DIR / "train", transform=get_transforms(train=True))
    val_ds = datasets.ImageFolder(config.DATA_DIR / "val", transform=get_transforms(train=False))

    # Can bang lop: moi anh co trong so = 1 / (so anh cua lop no)
    # -> lop it anh (Potato_healthy) duoc boc nhieu lan hon, lop nhieu anh (Tomato_Yellow_Curl) boc it hon
    targets = torch.tensor(train_ds.targets)
    class_counts = torch.bincount(targets)
    sample_weights = 1.0 / class_counts[targets].float()
    sampler = WeightedRandomSampler(sample_weights, num_samples=len(train_ds), replacement=True)

    train_loader = DataLoader(train_ds, batch_size=batch_size, sampler=sampler,
                              num_workers=workers, pin_memory=True, persistent_workers=True)
    val_loader = DataLoader(val_ds, batch_size=batch_size * 2, shuffle=False,
                            num_workers=workers, pin_memory=True, persistent_workers=True)
    return train_ds.classes, train_loader, val_loader


def train_one_epoch(model, loader, criterion, optimizer, scaler, device):
    """Cho model hoc het 1 luot tap train. Tra ve (loss trung binh, do chinh xac)."""
    model.train()
    total_loss, correct, seen = 0.0, 0, 0
    for images, labels in tqdm(loader, desc="  train", ncols=90, leave=False):
        images = images.to(device, non_blocking=True)
        labels = labels.to(device, non_blocking=True)

        optimizer.zero_grad(set_to_none=True)
        # autocast: tinh bang float16 tren GPU -> nhanh hon, ton it VRAM hon
        with torch.autocast(device_type=device.type, dtype=torch.float16, enabled=device.type == "cuda"):
            outputs = model(images)            # [batch, 15] diem so cho tung lop
            loss = criterion(outputs, labels)  # Do sai lech giua du doan va nhan that
        scaler.scale(loss).backward()          # Lan truyen nguoc: tinh gradient
        scaler.step(optimizer)                 # Cap nhat trong so
        scaler.update()

        total_loss += loss.item() * labels.size(0)
        correct += (outputs.argmax(dim=1) == labels).sum().item()
        seen += labels.size(0)
    return total_loss / seen, correct / seen


@torch.no_grad()
def evaluate(model, loader, device):
    """Cham diem model tren 1 tap. Tra ve (accuracy, macro-F1)."""
    model.eval()
    all_preds, all_labels = [], []
    for images, labels in tqdm(loader, desc="  val  ", ncols=90, leave=False):
        images = images.to(device, non_blocking=True)
        with torch.autocast(device_type=device.type, dtype=torch.float16, enabled=device.type == "cuda"):
            outputs = model(images)
        all_preds.append(outputs.argmax(dim=1).cpu())
        all_labels.append(labels)
    preds = torch.cat(all_preds).numpy()
    labels = torch.cat(all_labels).numpy()
    acc = float((preds == labels).mean())  # float() doi tu so numpy ve so Python thuong
    f1 = float(f1_score(labels, preds, average="macro"))
    return acc, f1


def main():
    parser = argparse.ArgumentParser(description="Train CNN phat hien benh la cay")
    parser.add_argument("--epochs-head", type=int, default=3, help="So epoch pha 1 (chi train lop cuoi)")
    parser.add_argument("--epochs", type=int, default=12, help="So epoch pha 2 (fine-tune toan bo)")
    parser.add_argument("--batch-size", type=int, default=32)
    parser.add_argument("--workers", type=int, default=6, help="So tien trinh doc anh song song")
    args = parser.parse_args()

    torch.manual_seed(config.SEED)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Thiet bi: {torch.cuda.get_device_name(0) if device.type == 'cuda' else 'CPU'}")

    class_names, train_loader, val_loader = make_loaders(args.batch_size, args.workers)
    print(f"{len(class_names)} lop, {len(train_loader.dataset)} anh train, {len(val_loader.dataset)} anh val")

    model = build_model(len(class_names)).to(device)
    criterion = nn.CrossEntropyLoss(label_smoothing=0.1)
    scaler = torch.amp.GradScaler(device.type, enabled=device.type == "cuda")

    config.MODELS_DIR.mkdir(parents=True, exist_ok=True)
    best_path = config.MODELS_DIR / "best.pt"
    best_f1 = 0.0

    # (ten pha, co train backbone khong, learning rate, so epoch)
    phases = [
        ("Pha 1: dong bang backbone, chi train lop cuoi", False, 1e-3, args.epochs_head),
        ("Pha 2: fine-tune toan bo model", True, 1e-4, args.epochs),
    ]
    for phase_name, train_backbone, lr, epochs in phases:
        print(f"\n===== {phase_name} ({epochs} epoch, lr={lr}) =====")
        set_backbone_trainable(model, train_backbone)
        params = [p for p in model.parameters() if p.requires_grad]
        optimizer = torch.optim.AdamW(params, lr=lr, weight_decay=1e-4)
        scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=epochs)

        for epoch in range(1, epochs + 1):
            start = time.time()
            train_loss, train_acc = train_one_epoch(model, train_loader, criterion, optimizer, scaler, device)
            val_acc, val_f1 = evaluate(model, val_loader, device)
            scheduler.step()  # Giam dan learning rate theo duong cong cosine

            note = ""
            if val_f1 > best_f1:
                best_f1 = val_f1
                torch.save({
                    "state_dict": model.state_dict(),
                    "class_names": class_names,
                    "img_size": IMG_SIZE,
                    "val_acc": val_acc,
                    "val_f1": val_f1,
                }, best_path)
                note = "  <- luu best.pt"
            print(f"Epoch {epoch:2d}/{epochs} | loss {train_loss:.3f} | train acc {train_acc:.3f} "
                  f"| val acc {val_acc:.3f} | val F1 {val_f1:.3f} | {time.time() - start:.0f}s{note}")

    print(f"\nXong! Macro-F1 tot nhat tren val: {best_f1:.4f}")
    print(f"Model da luu tai: {best_path}")


if __name__ == "__main__":
    main()
