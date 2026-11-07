import re
import sys
from pathlib import Path

# Путь можно передать аргументом, иначе берём version.h рядом со скриптом.
default_path = Path(__file__).resolve().parent / "include" / "version.h"
path = Path(sys.argv[1]) if len(sys.argv) > 1 else default_path

if not path.is_file():
    sys.exit(f"version file not found: {path}")

with path.open("r", encoding="utf-8", newline="") as f:
    src = f.read()

m = re.search(r"(#define\s+FW_VERSION_BUILD\s+)(\d+)", src)
if not m:
    sys.exit(f"FW_VERSION_BUILD not found in {path}")

build = int(m.group(2)) + 1

# newline="" сохраняет исходные CRLF/LF, чтобы git не показывал весь файл изменённым.
with path.open("w", encoding="utf-8", newline="") as f:
    f.write(src[:m.start(2)] + str(build) + src[m.end(2):])

print(f"firmware build -> {build}")
