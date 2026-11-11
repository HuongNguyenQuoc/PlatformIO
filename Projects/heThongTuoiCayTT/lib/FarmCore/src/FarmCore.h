// Logic dieu khien thuan C++ (khong goi ham Arduino): quyet dinh tuoi, phun suong va quy doi so do cam bien.
// Tach rieng khoi phan cung de de doc, de kiem thu va dung lai; src/main.cpp chi doc cam bien, bat tat LED,
// hien LCD va lo mang.
#pragma once

namespace farm {

// ===================== Tuoi theo do am dat =====================

struct IrrigationConfig {
  int moistureLow;            // Do am (%) duoi muc nay thi bat dau dot tuoi
  int moistureHigh;           // Tu muc nay tro len thi ket thuc dot tuoi
  unsigned long dryConfirmMs; // Phai kho lien tuc chung nay ms moi bat dau tuoi, tranh 1 lan doc nhieu
  unsigned long pumpOnMs;     // Moi lan bom chay chung nay ms
  unsigned long soakMs;       // Cho nuoc ngam xuong toi cam bien giua 2 lan bom
  int maxPulses;              // Toi da so lan bom moi dot tuoi
  unsigned long restMs;       // Bom du maxPulses ma chua du am thi nghi chung nay ms cho nuoc lan deu
  int minRise;                // Bom du maxPulses ma do am tang it hon muc nay: khoa bom (hong bom, tuot ong)
  int maxIncompleteCycles;    // Chung nay dot lien tiep khong du am: khoa bom, tranh ngap chau
};

enum class PumpState {
  Off,          // Khong tuoi
  On,           // Bom dang chay
  Soaking,      // Dang trong dot tuoi, cho nuoc ngam giua 2 lan bom
  Resting,      // Dot truoc chua du am, dang nghi restMs
  Locked,       // Bi khoa: nghi hong bom / tuot ong, cho nguoi kiem tra roi mo khoa
  NoWater,      // Be het nuoc
  SensorFault,  // Cam bien do am loi, khong tin so do
};

class Irrigation {
 public:
  explicit Irrigation(const IrrigationConfig &config);

  // Goi moi chu ky dieu khien. Tra ve true neu bom phai chay.
  bool update(unsigned long now, int moisture, bool sensorFault, bool tankEmpty);
  // Mo khoa bom sau khi da kiem tra (nut bam hoac lenh UNLOCK)
  void unlock();

  bool pumpOn() const { return pumpOn_; }
  bool watering() const { return watering_; }
  bool locked() const { return locked_; }
  int pulseCount() const { return pulseCount_; }
  PumpState state() const { return state_; }

 private:
  IrrigationConfig c_;
  bool watering_ = false;
  bool pumpOn_ = false;
  bool locked_ = false;
  bool resting_ = false;
  bool everPumped_ = false;
  unsigned long pumpStart_ = 0;
  unsigned long pumpStop_ = 0;
  unsigned long drySince_ = 0;
  unsigned long restStart_ = 0;
  int pulseCount_ = 0;
  int cycleStartMoisture_ = 0;
  int incompleteCycles_ = 0;
  PumpState state_ = PumpState::Off;
};

// Chuoi 3 ky tu cho LCD (OFF, ON, CHO, NGI, LOI, HET, ---) va ten cho MQTT / log
const char *pumpStateCode(PumpState state);
const char *pumpStateName(PumpState state);

// ===================== Phun suong theo gio va ket qua AI =====================

// Che do phun, may tinh chay AI gui xuong bang lenh "MIST:<0-3>"
enum MistMode {
  MIST_KEEP = 0,    // AI khong xem duoc la (anh toi, khong thay la): giu che do dang chay
  MIST_NORMAL = 1,  // La khoe, benh virus, hoac chua co ket qua AI
  MIST_STOP = 2,    // La benh nam / vi khuan, hoac nghi benh: khong phun (la uot lam benh lan nhanh)
  MIST_BOOST = 3,   // Chac chan nhen do (so am): phun day hon
};

struct MistConfig {
  long onSec;           // Moi lan phun chung nay giay...
  long everySec;        // ...chung nay giay 1 lan (tinh theo dong ho nen luon dung :00, :30)
  long boostEverySec;   // ...hoac chung nay giay 1 lan khi MIST_BOOST
  long startSec;        // Chi phun tu gio nay (giay trong ngay)...
  long endSec;          // ...den truoc gio nay: la kip kho truoc toi
  float maxAirHumidity; // Khong khi da am tu muc nay (%) thi khong phun
};

enum class MistReason {
  Scheduled,    // Dang phun dung lich
  NoClock,      // Chua biet gio: khong phun, tranh phun nham ban dem
  Disease,      // AI bao la benh nam / vi khuan
  OutsideHours, // Ngoai khung gio phun
  Waiting,      // Trong khung gio nhung chua toi nhip phun
  NoWater,      // Be het nuoc
  AirHumid,     // Khong khi da du am
};

struct MistDecision {
  bool on;
  MistReason reason;
};

// secondOfDay: giay trong ngay (0-86399), -1 neu chua biet gio
MistDecision decideMist(const MistConfig &config, long secondOfDay, int mode, float airHumidity,
                        bool airHumidityValid, bool tankEmpty);
const char *mistReasonName(MistReason reason);

// ===================== Quy doi so do cam bien =====================

// Doi so doc ADC ra %: raw0 -> 0%, raw100 -> 100%, gioi han 0-100 (raw0 lon hon raw100 cung duoc)
int percentFromRaw(int raw, int raw0, int raw100);

// Muc nuoc be (%) tu khoang cach cam bien sieu am toi mat nuoc: emptyCm -> 0%, fullCm -> 100%
int tankLevelPercent(float distanceCm, float emptyCm, float fullCm);

// Do sang (lux) tu module quang tro: LDR noi tu AO xuong GND, dien tro seriesOhm noi tu VCC xuong AO.
// rl10KOhm: dien tro LDR o 10 lux, gamma: do doc log(R)/log(lux). Cong thuc theo tai lieu Wokwi.
float luxFromAdc(int raw, int adcMax, float vcc, float seriesOhm, float rl10KOhm, float gamma);

}  // namespace farm
