#!/usr/bin/env bash
#
# Прошивка блока DED CORE V4 по проводу: приложение и вшитый интерфейс.
#
# Зачем отдельный скрипт, если есть `pio run -t upload`, `-t uploadfs`:
# esptool падает на сжатой записи образа файловой системы (1088 КБ) с ошибкой
#   A fatal error occurred: Failed to leave compressed flash mode (result was C900: Too much data)
# Флаг, который это лечит, — --no-compress — относится к подкоманде write_flash, а
# PlatformIO через upload_flags подставляет флаги до подкоманды (esptool отвечает
# «unrecognized arguments: --no-compress»). Скорость тут ни при чём: проверил 921600,
# 460800 и 230400 — падает одинаково. Приложение пишется обычным путём PlatformIO.
#
# Использование:
#   ./flash-device.sh -p /dev/cu.usbserial-0001 [-e esp32dev] [--fs-only]
#
# Перед запуском блок должен быть подключён по USB. Для нового блока сначала полезно
# стереть флеш: ./flash-device.sh -p <порт> --erase

set -euo pipefail

PORT=""
ENV_NAME="esp32dev"
FS_ONLY=0
ERASE=0

while [ $# -gt 0 ]; do
  case "$1" in
    -p|--port) PORT="$2"; shift 2 ;;
    -e|--env) ENV_NAME="$2"; shift 2 ;;
    --fs-only) FS_ONLY=1; shift ;;
    --erase) ERASE=1; shift ;;
    -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
    *) echo "Неизвестный аргумент: $1" >&2; exit 2 ;;
  esac
done

if [ -z "$PORT" ]; then
  echo "Укажите порт: ./flash-device.sh -p /dev/cu.usbserial-0001" >&2
  echo "Список портов: pio device list" >&2
  exit 2
fi

cd "$(dirname "$0")"

esptool() {
  pio pkg exec -p tool-esptoolpy -- esptool.py --port "$PORT" "$@"
}

# Смещение раздела файловой системы берём из таблицы того же окружения, что прошиваем:
# в новой разметке это 0x2F0000, в заводской — 0x290000, и ошибиться тут нельзя.
partition_csv() {
  case "$ENV_NAME" in
    esp32dev) echo "partitions_ded_core.csv" ;;
    esp32dev_legacy) echo "partitions_legacy.csv" ;;
    *) echo "Неизвестное окружение: $ENV_NAME" >&2; exit 2 ;;
  esac
}

FS_OFFSET="$(awk -F, '/^spiffs/ {gsub(/ /, "", $4); print $4}' "$(partition_csv)")"

if [ -z "$FS_OFFSET" ]; then
  echo "Не нашёл раздел spiffs в $(partition_csv)" >&2
  exit 1
fi

echo "==> окружение: $ENV_NAME, порт: $PORT, раздел ФС: $FS_OFFSET"

if [ "$ERASE" = "1" ]; then
  echo "==> стираю флеш"
  esptool erase_flash
fi

if [ "$FS_ONLY" != "1" ]; then
  echo "==> собираю прошивку"
  pio run -e "$ENV_NAME"

  echo "==> записываю прошивку"
  pio run -e "$ENV_NAME" -t upload --upload-port "$PORT"
fi

echo "==> собираю образ файловой системы"
pio run -e "$ENV_NAME" -t buildfs

case "$ENV_NAME" in
  esp32dev) FS_IMAGE=".pio/build/$ENV_NAME/littlefs.bin" ;;
  *) FS_IMAGE=".pio/build/$ENV_NAME/spiffs.bin" ;;
esac

if [ ! -f "$FS_IMAGE" ]; then
  echo "Нет образа файловой системы: $FS_IMAGE" >&2
  exit 1
fi

echo "==> записываю файловую систему ($(stat -f%z "$FS_IMAGE" 2>/dev/null || stat -c%s "$FS_IMAGE") байт) без сжатия"
esptool write_flash --no-compress "$FS_OFFSET" "$FS_IMAGE"

echo "==> готово. Лог блока: pio device monitor --port $PORT (или скрипт провижининга)"
