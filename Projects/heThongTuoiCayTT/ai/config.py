# Cau hinh chung cho phan AI
from pathlib import Path

# Duong dan den thu muc chua file config
# AI_DIR = Path(__file__).parent
AI_DIR = Path(__file__).resolve().parent

DATA_DIR = AI_DIR / "data"
RAW_DIR = DATA_DIR / "raw"
PV_DIR = RAW_DIR / "plantvillage" / "raw" / "color" # Anh PlantVillage goc
PD_DIR = RAW_DIR / "plantdoc"

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

# Chia du lieu
VAL_RATIO = 0.1 # 10% PlantVillage lam tap validation
TEST_RATIO = 0.1 # 10% PlantVillage lam tap test
SEED = 42 # random seed
MAX_SIZE = 512 # kich thuoc toi da cua anh dau vao cho model