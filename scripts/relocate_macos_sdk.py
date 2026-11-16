"""Refresh SDK paths in generated dependency metadata after an Xcode update.

Only a restored build prefix is changed, not dependency sources or libraries.
The dependency archives retain their original macOS deployment target.
"""

import argparse
from pathlib import Path
import re
import subprocess


SDK_PATH = re.compile(r"/Applications/[^\n\"';]+?/SDKs/MacOSX[0-9.]+\.sdk")


def relocate(prefix: Path, sdk: Path) -> list[str]:
    if not prefix.is_dir() or not sdk.is_dir():
        raise ValueError("Both the dependency prefix and selected SDK must exist")
    changed = []
    for path in prefix.rglob("*"):
        if not path.is_file() or path.is_symlink():
            continue
        relative = path.relative_to(prefix)
        if path.suffix not in {".cmake", ".pc", ".la"} and path.name != "wx-config" and "lib/wx/config/" not in relative.as_posix():
            continue
        text = path.read_text()
        updated = SDK_PATH.sub(lambda match: str(sdk), text)
        if updated != text:
            path.write_text(updated)
            changed.append(str(relative))
    return changed


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prefix", type=Path)
    parser.add_argument("--sdk-path", type=Path)
    args = parser.parse_args()
    sdk = args.sdk_path or Path(subprocess.check_output(["xcrun", "--show-sdk-path"], text=True).strip())
    for name in relocate(args.prefix, sdk):
        print(f"Updated generated SDK reference: {name}")
