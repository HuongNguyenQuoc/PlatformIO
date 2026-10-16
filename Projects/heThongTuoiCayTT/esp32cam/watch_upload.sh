#!/usr/bin/env bash
# Tu dong build + nap code cho ESP32-CAM qua WiFi moi khi luu file trong src/, include/ hoac platformio.ini.
# Chay: ./esp32cam/watch_upload.sh   (Ctrl+C de dung)
# ESP phai dang chay firmware co OTA va online (curl http://esp32cam.local/status tra loi duoc).

cd "$(dirname "$0")" || exit 1
PIO="${PIO:-$HOME/.platformio/penv/bin/pio}"

# Ten + thoi gian sua cua tung file code; doi bat ky file nao thi chuoi nay doi
fingerprint() {
  find src include platformio.ini secrets.ini -type f -exec stat -c '%n %Y' {} + 2>/dev/null | sort | md5sum
}

last=$(fingerprint)
echo "Dang theo doi code ESP32-CAM. Luu file la tu nap qua WiFi (Ctrl+C de dung)."
while sleep 2; do
  [ "$(fingerprint)" = "$last" ] && continue
  sleep 1  # Cho luu xong neu sua nhieu file cung luc
  last=$(fingerprint)
  echo "== $(date +%T) Code thay doi, dang build + nap qua WiFi..."
  if "$PIO" run -e ota -t upload; then
    echo "== $(date +%T) Nap xong, ESP dang khoi dong lai"
  else
    echo "== $(date +%T) LOI: code build loi, hoac ESP khong online / sai mat khau OTA (xem log o tren)"
  fi
done
