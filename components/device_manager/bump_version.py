#!/usr/bin/env python3
"""
Increments the build number (4th component) in version.h.
Format: inline const char *firmware_version() { return "MAJOR.MINOR.PATCH.BUILD"; }

Run standalone:  python bump_version.py [path/to/version.h]
Run via CMake:   automatically before each build (через device_manager/CMakeLists.txt)

По умолчанию ищет version.h в include/ рядом со скриптом (т.е. в device_manager/include/).
"""
import re
import sys
from pathlib import Path

VERSION_RE = re.compile(
    r'(inline\s+const\s+char\s*\*\s*firmware_version\s*\(\s*\)\s*\{\s*return\s*")'
    r'(\d+)\.(\d+)\.(\d+)\.(\d+)'
    r'("\s*;\s*\}\s*)$'
)


def bump(path: Path) -> str:
    if not path.exists():
        sys.exit(f"version.h not found: {path}")

    src = path.read_text(encoding="utf-8")
    m = VERSION_RE.search(src)
    if not m:
        sys.exit(f"could not parse version in {path} (expected: inline const char *firmware_version() {{ return \"X.Y.Z.N\"; }})")

    major, minor, patch, build = (int(m.group(i)) for i in (2, 3, 4, 5))
    build += 1
    new_ver = f"{major}.{minor}.{patch}.{build}"
    new_src = VERSION_RE.sub(
        lambda mo: f"{mo.group(1)}{major}.{minor}.{patch}.{build}{mo.group(6)}",
        src,
        count=1,
    )
    path.write_text(new_src, encoding="utf-8")
    return new_ver


def main():
    if len(sys.argv) >= 2:
        target = Path(sys.argv[1])
    else:
        target = Path(__file__).resolve().parent / "include" / "version.h"
    new_ver = bump(target)
    print(f"[bump_version] {target} -> {new_ver}")


if __name__ == "__main__":
    main()
