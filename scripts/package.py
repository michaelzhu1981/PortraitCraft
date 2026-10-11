#!/usr/bin/env python3
"""Package the binary, sources, tests and manual; exclude local toolchains."""
import json
from pathlib import Path
import zipfile
ROOT=Path(__file__).resolve().parents[1]
version=json.loads((ROOT/"manifest.json").read_text())["version"]
dest=ROOT/"dist"/f"PortraitCraft-{version}.zip"
paths=[ROOT/name for name in ("README.md","LICENSE","manifest.json",".gitignore","dist/PortraitCraft.wasm","dist/SHA256SUMS")]
for folder in ("src","scripts","presets","docs","tests"):
    paths.extend(p for p in (ROOT/folder).rglob("*") if p.is_file() and "__pycache__" not in p.parts and "output" not in p.parts)
with zipfile.ZipFile(dest,"w",zipfile.ZIP_DEFLATED) as z:
    for path in sorted(paths): z.write(path,Path(f"PortraitCraft-{version}")/path.relative_to(ROOT))
with zipfile.ZipFile(dest) as z:
    assert z.testzip() is None
print(f"Packaged {dest.name}: {dest.stat().st_size:,} bytes")
