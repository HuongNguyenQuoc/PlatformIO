# Cau hinh chung cho phan AI
from pathlib import Path

# Duong dan den thu muc chua file config
# AI_DIR = Path(__file__).parent
AI_DIR = Path(__file__).resolve().parent

DATA_DIR = AI_DIR / "data"
RAW_DIR = DATA_DIR / "raw"
PV_DIR = RAW_DIR / "plantvillage" / "raw" / "color" # Anh PlantVillage goc
PD_DIR = RAW_DIR / "plantdoc"
OWN_DIR = RAW_DIR / "own"  # Anh tu chup vuon nha (khong bat buoc): own/<ten lop>/*.jpg
PD_OD_DIR = RAW_DIR / "plantdoc_od" / "repo"  # PlantDoc Object Detection: anh vuon that + khung tung la (.xml)
DETECTOR_DATA_DIR = DATA_DIR / "detector"     # Du lieu YOLO do prepare_detector_data.py tao ra

MODELS_DIR = AI_DIR / "models"
CAPTURES_DIR = AI_DIR / "captures"
LOGS_DIR = AI_DIR / "logs"

# ===== 10 lop: chi la ca chua (bo ot, khoai tay tu 18/10/2026) =====
# Ten lop chuan -> (thu muc trong PlantVillage, thu muc trong PlantDoc hoac None neu PlantDoc khong co)
CLASSES = {
    "Tomato_Bacterial_spot": ("Tomato___Bacterial_spot", "Tomato leaf bacterial spot"),
    "Tomato_Early_blight":   ("Tomato___Early_blight", "Tomato Early blight leaf"),
    "Tomato_Late_blight":    ("Tomato___Late_blight", "Tomato leaf late blight"),
    "Tomato_Leaf_Mold":      ("Tomato___Leaf_Mold", "Tomato mold leaf"),
    "Tomato_Mosaic":         ("Tomato___Tomato_mosaic_virus", "Tomato leaf mosaic virus"),
    "Tomato_Septoria":       ("Tomato___Septoria_leaf_spot", "Tomato Septoria leaf spot"),
    "Tomato_Spider_mites":   ("Tomato___Spider_mites Two-spotted_spider_mite", "Tomato two spotted spider mites leaf"),
    "Tomato_Target_Spot":    ("Tomato___Target_Spot", None),
    "Tomato_Yellow_Curl":    ("Tomato___Tomato_Yellow_Leaf_Curl_Virus", "Tomato leaf yellow virus"),
    "Tomato_healthy":        ("Tomato___healthy", "Tomato leaf"),
}

# Ma 3 ky tu (hien tren LCD) va ten tieng Viet cua tung lop
CLASS_INFO = {
    "Tomato_Bacterial_spot": ("TBS", "Ca chua - dom vi khuan"),
    "Tomato_Early_blight":   ("TEB", "Ca chua - dom vong (benh som)"),
    "Tomato_Late_blight":    ("TLB", "Ca chua - moc suong (benh muon)"),
    "Tomato_Leaf_Mold":      ("TLM", "Ca chua - nam moc la"),
    "Tomato_Mosaic":         ("TMV", "Ca chua - virus kham la"),
    "Tomato_Septoria":       ("TSL", "Ca chua - dom la Septoria"),
    "Tomato_Spider_mites":   ("TSM", "Ca chua - nhen do"),
    "Tomato_Target_Spot":    ("TTS", "Ca chua - dom muc tieu"),
    "Tomato_Yellow_Curl":    ("TYC", "Ca chua - virus xoan vang la"),
    "Tomato_healthy":        ("---", "Ca chua - khoe"),
}

# Chia du lieu
VAL_RATIO = 0.1 # 10% PlantVillage lam tap validation
TEST_RATIO = 0.1 # 10% PlantVillage lam tap test
SEED = 42 # random seed
OWN_TEST_RATIO = 0.3 # Anh tu chup: 30% vao test_real (cham diem that), 70% vao train
MAX_SIZE = 512 # kich thuoc toi da cua anh dau vao cho model

# ===== Phan tich anh (predict.py) =====
MODEL_PATH = MODELS_DIR / "best.pt"
DETECTOR_PATH = MODELS_DIR / "leaf_detector.onnx"  # Model YOLO tim tung chiec la (train_detector.py)
DET_IMG_SIZE = 640       # Anh dua vao model tim la: 640x640 (thu nho giu ti le, phan thua to xam)
# Nguong chon theo test_real (anh that). Quy trinh 18/10 chi ca chua (python evaluate.py --scenes):
# 61 anh benh -> 56 "Benh", 3 "Nghi", 2 "Khong thay la"; 8 anh khoe -> 4 "Khoe", 3 "Nghi", 1 "Khong thay la".
DISEASE_THRESHOLD = 0.9  # Xac suat benh >= 90% thi bao "Benh"
SUSPECT_THRESHOLD = 0.6  # 60-90% thi bao "Nghi" (nghi ngo, nen ra xem cay), duoi 60% la "Khoe"
DET_CONF = 0.25          # Model tim la: khung la ca chua co diem tin cay duoi muc nay thi bo
LEAF_MIN_SIDE = 32       # La co canh ngan < 32 px (tren anh goc) thi bo: phong to len 224 chi con mo nhoe
MAX_LEAVES = 12          # Chi cham diem toi da 12 la chac chan nhat (anh nhieu la van nhanh)
MIN_VOTES = 2            # Can it nhat 2 la cung vuot nguong moi bao "Benh"; 1 la le chi bao "Nghi"
DARK_MAX_BRIGHTNESS = 35 # Do sang trung binh (0-255) duoi muc nay thi coi la anh qua toi (ban dem)

# ===== Camera ESP32-CAM (service.py) =====
# Thu lan luot tung dia chi. Neu may khong phan giai duoc esp32cam.local thi them IP that cua ESP32-CAM
# (xem tren Serial Monitor cua ESP32-CAM luc khoi dong), vd "http://192.168.1.50/capture"
CAMERA_URLS = ["http://esp32cam.local/capture"]
CAMERA_FLASH = False      # True: bat den flash khi chup (chi nen dung khi trong nha / thieu sang)
CAMERA_TIMEOUT = 20       # Giay cho moi lan tai anh
CAPTURE_TIMES = ["08:00", "16:00"]  # Gio chup anh + phan tich moi ngay

# ===== Arduino Nano (service.py) =====
SERIAL_PORT = "/dev/ttyUSB0"  # Nano clone (chip CH340) -> /dev/ttyUSB0; Nano chinh hang -> /dev/ttyACM0
SERIAL_BAUD = 9600            # Phai trung voi Serial.begin() trong src/main.cpp
RESEND_SECONDS = 60           # Gui lai che do phun suong + gio dinh ky, phong khi Nano bi reset
MOISTURE_LOG_SECONDS = 60     # Ghi do am Arduino gui len vao CSV toi da 1 lan / 60 giay