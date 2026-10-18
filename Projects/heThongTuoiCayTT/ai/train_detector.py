"""Train model YOLO26n tim tung chiec la trong anh (2 lop: 0 = la ca chua, 1 = la cay khac),
roi xuat ONNX cho predict.py va app Android.

Can chay prepare_detector_data.py truoc. Can them thu vien: pip install ultralytics (chi de train,
predict.py va app chi can file .onnx).

Cach chay:  python train_detector.py                 (mac dinh 60 epoch, ~35 phut tren RTX 2050)
            python train_detector.py --epochs 1      (chay thu)
            python train_detector.py --export-only   (chi xuat lai ONNX tu lan train truoc)

Dau ra: models/leaf_detector.onnx (vao [1,3,640,640] RGB 0..1, ra [1,300,6] = x1,y1,x2,y2,diem,lop;
        model YOLO26 tu loc khung trung nen khong can NMS; detector.py chi lay lop 0)
"""
import argparse
import shutil

from ultralytics import YOLO, settings

import config

RUNS_DIR = config.MODELS_DIR / "detector_runs"
# Ultralytics mac dinh tai model (vd yolo26n.pt cho buoc kiem tra AMP) vao <thu muc git goc>/weights.
# Doi ve models/ (da nam trong .gitignore) de khong lam ban repo.
settings.update({"weights_dir": str(config.MODELS_DIR), "runs_dir": str(RUNS_DIR)})
BEST_PT = RUNS_DIR / "leaf" / "weights" / "best.pt"


def main():
    parser = argparse.ArgumentParser(description="Train model YOLO tim la")
    parser.add_argument("--epochs", type=int, default=60)
    parser.add_argument("--batch", type=int, default=16)  # 16 vua 4 GB VRAM o anh 640
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--export-only", action="store_true")
    args = parser.parse_args()

    if not args.export_only:
        data = config.DETECTOR_DATA_DIR / "leaf.yaml"
        if not data.exists():
            raise SystemExit(f"Chua co {data}, hay chay 'python prepare_detector_data.py' truoc")
        # Bat dau tu model YOLO26n da hoc san tren COCO (80 loai vat): chi can day them "la ca chua / la cay khac"
        model = YOLO(config.MODELS_DIR / "yolo26n.pt")
        model.train(data=data, epochs=args.epochs, imgsz=config.DET_IMG_SIZE, batch=args.batch,
                    workers=args.workers, cache="disk", patience=15, seed=config.SEED,
                    project=RUNS_DIR, name="leaf", exist_ok=True)

    # nms=False: dung dau "1-1" cua YOLO26, tu loc khung trung -> ra [1,300,6], app khong phai tu viet NMS.
    # opset 17 giong model benh (export_onnx.py), onnxruntime-android 1.30 doc duoc
    onnx_path = YOLO(BEST_PT).export(format="onnx", imgsz=config.DET_IMG_SIZE, opset=17, simplify=True, nms=False)
    shutil.copy(onnx_path, config.DETECTOR_PATH)
    print(f"Da xuat: {config.DETECTOR_PATH}")


if __name__ == "__main__":
    main()
