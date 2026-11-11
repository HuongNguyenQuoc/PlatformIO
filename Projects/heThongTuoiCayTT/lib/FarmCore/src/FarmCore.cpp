#include "FarmCore.h"

#include <math.h>

namespace farm {

// ===================== Tuoi =====================

Irrigation::Irrigation(const IrrigationConfig &config) : c_(config) {}

bool Irrigation::update(unsigned long now, int moisture, bool sensorFault, bool tankEmpty) {
  if (sensorFault || moisture >= c_.moistureLow) {
    drySince_ = now;  // Chua kho thi dem lai tu dau
  }
  bool restDone = !resting_ || now - restStart_ >= c_.restMs;

  if (sensorFault || locked_ || tankEmpty) {
    // Cam bien loi thi khong tin so do, bom bi khoa thi cho nguoi kiem tra, het nuoc thi bom chay khan: tat bom
    watering_ = false;
  } else if (watering_ && moisture >= c_.moistureHigh) {
    watering_ = false;  // Du am, ket thuc dot tuoi
    incompleteCycles_ = 0;
  } else if (!watering_ && restDone && now - drySince_ >= c_.dryConfirmMs) {
    watering_ = true;  // Bat dau dot tuoi moi
    resting_ = false;
    pulseCount_ = 0;
    cycleStartMoisture_ = moisture;
  }

  if (pumpOn_) {
    // Bom du pumpOnMs hoac khong can tuoi nua thi tat
    if (!watering_ || now - pumpStart_ >= c_.pumpOnMs) {
      pumpOn_ = false;
      pumpStop_ = now;
    }
  } else if (watering_ && (!everPumped_ || now - pumpStop_ >= c_.soakMs)) {
    if (pulseCount_ < c_.maxPulses) {
      // Da cho nuoc ngam du lau ma van kho thi bom them 1 lan
      pumpOn_ = true;
      everPumped_ = true;
      pumpStart_ = now;
      pulseCount_++;
    } else {
      // Bom du maxPulses lan, ngam xong ma van chua du am
      watering_ = false;
      incompleteCycles_++;
      if (moisture - cycleStartMoisture_ < c_.minRise || incompleteCycles_ >= c_.maxIncompleteCycles) {
        locked_ = true;
      } else {
        resting_ = true;
        restStart_ = now;
      }
    }
  }

  if (locked_) {
    state_ = PumpState::Locked;
  } else if (sensorFault) {
    state_ = PumpState::SensorFault;
  } else if (tankEmpty) {
    state_ = PumpState::NoWater;
  } else if (pumpOn_) {
    state_ = PumpState::On;
  } else if (watering_) {
    state_ = PumpState::Soaking;
  } else if (resting_ && !restDone) {
    state_ = PumpState::Resting;
  } else {
    state_ = PumpState::Off;
  }
  return pumpOn_;
}

void Irrigation::unlock() {
  locked_ = false;
  resting_ = false;
  incompleteCycles_ = 0;
}

const char *pumpStateCode(PumpState state) {
  switch (state) {
    case PumpState::On: return "ON ";
    case PumpState::Soaking: return "CHO";
    case PumpState::Resting: return "NGI";
    case PumpState::Locked: return "LOI";
    case PumpState::NoWater: return "HET";
    case PumpState::SensorFault: return "---";
    default: return "OFF";
  }
}

const char *pumpStateName(PumpState state) {
  switch (state) {
    case PumpState::On: return "on";
    case PumpState::Soaking: return "soaking";
    case PumpState::Resting: return "resting";
    case PumpState::Locked: return "locked";
    case PumpState::NoWater: return "no_water";
    case PumpState::SensorFault: return "sensor_fault";
    default: return "off";
  }
}

// ===================== Phun suong =====================

MistDecision decideMist(const MistConfig &c, long sec, int mode, float airHumidity, bool airHumidityValid,
                        bool tankEmpty) {
  if (sec < 0) {
    return {false, MistReason::NoClock};
  }
  if (mode == MIST_STOP) {
    return {false, MistReason::Disease};
  }
  if (sec < c.startSec || sec >= c.endSec) {
    return {false, MistReason::OutsideHours};
  }
  long every = mode == MIST_BOOST ? c.boostEverySec : c.everySec;
  if (sec % every >= c.onSec) {
    return {false, MistReason::Waiting};
  }
  if (tankEmpty) {
    return {false, MistReason::NoWater};
  }
  if (airHumidityValid && airHumidity >= c.maxAirHumidity) {
    return {false, MistReason::AirHumid};
  }
  return {true, MistReason::Scheduled};
}

const char *mistReasonName(MistReason reason) {
  switch (reason) {
    case MistReason::Scheduled: return "scheduled";
    case MistReason::NoClock: return "no_clock";
    case MistReason::Disease: return "disease";
    case MistReason::OutsideHours: return "outside_hours";
    case MistReason::Waiting: return "waiting";
    case MistReason::NoWater: return "no_water";
    default: return "air_humid";
  }
}

// ===================== Cam bien =====================

int percentFromRaw(int raw, int raw0, int raw100) {
  if (raw0 == raw100) {
    return 0;
  }
  long p = (long)(raw - raw0) * 100 / (raw100 - raw0);
  return p < 0 ? 0 : p > 100 ? 100 : (int)p;
}

int tankLevelPercent(float distanceCm, float emptyCm, float fullCm) {
  float p = (emptyCm - distanceCm) * 100.0f / (emptyCm - fullCm);
  return p < 0 ? 0 : p > 100 ? 100 : (int)(p + 0.5f);
}

float luxFromAdc(int raw, int adcMax, float vcc, float seriesOhm, float rl10KOhm, float gamma) {
  float voltage = (float)raw / adcMax * vcc;
  if (voltage >= vcc * 0.999f) {
    return 0;  // LDR gan nhu ho mach: toi han
  }
  if (voltage <= 0) {
    return 100000;  // LDR gan nhu chap: rat sang
  }
  float resistance = seriesOhm * voltage / (vcc - voltage);
  return powf(rl10KOhm * 1e3f * powf(10, gamma) / resistance, 1 / gamma);
}

}  // namespace farm
