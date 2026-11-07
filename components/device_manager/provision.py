#!/usr/bin/env python3
"""
Записывает метаданные устройства (имя проекта + серийник) в раздел 'meta'.

Использование:
  python components/device_manager/provision.py "Project, SN-00001" --port COM3
  python components/device_manager/provision.py "Led, SN-00042" --port COM3

Формат аргумента: "<project>, <serial>" — через запятую с пробелом.
Пробелы по краям автоматически срезаются.

Что делает:
  1. Парсит "project, serial"
  2. Собирает бинарь: magic(4) + project(64) + serial(32) + crc32(4) = 104 байта
  3. Записывает его в раздел 'meta' (поиск по метке, адрес не важен)
"""
import struct
import zlib
import sys
import os
import subprocess
import tempfile


META_MAGIC = 0xDEADBEEF
META_PARTITION = "meta"
PROJECT_SIZE = 64
SERIAL_SIZE = 32


def parse_arg(arg: str) -> tuple[str, str]:
    if "," not in arg:
        sys.exit("аргумент должен быть в формате \"<project>, <serial>\" (с запятой)")
    project, serial = arg.split(",", 1)
    project = project.strip()
    serial = serial.strip()
    if not project or not serial:
        sys.exit("project и serial не должны быть пустыми")
    if len(project) > PROJECT_SIZE - 1:
        sys.exit(f"project длиннее {PROJECT_SIZE - 1} символов: {len(project)}")
    if len(serial) > SERIAL_SIZE - 1:
        sys.exit(f"serial длиннее {SERIAL_SIZE - 1} символов: {len(serial)}")
    return project, serial


def make_meta_bin(project: str, serial: str) -> bytes:
    body = struct.pack("<I", META_MAGIC) \
         + project.encode("ascii").ljust(PROJECT_SIZE, b"\x00") \
         + serial.encode("ascii").ljust(SERIAL_SIZE, b"\x00")
    crc = zlib.crc32(body) & 0xFFFFFFFF
    return body + struct.pack("<I", crc)


def find_parttool() -> str:
    idf_path = os.environ.get("IDF_PATH")
    if not idf_path:
        sys.exit("IDF_PATH не выставлена. Запусти export.bat из esp-idf")
    parttool = os.path.join(idf_path, "components", "partition_table", "parttool.py")
    if not os.path.exists(parttool):
        sys.exit(f"parttool.py не найден: {parttool}")
    return parttool


def main():
    if len(sys.argv) < 2:
        sys.exit("Usage: python components/device_manager/provision.py \"<project>, <serial>\" --port PORT")

    project, serial = parse_arg(sys.argv[1])

    port = None
    if "--port" in sys.argv:
        idx = sys.argv.index("--port")
        port = sys.argv[idx + 1]
    if not port:
        sys.exit("не указан --port (например --port COM3)")

    bin_data = make_meta_bin(project, serial)
    tmp = tempfile.NamedTemporaryFile(delete=False, suffix=".bin")
    tmp.write(bin_data)
    tmp.close()
    bin_path = tmp.name

    parttool = find_parttool()
    cmd = [
        sys.executable, parttool,
        "--port", port,
        "write_partition",
        "--partition-name", META_PARTITION,
        "--input", bin_path,
    ]
    print(f"[provision] project='{project}', serial='{serial}', {len(bin_data)} bytes -> {META_PARTITION}")
    print("[provision] " + " ".join(cmd))
    rc = subprocess.call(cmd)
    os.unlink(bin_path)
    if rc != 0:
        sys.exit(f"parttool завершился с кодом {rc}")
    print(f"[provision] OK: {project}, {serial}")


if __name__ == "__main__":
    main()
