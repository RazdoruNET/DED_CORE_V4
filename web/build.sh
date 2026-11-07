#!/usr/bin/env bash
#
# Собирает встроенный интерфейс и раскладывает его в data/ — оттуда PlatformIO
# собирает образ LittleFS. Старый бандл при этом полностью заменяется.
#
# Использование:
#   ./build.sh          — установить зависимости (если нужно) и собрать
#   ./build.sh --clean  — переустановить node_modules перед сборкой

set -euo pipefail

cd "$(dirname "$0")"

if [ "${1:-}" = "--clean" ]; then
  rm -rf node_modules
fi

if [ ! -d node_modules ]; then
  if [ -f package-lock.json ]; then
    npm ci --no-audit --no-fund
  else
    npm install --no-audit --no-fund
  fi
fi

npm run build

rm -rf ../data
mkdir -p ../data
cp -R dist/ded-device/. ../data/
rm -f ../data/prerendered-routes.json

echo "==> готово: $(du -sh ../data | cut -f1) в DED_CORE_V4-feature-ota-server-update/data"
echo "==> залить только файловую систему: ./flash-device.sh -p /dev/cu.usbserial-0001 --fs-only"
