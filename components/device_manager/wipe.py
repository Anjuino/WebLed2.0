#!/usr/bin/env python3
"""
Стирает указанный раздел (по умолчанию storage) через parttool.py.
Используй, если в storage лежит битый скрипт и контроллер в вечном ребуте.

Использование:
  python components/device_manager/wipe.py storage --port COM3
  python components/device_manager/wipe.py meta --port COM3
  python components/device_manager/wipe.py --port COM3           # storage по дефолту

Ищет раздел по метке, адрес не важен.
"""
import sys
import os
import subprocess


DEFAULT_PARTITION = "storage"


def find_parttool() -> str:
    idf_path = os.environ.get("IDF_PATH")
    if not idf_path:
        sys.exit("IDF_PATH не выставлена. Запусти export.bat из esp-idf")
    parttool = os.path.join(idf_path, "components", "partition_table", "parttool.py")
    if not os.path.exists(parttool):
        sys.exit(f"parttool.py не найден: {parttool}")
    return parttool


def main():
    args = sys.argv[1:]

    partition = DEFAULT_PARTITION
    if args and not args[0].startswith("--"):
        partition = args.pop(0)

    port = None
    if "--port" in args:
        idx = args.index("--port")
        port = args[idx + 1]
    if not port:
        sys.exit("не указан --port (например --port COM3)")

    parttool = find_parttool()
    cmd = [
        sys.executable, parttool,
        "--port", port,
        "erase_partition",
        "--partition-name", partition,
    ]
    print(f"[wipe] partition='{partition}'")
    print("[wipe] " + " ".join(cmd))
    rc = subprocess.call(cmd)
    if rc != 0:
        sys.exit(f"parttool завершился с кодом {rc}")
    print(f"[wipe] OK: {partition} стёрт")


if __name__ == "__main__":
    main()
