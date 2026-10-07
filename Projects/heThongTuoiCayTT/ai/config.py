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

MODELS_DIR = AI_DIR / "models"
CAPTURES_DIR = AI_DIR / "captures"
LOGS_DIR = AI_DIR / "logs"

# ===== 15 lop =====
# Ten lop chuan -> (thu muc trong PlantVillage, thu muc trong PlantDoc hoac None neu PlantDoc khong co)
CLASSES = {
    "Pepper_Bacterial_spot": ("Pepper,_bell___Bacterial_spot", "Bell_pepper leaf spot"),
    "Pepper_healthy":        ("Pepper,_bell___healthy", "Bell_pepper leaf"),
    "Potato_Early_blight":   ("Potato___Early_blight", "Potato leaf early blight"),
    "Potato_Late_blight":    ("Potato___Late_blight", "Potato leaf late blight"),
    "Potato_healthy":        ("Potato___healthy", None),
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
    "Pepper_Bacterial_spot": ("PBS", "Ot - dom vi khuan"),
    "Pepper_healthy":        ("---", "Ot - khoe"),
    "Potato_Early_blight":   ("PEB", "Khoai tay - dom vong (benh som)"),
    "Potato_Late_blight":    ("PLB", "Khoai tay - moc suong (benh muon)"),
    "Potato_healthy":        ("---", "Khoai tay - khoe"),
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
# Nguong chon theo test_real (anh that): o 0.9 bat duoc 78/86 anh benh, khong bao nham anh khoe nao (0/16);
# o 0.6 bat duoc 84/86 nhung bao nham 5/16 anh khoe. Anh khoe bi nham co P(benh) 0.64-0.87 -> roi vao "Nghi".
DISEASE_THRESHOLD = 0.9  # Xac suat benh >= 90% thi bao "Benh"
SUSPECT_THRESHOLD = 0.6  # 60-90% thi bao "Nghi" (nghi ngo, nen ra xem cay), duoi 60% la "Khoe"
TILE_GRID = 3            # Anh lon duoc chia luoi 3x3 o (co chong lan) de soi tung vung la
LEAF_MIN_RATIO = 0.15    # Ca anh co it hon 15% pixel mau la cay (xanh/vang) -> "Khong thay la"
TILE_LEAF_MIN_RATIO = 0.5  # 1 o chi duoc cham diem khi >= 50% la la cay: o lan nhieu dat nau de bi nham la vet benh
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
RESEND_SECONDS = 60           # Gui lai ket qua dinh ky, phong khi Nano bi reset
MOISTURE_LOG_SECONDS = 60     # Ghi do am Arduino gui len vao CSV toi da 1 lan / 60 giay