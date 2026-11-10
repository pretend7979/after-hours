#!/usr/bin/env python3
"""
Packages After Hours Arcade Manager into After-Hours-<version>-windows-x64.zip.

usage: package_afterhours.py [version] [output folder]

Takes build/src/afterhours (afterhours.exe and its logos), the DLLs it imports and the Qt
plugins it loads from the MSYS2 clang64 toolchain it was built with, WebView2Loader.dll from
externals/webview2, and the licence of every package a shipped DLL comes from. Emulators are not
included: After Hours downloads them from their own GitHub releases.
"""

import hashlib
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build" / "src" / "afterhours"
MSYS = Path(r"C:\msys64")
TOOLCHAIN = MSYS / "clang64"
OBJDUMP = TOOLCHAIN / "bin" / "llvm-objdump.exe"
PACMAN = MSYS / "usr" / "bin" / "pacman.exe"
WEBVIEW2 = ROOT / "externals" / "webview2" / "pkg" / "build" / "native" / "x64" / "WebView2Loader.dll"
# Qt loads these at run time (no import entry): the window, images (SVG logos), HTTPS for IGDB
# and downloads, the style, network status.
PLUGIN_DIRS = ["platforms", "imageformats", "iconengines", "styles", "tls", "networkinformation",
               "generic"]
PACKAGE_PREFIX = "mingw-w64-clang-x86_64-"


def imports(binary: Path) -> list[str]:
    out = subprocess.run([str(OBJDUMP), "-p", str(binary)], capture_output=True, text=True).stdout
    return [line.split(":", 1)[1].strip() for line in out.splitlines() if "DLL Name:" in line]


def needed_dlls(start: list[Path]) -> list[Path]:
    """The toolchain DLLs the binaries import, directly or through each other."""
    available = {p.name.lower(): p for p in (TOOLCHAIN / "bin").glob("*.dll")}
    needed: dict[str, Path] = {}
    queue = list(start)
    while queue:
        for name in imports(queue.pop()):
            key = name.lower()
            if key in available and key not in needed:
                needed[key] = available[key]
                queue.append(available[key])
    return list(needed.values())


def packages_of(files: list[Path]) -> set[str]:
    owners: set[str] = set()
    for file in files:
        msys_path = "/" + file.relative_to(MSYS).as_posix()
        result = subprocess.run([str(PACMAN), "-Qqo", msys_path], capture_output=True, text=True)
        for line in result.stdout.split():
            owners.add(line.removeprefix(PACKAGE_PREFIX))
    return owners


def package(version: str, outputs: Path) -> bool:
    exe = BUILD / "afterhours.exe"
    if not exe.exists():
        print(f"Error: {exe} not found; build first (cmake --build build)", file=sys.stderr)
        return False
    staging = outputs / f"afterhours-pkg-{version}"
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)

    shutil.copy2(exe, staging / "afterhours.exe")
    shutil.copytree(BUILD / "logos", staging / "logos")
    plugins: list[Path] = []
    for folder in PLUGIN_DIRS:
        source = TOOLCHAIN / "share" / "qt6" / "plugins" / folder
        if source.is_dir():
            shutil.copytree(source, staging / folder,
                            ignore=shutil.ignore_patterns("*.debug", "*.pdb"))
            plugins += list(source.glob("*.dll"))
    dlls = needed_dlls([exe] + plugins)
    for dll in dlls:
        shutil.copy2(dll, staging / dll.name)
    shutil.copy2(WEBVIEW2, staging / WEBVIEW2.name)

    # The licences: After Hours' (GPL-3.0), WebView2's, and each toolchain package's.
    shutil.copy2(ROOT / "LICENSE.txt", staging / "LICENSE.txt")
    licences = staging / "third-party-licenses"
    licences.mkdir()
    for name in sorted(packages_of(dlls + plugins)):
        source = TOOLCHAIN / "share" / "licenses" / name
        if source.is_dir():
            shutil.copytree(source, licences / name)
    webview_licence = ROOT / "externals" / "webview2" / "pkg" / "LICENSE.txt"
    if webview_licence.exists():
        (licences / "webview2").mkdir()
        shutil.copy2(webview_licence, licences / "webview2" / "LICENSE.txt")

    outputs.mkdir(parents=True, exist_ok=True)
    archive = outputs / f"After-Hours-{version}-windows-x64.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for path in sorted(staging.rglob("*")):
            if path.is_file():
                z.write(path, path.relative_to(staging).as_posix())
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    (outputs / f"{archive.name}.sha256.txt").write_text(f"{digest}  {archive.name}\n",
                                                       encoding="utf-8")
    shutil.rmtree(staging)
    print(f"Packaged {archive} ({archive.stat().st_size:,} bytes, {len(dlls)} DLLs)")
    print(f"SHA256: {digest}")
    return True


if __name__ == "__main__":
    version = sys.argv[1] if len(sys.argv) > 1 else "0.1.0"
    outputs = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "out"
    sys.exit(0 if package(version, outputs) else 1)
