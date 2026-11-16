"""Сжатые копии вшитого фронта для образа файловой системы.

Зачем: несжатый JS фронта — 273 КБ, и через TLS он занимает сессию на три секунды.
В gzip это ~80 КБ, то есть страница грузится втрое быстрее, а сессия (а вместе с ней и
память рядом с Bluetooth) занята втрое меньше времени.

Как: PlatformIO берёт содержимое каталога из [platformio] data_dir. Скрипт читает
исходный data/ и складывает в .pio/frontend рядом с каждым текстовым файлом его
сжатую копию <имя>.gz. Прошивка отдаёт .gz тем, кто прислал Accept-Encoding: gzip,
и обычный файл — всем остальным (curl, диагностика).

Запускается на каждую сборку: 300 КБ сжимаются за десятки миллисекунд.
"""

Import("env")

import gzip
import os
import shutil

COMPRESSIBLE = {".html", ".js", ".css", ".svg", ".json", ".txt", ".map"}


def pack(source_dir, target_dir):
    if os.path.isdir(target_dir):
        shutil.rmtree(target_dir)
    os.makedirs(target_dir, exist_ok=True)

    raw_total = 0
    packed_total = 0
    compressed = 0

    for root, _, files in os.walk(source_dir):
        relative = os.path.relpath(root, source_dir)
        destination = os.path.join(target_dir, relative) if relative != "." else target_dir
        os.makedirs(destination, exist_ok=True)

        for name in files:
            source = os.path.join(root, name)
            copy = os.path.join(destination, name)
            shutil.copy2(source, copy)

            if os.path.splitext(name)[1].lower() not in COMPRESSIBLE:
                continue

            size = os.path.getsize(source)
            data = open(source, "rb").read()

            # mtime=0: одинаковый вход даёт одинаковый образ файловой системы.
            with gzip.GzipFile(copy + ".gz", "wb", compresslevel=9, mtime=0) as packed:
                packed.write(data)

            raw_total += size
            packed_total += os.path.getsize(copy + ".gz")
            compressed += 1

    if compressed:
        print(
            "frontend: %d файлов сжато, %d КБ -> %d КБ"
            % (compressed, raw_total // 1024, packed_total // 1024)
        )


project = env.subst("$PROJECT_DIR")
source = os.path.join(project, "data")
target = os.path.join(project, ".pio", "frontend")

if os.path.isdir(source):
    pack(source, target)
