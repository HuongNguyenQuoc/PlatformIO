"""Dinh nghia model CNN (EfficientNet-B0), cac phep bien doi anh va ham doc model da train."""
from pathlib import Path

import torch
import torch.nn as nn
from torchvision import models, transforms

IMG_SIZE = 224  # Kich thuoc anh dua vao model (224x224 la chuan cua EfficientNet-B0)

# Trung binh va do lech chuan mau cua bo anh ImageNet.
# Model pretrained da hoc voi anh duoc chuan hoa theo 2 bo so nay, nen ta phai chuan hoa giong vay.
IMAGENET_MEAN = [0.485, 0.456, 0.406]
IMAGENET_STD = [0.229, 0.224, 0.225]

def build_model(num_classes: int, pretrained: bool = True) -> nn.Module:
    """Tao EfficientNet-B0, thay lop cuoi (1000 lop ImageNet) bang lop moi co num_classes dau ra."""
    weights = models.EfficientNet_B0_Weights.IMAGENET1K_V1 if pretrained else None
    model = models.efficientnet_b0(weights=weights)
    # classifier = Sequential(Dropout, Linear(1280 -> 1000)) -> thay Linear bang Linear(1280 -> num_classes)
    in_features = model.classifier[1].in_features
    model.classifier[1] = nn.Linear(in_features, num_classes)
    return model

def set_backbone_trainable(model: nn.Module, trainable: bool) -> None:
    """Dong bang (False) hoac mo khoa (True) phan trich dac trung (backbone) cua model."""
    for param in model.features.parameters():
        param.requires_grad = trainable

def load_trained_model(path: Path, device: torch.device) -> tuple[nn.Module, list[str]]:
    """Doc file .pt do train.py luu, tra ve (model da o che do eval, danh sach ten lop)."""
    checkpoint = torch.load(path, map_location=device)
    class_names = checkpoint["class_names"]
    model = build_model(len(class_names), pretrained=False)  # Trong so se lay tu file, khong can tai ImageNet
    model.load_state_dict(checkpoint["state_dict"])
    model.to(device).eval()
    return model, class_names

def get_transforms(train: bool) -> transforms.Compose:
    """Phep bien doi anh: luc train thi bien doi ngau nhien (augmentation), luc danh gia thi co dinh."""
    if train:
        return transforms.Compose([
            transforms.RandomResizedCrop(IMG_SIZE, scale=(0.4, 1.0)), # Cat 1 vung ngau nhien roi phong ve 224x224
            transforms.RandomHorizontalFlip(), # Lat ngang
            transforms.RandomVerticalFlip(), # Lat doc
            transforms.RandomRotation(degrees=20), # Xoay ngau nhien trong khoang -20 -> 20 do
            transforms.ColorJitter(brightness=0.4, contrast=0.4, saturation=0.3, hue=0.02), # Gia lap anh sang, toi, tuong phan, mau sac bi thay doi
            transforms.RandomApply([transforms.GaussianBlur(5)], p=0.2), # Lam mo Gaussian voi xac suat 20% (camera rung / lech net)
            transforms.ToTensor(),
            transforms.Normalize(mean=IMAGENET_MEAN, std=IMAGENET_STD),
            transforms.RandomErasing(p=0.25), # Xoa 1 o chu nhat ngau nhien (gia lap bi che khuat)
        ])
    return transforms.Compose([
        transforms.Resize(256), # Thu nho canh ngan cua anh ve 256 (giu ti le)
        transforms.CenterCrop(IMG_SIZE), # Cat giua anh ve 224x224
        transforms.ToTensor(),
        transforms.Normalize(mean=IMAGENET_MEAN, std=IMAGENET_STD)
    ])
