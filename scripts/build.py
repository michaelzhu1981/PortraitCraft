#!/usr/bin/env python3
"""Build a freestanding, import-free PhotoCraft ABI v1 module."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
manifest = json.loads((ROOT / "manifest.json").read_text())
encoded = json.dumps(manifest, ensure_ascii=False, separators=(",", ":")).encode()
(ROOT / "build").mkdir(exist_ok=True)
(ROOT / "dist").mkdir(exist_ok=True)
(ROOT / "build/manifest.h").write_text(
    "static const unsigned char manifest[] = {" + ",".join(map(str, encoded)) + "};\n"
)
zig = os.environ.get("ZIG") or shutil.which("zig") or str(ROOT / ".tools/ziglang/zig")
if not Path(zig).is_file():
    raise SystemExit("Install Zig 0.13: python3 -m pip install --target .tools ziglang==0.13.0")
env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(ROOT / ".cache/zig"))
out = ROOT / "dist/PortraitCraft.wasm"
subprocess.run([
    zig, "cc", "-target", "wasm32-freestanding", "-O3", "-nostdlib",
    "-fno-builtin", "-fno-vectorize", "-fno-slp-vectorize",
    "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "build"),
    "-Wl,--no-entry", "-Wl,--export-memory",
    "-s",
    str(ROOT / "src/portrait.c"), "-o", str(out),
], check=True, env=env)
digest = hashlib.sha256(out.read_bytes()).hexdigest()
(ROOT / "dist/SHA256SUMS").write_text(f"{digest}  {out.name}\n")
print(f"Built {out.name}: {out.stat().st_size:,} bytes, SHA256 {digest}")
