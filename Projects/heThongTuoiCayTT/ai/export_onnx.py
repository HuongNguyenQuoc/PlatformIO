"""Script chuyen doi model PyTorch (best.pt) sang dinh dang ONNX (.onnx) de tich hop vao Mobile App (Flutter, React Native, Android, iOS).

Xuat ra:
    ai/models/leaf_disease_model.onnx : Model ONNX da tich hop san Softmax (tra ve xac suat 0.0 -> 1.0)
    ai/models/labels.txt             : Danh sach 15 ten lop (moi dong 1 lop, dung thu tu index)
    ai/models/labels.json            : Thong tin chi tiet (ma LCD, ten tieng Viet, tinh trang benh/khoe)
"""
import json
import sys
from pathlib import Path

import numpy as np
import onnx
import onnxruntime as ort
import torch
import torch.nn as nn

import config
from model import build_model, IMAGENET_MEAN, IMAGENET_STD, IMG_SIZE


class OnnxModelWrapper(nn.Module):
    """Boc model lai de them lop Softmax o dau ra.
    Giu cho lap trinh vien Mobile cuc ky tien loi: model tra thang ve xac suat (0% -> 100%),
    khong can phai tu viet ham tinh Softmax bang Dart/Kotlin/Swift nua.
    """
    def __init__(self, model: nn.Module):
        super().__init__()
        self.model = model
        self.softmax = nn.Softmax(dim=1)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        logits = self.model(x)
        probabilities = self.softmax(logits)
        return probabilities


def export():
    model_path = config.MODELS_DIR / "best.pt"
    if not model_path.exists():
        print(f"Loi: Khong tim thay file {model_path}")
        sys.exit(1)

    print(f"1. Dang doc checkpoint tu: {model_path}")
    checkpoint = torch.load(model_path, map_location="cpu")
    class_names = checkpoint["class_names"]
    num_classes = len(class_names)
    print(f"   -> So luong lop: {num_classes}")

    # Build va load state_dict
    raw_model = build_model(num_classes, pretrained=False)
    raw_model.load_state_dict(checkpoint["state_dict"])
    raw_model.eval()

    # Boc Softmax
    wrapped_model = OnnxModelWrapper(raw_model)
    wrapped_model.eval()

    # Tao dummy tensor dau vao: [batch_size=1, channels=3, height=224, width=224]
    dummy_input = torch.randn(1, 3, IMG_SIZE, IMG_SIZE, dtype=torch.float32)

    onnx_path = config.MODELS_DIR / "leaf_disease_model.onnx"
    print(f"\n2. Dang xuat sang ONNX: {onnx_path} ...")

    # Xoa file .data cu neu co
    old_data = config.MODELS_DIR / "leaf_disease_model.onnx.data"
    if old_data.exists():
        old_data.unlink()

    # Xuat ONNX voi dynamic batch size (ho tro ca batch 1 lan batch nhieu anh)
    # Dung dynamo=False de nhung toan bo trong so vao duy nhat 1 file .onnx doc lap (Standalone)
    torch.onnx.export(
        wrapped_model,
        dummy_input,
        str(onnx_path),
        export_params=True,
        opset_version=17,
        do_constant_folding=True,
        input_names=["input"],
        output_names=["probabilities"],
        dynamic_axes={
            "input": {0: "batch_size"},
            "probabilities": {0: "batch_size"}
        },
        dynamo=False
    )

    print(f"   -> Xuat thanh cong! Dung luong file: {onnx_path.stat().st_size / (1024*1024):.2f} MB")

    # Kiem tra model bang onnx package
    print("\n3. Dang kiem tra tinh hop le cua file ONNX (onnx.checker)...")
    onnx_model = onnx.load(str(onnx_path))
    onnx.checker.check_model(onnx_model)
    print("   -> File ONNX chuan hop le 100%!")

    # 4. Kiem thu so sanh PyTorch vs ONNX Runtime
    print("\n4. Dang kiem thu so sanh ket qua giua PyTorch va ONNX Runtime...")
    with torch.no_grad():
        pt_out = wrapped_model(dummy_input).numpy()

    ort_session = ort.InferenceSession(str(onnx_path), providers=["CPUExecutionProvider"])
    ort_inputs = {ort_session.get_inputs()[0].name: dummy_input.numpy()}
    ort_out = ort_session.run(None, ort_inputs)[0]

    max_diff = np.max(np.abs(pt_out - ort_out))
    print(f"   -> Do lech toi da giua PyTorch va ONNX: {max_diff:.8f}")
    assert max_diff < 1e-4, "Do lech vuot qua nguong cho phep!"
    print("   -> Ket qua PyTorch va ONNX khop hoan toan!")

    # 5. Xuat file labels.txt va labels.json
    labels_txt_path = config.MODELS_DIR / "labels.txt"
    print(f"\n5. Dang tao file nhan {labels_txt_path} ...")
    with open(labels_txt_path, "w", encoding="utf-8") as f:
        for c in class_names:
            f.write(f"{c}\n")

    labels_json_path = config.MODELS_DIR / "labels.json"
    print(f"   Dang tao file chi tiet {labels_json_path} ...")
    labels_detail = []
    for idx, c in enumerate(class_names):
        code, vi_name = config.CLASS_INFO.get(c, ("---", c))
        labels_detail.append({
            "index": idx,
            "id": c,
            "code": code,
            "name_vi": vi_name,
            "is_healthy": "healthy" in c.lower()
        })

    with open(labels_json_path, "w", encoding="utf-8") as f:
        json.dump({
            "model_name": "EfficientNet-B0 Leaf Disease Classifier",
            "img_size": IMG_SIZE,
            "mean": IMAGENET_MEAN,
            "std": IMAGENET_STD,
            "classes_count": num_classes,
            "labels": labels_detail
        }, f, ensure_ascii=False, indent=2)

    print("\n=== HOAN TAT XUAT MODEL ONNX VA TAI LIEU TICH HOP ===")
    print(f"1. Model:  {onnx_path}")
    print(f"2. Nhan:   {labels_txt_path}")
    print(f"3. Chi tiet: {labels_json_path}")


if __name__ == "__main__":
    export()
