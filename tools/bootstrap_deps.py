"""bootstrap_deps.py - Automated dependency fetcher and builder for Dominion 2026."""

import os
import subprocess
import sys
import tarfile
import urllib.request
from pathlib import Path

# LZ4 constants
LZ4_H_URL = "https://raw.githubusercontent.com/lz4/lz4/v1.10.0/lib/lz4.h"
LZ4_C_URL = "https://raw.githubusercontent.com/lz4/lz4/v1.10.0/lib/lz4.c"

# SDL3 constants
SDL_VERSION = "3.2.10"
SDL_URLS = [
    f"https://github.com/libsdl-org/SDL/releases/download/release-{SDL_VERSION}/SDL3-{SDL_VERSION}.tar.gz",
    f"https://github.com/libsdl-org/SDL/archive/refs/tags/release-{SDL_VERSION}.tar.gz",
]


def is_valid_lz4_file(path: Path) -> bool:
    if not path.is_file():
        return False
    if path.stat().st_size <= 20000:
        return False
    try:
        content = path.read_text(encoding="utf-8", errors="ignore")
        return "LZ4_compress_default" in content and "LZ4_decompress_safe" in content
    except Exception:
        return False


def bootstrap_lz4(root: Path) -> bool:
    lz4_dir = root / "external" / "lz4"
    lz4_dir.mkdir(parents=True, exist_ok=True)
    lz4_h = lz4_dir / "lz4.h"
    lz4_c = lz4_dir / "lz4.c"

    if not is_valid_lz4_file(lz4_h):
        print("[BOOTSTRAP] Downloading lz4.h...")
        urllib.request.urlretrieve(LZ4_H_URL, lz4_h)

    if not is_valid_lz4_file(lz4_c):
        print("[BOOTSTRAP] Downloading lz4.c...")
        urllib.request.urlretrieve(LZ4_C_URL, lz4_c)

    if not is_valid_lz4_file(lz4_h) or not is_valid_lz4_file(lz4_c):
        print("[BOOTSTRAP ERROR] LZ4 validation failed.", file=sys.stderr)
        return False

    print("[BOOTSTRAP] LZ4 v1.10.0 ready.")
    return True


def bootstrap_sdl3(root: Path) -> bool:
    sdl_dir = root / "third_party" / "SDL3"
    cmake_file = sdl_dir / "CMakeLists.txt"
    header_file = sdl_dir / "include" / "SDL3" / "SDL.h"

    if cmake_file.is_file() and header_file.is_file():
        print(f"[BOOTSTRAP] SDL3 {SDL_VERSION} already present.")
        return True

    sdl_dir.mkdir(parents=True, exist_ok=True)
    tar_path = root / "third_party" / f"SDL3-{SDL_VERSION}.tar.gz"

    downloaded = False
    for url in SDL_URLS:
        print(f"[BOOTSTRAP] Trying SDL3 download: {url}...")
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
            with urllib.request.urlopen(req) as resp, open(tar_path, "wb") as out_f:
                if resp.status == 200:
                    out_f.write(resp.read())
                    downloaded = True
                    break
        except Exception as e:
            print(f"[BOOTSTRAP] Download failed: {e}")

    if not downloaded or not tar_path.is_file() or tar_path.stat().st_size <= 3000000:
        print("[BOOTSTRAP ERROR] Failed to download valid SDL3 archive (>3MB).", file=sys.stderr)
        return False

    print(f"[BOOTSTRAP] Extracting SDL3 ({tar_path.stat().st_size} bytes)...")
    try:
        with tarfile.open(tar_path, "r:gz") as tar:
            for member in tar.getmembers():
                parts = Path(member.name).parts
                if len(parts) <= 1:
                    continue
                member.name = str(Path(*parts[1:]))
                tar.extract(member, path=sdl_dir, filter="data")
    except Exception as e:
        print(f"[BOOTSTRAP ERROR] Extraction failed: {e}", file=sys.stderr)
        return False
    finally:
        if tar_path.exists():
            tar_path.unlink()

    if not cmake_file.is_file() or not header_file.is_file():
        print("[BOOTSTRAP ERROR] SDL3 sanity check failed: missing CMakeLists.txt or SDL.h", file=sys.stderr)
        return False

    print(f"[BOOTSTRAP] SDL3 {SDL_VERSION} ready.")
    return True


def bootstrap_glad(root: Path) -> bool:
    glad_dir = root / "third_party" / "glad"
    gl_h = glad_dir / "include" / "glad" / "gl.h"
    khr_h = glad_dir / "include" / "KHR" / "khrplatform.h"
    gl_c = glad_dir / "src" / "gl.c"

    if gl_h.is_file() and khr_h.is_file() and gl_c.is_file():
        if gl_h.stat().st_size > 100000 and gl_c.stat().st_size > 50000:
            content = gl_h.read_text(encoding="utf-8", errors="ignore")
            if "GLAD_GL_VERSION_4_6" in content:
                print("[BOOTSTRAP] glad2 core 4.6 already present.")
                return True

    pydeps_dir = root / "tools" / "_pydeps"
    pydeps_dir.mkdir(parents=True, exist_ok=True)

    print("[BOOTSTRAP] Installing glad2 into tools/_pydeps...")
    res = subprocess.run(
        [sys.executable, "-m", "pip", "install", "--target", str(pydeps_dir), "glad2"],
        capture_output=True,
        text=True,
    )
    if res.returncode != 0:
        print(f"[BOOTSTRAP ERROR] pip install glad2 failed: {res.stderr}", file=sys.stderr)
        return False

    print("[BOOTSTRAP] Generating glad2 C sources...")
    env = os.environ.copy()
    env["PYTHONPATH"] = str(pydeps_dir) + os.pathsep + env.get("PYTHONPATH", "")

    cmd = [
        sys.executable,
        "-m",
        "glad",
        "--out-path",
        str(glad_dir),
        "--api",
        "gl:core=4.6",
        "--extensions",
        "GL_EXT_texture_compression_s3tc,GL_EXT_texture_sRGB,GL_NVX_gpu_memory_info,GL_ATI_meminfo",
        "c",
    ]
    res = subprocess.run(cmd, env=env, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"[BOOTSTRAP ERROR] glad generation failed: {res.stderr}", file=sys.stderr)
        return False

    if not gl_h.is_file() or gl_h.stat().st_size <= 100000:
        print("[BOOTSTRAP ERROR] glad sanity check failed for gl.h", file=sys.stderr)
        return False
    if not khr_h.is_file():
        print("[BOOTSTRAP ERROR] glad sanity check failed for khrplatform.h", file=sys.stderr)
        return False
    if not gl_c.is_file() or gl_c.stat().st_size <= 50000:
        print("[BOOTSTRAP ERROR] glad sanity check failed for gl.c", file=sys.stderr)
        return False

    content = gl_h.read_text(encoding="utf-8", errors="ignore")
    if "GLAD_GL_VERSION_4_6" not in content:
        print("[BOOTSTRAP ERROR] gl.h missing GLAD_GL_VERSION_4_6", file=sys.stderr)
        return False

    print("[BOOTSTRAP] glad2 core 4.6 ready.")
    return True


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    if not bootstrap_lz4(root):
        return 1
    if not bootstrap_sdl3(root):
        return 1
    if not bootstrap_glad(root):
        return 1
    print("[BOOTSTRAP] All dependencies ready.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
