#!/usr/bin/env python3
"""
Сжимает веб-страницу в gzip для встраивания в прошивку.

Вызывается автоматически из components/server/CMakeLists.txt при каждой сборке,
если index.html изменился. Вручную:  python gzip_page.py <вход.html> <выход.gz>

mtime=0 и пустое имя файла в заголовке — чтобы одинаковый index.html всегда давал
одинаковый .gz и сборка не пересобиралась без причины.
"""
import gzip
import sys
from pathlib import Path


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: gzip_page.py <input.html> <output.gz>")

    src = Path(sys.argv[1])
    dst = Path(sys.argv[2])
    data = src.read_bytes()

    dst.parent.mkdir(parents=True, exist_ok=True)
    with open(dst, "wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", compresslevel=9, fileobj=raw, mtime=0) as gz:
            gz.write(data)

    size = dst.stat().st_size
    print(f"[gzip_page] {src.name}: {len(data)} -> {size} bytes ({100 - size * 100 // len(data)}% smaller)")


if __name__ == "__main__":
    main()
