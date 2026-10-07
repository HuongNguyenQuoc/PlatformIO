"""Cham diem model da train tren cac tap test: accuracy, macro-F1, bao cao tung lop,
ma tran nham lan, va chi so khoe/benh (dung dung nguong nhu predict.py).

Cach chay: python evaluate.py                     (danh gia ca test_pv va test_real)
           python evaluate.py --split test_real
"""
import argparse

import numpy as np
import torch
from sklearn.metrics import classification_report, confusion_matrix, f1_score
from torch.utils.data import DataLoader
from torchvision import datasets

import config
from model import get_transforms, load_trained_model


@torch.no_grad()
def predict_split(model, split, class_names, device, workers):
    """Cho model du doan toan bo 1 tap. Tra ve (xac suat [N, so lop], nhan that [N])."""
    # allow_empty: test_real co 3 lop khong co anh nao, van giu thu muc rong de so thu tu lop khong lech
    ds = datasets.ImageFolder(config.DATA_DIR / split, transform=get_transforms(train=False), allow_empty=True)
    if ds.classes != class_names:
        raise SystemExit(f"Danh sach lop trong data/{split} khac voi luc train, hay chay lai prepare_data.py")
    loader = DataLoader(ds, batch_size=64, num_workers=workers)
    all_probs, all_labels = [], []
    for images, labels in loader:
        outputs = model(images.to(device))
        all_probs.append(torch.softmax(outputs.float(), dim=1).cpu())
        all_labels.append(labels)
    return torch.cat(all_probs).numpy(), torch.cat(all_labels).numpy()


def report(split, probs, labels, class_names):
    """In ket qua danh gia cua 1 tap."""
    preds = probs.argmax(axis=1)
    present = sorted(set(labels.tolist()))  # Chi tinh cac lop co anh trong tap nay
    acc = float((preds == labels).mean())
    f1 = float(f1_score(labels, preds, labels=present, average="macro", zero_division=0))

    print(f"\n{'=' * 70}\n{split}: {len(labels)} anh, {len(present)} lop\n{'=' * 70}")
    print(f"Accuracy: {acc:.3f}   Macro-F1: {f1:.3f}\n")
    print(classification_report(labels, preds, labels=present, target_names=[class_names[i] for i in present],
                                digits=3, zero_division=0))

    # Ma tran nham lan: hang = lop that, cot = lop model doan (dung so thu tu de bang gon)
    cm = confusion_matrix(labels, preds, labels=range(len(class_names)))
    print("Ma tran nham lan (hang = that, cot = du doan):")
    print("    " + "".join(f"{j:>4}" for j in range(len(class_names))))
    for i, row in enumerate(cm):
        if i in present:
            print(f"{i:>2}  " + "".join(f"{v:>4}" if v else "   ." for v in row) + f"   {class_names[i]}")

    # Cac cap hay nham nhat
    errors = [(cm[i, j], i, j) for i in range(len(cm)) for j in range(len(cm)) if i != j and cm[i, j] > 0]
    if errors:
        print("\nNham nhieu nhat:")
        for count, i, j in sorted(errors, reverse=True)[:8]:
            print(f"  {count:>3} anh {class_names[i]:<22} -> doan thanh {class_names[j]}")

    # Chi so khoe / benh: dung dung cach predict.py ra quyet dinh
    healthy_idx = [i for i, name in enumerate(class_names) if name.endswith("_healthy")]
    pred_sick = (1 - probs[:, healthy_idx].sum(axis=1)) >= config.DISEASE_THRESHOLD
    true_sick = ~np.isin(labels, healthy_idx)
    tp, fn = int((pred_sick & true_sick).sum()), int((~pred_sick & true_sick).sum())
    fp, tn = int((pred_sick & ~true_sick).sum()), int((~pred_sick & ~true_sick).sum())
    print(f"\nKhoe/Benh (nguong {config.DISEASE_THRESHOLD:.0%}): dung {(tp + tn) / len(labels):.1%} | "
          f"bat duoc {tp}/{tp + fn} anh benh | bao nham {fp}/{fp + tn} anh khoe la benh")
    return acc, f1


def main():
    parser = argparse.ArgumentParser(description="Danh gia model tren tap test")
    parser.add_argument("--split", choices=["val", "test_pv", "test_real"], nargs="+",
                        default=["test_pv", "test_real"])
    parser.add_argument("--workers", type=int, default=4)
    args = parser.parse_args()

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model, class_names = load_trained_model(config.MODEL_PATH, device)
    print("Cac lop:")
    for i, name in enumerate(class_names):
        print(f"  {i:>2} {name}")

    summary = [(split, *report(split, *predict_split(model, split, class_names, device, args.workers), class_names))
               for split in args.split]

    print(f"\n{'=' * 70}\nTong ket")
    for split, acc, f1 in summary:
        print(f"  {split:<10} accuracy {acc:.3f}   macro-F1 {f1:.3f}")


if __name__ == "__main__":
    main()
