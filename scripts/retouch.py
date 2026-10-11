#!/usr/bin/env python3
"""Apply PortraitCraft through PhotoCraft CLI, preserving source files."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
ID = "io.photocraft.portrait-smooth"
LEGACY_CHOICES = {
    "preset": {"轻柔":"Gentle", "自然":"Natural", "精修":"Strong smoothing", "自定义":"Custom", "Strong":"Strong smoothing"},
    "mask": {"自动肤色":"Automatic skin", "取样肤色":"Sampled skin", "全图":"Whole image"},
    "view": {"效果":"Result", "肤色蒙版":"Skin mask", "红色叠加":"Red overlay", "左右对比":"Split view"},
}

def preset_name(value):
    return LEGACY_CHOICES["preset"].get(value,value)

def main():
    p=argparse.ArgumentParser(description="Retouch one image or a folder through PhotoCraft; save separate outputs")
    p.add_argument("input",type=Path,help="Image file or folder (non-recursive)")
    p.add_argument("output",type=Path,help="Output file or batch output folder")
    p.add_argument("--preset",type=preset_name,choices=["Gentle","Natural","Strong smoothing","Custom"],default="Natural")
    p.add_argument("--params",type=Path,help="JSON parameter file; see README for field names")
    p.add_argument("--cli",help="Path to photocraft-cli; otherwise search PATH and project .tools")
    p.add_argument("--format",choices=["png","tif","jpg","pcraft"],default="png",help="Batch output format")
    args=p.parse_args()
    cli=args.cli or shutil.which("photocraft-cli")
    fallback=ROOT / ".tools/photocraft/photocraft-cli-0.7.0-macos-universal/photocraft-cli"
    if not cli and fallback.is_file(): cli=str(fallback)
    if not cli: p.error("photocraft-cli not found; download the official CLI and pass --cli")
    source=args.input.resolve(); dest=args.output.resolve()
    if not source.exists(): p.error("Input does not exist")
    if source==dest: p.error("Input and output must be different")
    config={"preset":args.preset,"view":"Result"}
    if args.params:
        try:
            extra=json.loads(args.params.read_text())
            if not isinstance(extra,dict): raise ValueError("The JSON root must be an object")
            config.update(extra)
        except (OSError,ValueError) as exc: p.error(str(exc))
    for key, aliases in LEGACY_CHOICES.items():
        value=config.get(key)
        if isinstance(value,str): config[key]=aliases.get(value,value)
    if config.get("view")!="Result": p.error("Export requires view=Result; inspect masks and comparisons in the desktop plugin")
    schema=json.loads((ROOT / "manifest.json").read_text())["params"]
    unknown=set(config)-set(schema)
    if unknown: p.error(f"Unknown parameters: {', '.join(sorted(unknown))}")
    wasm=ROOT / "dist/PortraitCraft.wasm"
    if not wasm.is_file(): p.error("Run python3 scripts/build.py first")
    if source.is_dir():
        if dest.exists() and not dest.is_dir(): p.error("Batch output must be a folder")
        extensions={".png",".jpg",".jpeg",".tif",".tiff",".webp",".psd",".pcraft"}
        files=sorted(f for f in source.iterdir() if f.is_file() and f.suffix.lower() in extensions)
        if not files: p.error("No supported images in the input folder")
        jobs=[(f,dest/(f.name+".retouched."+args.format)) for f in files]
    else:
        jobs=[(source,dest)]
    # Preflight the whole batch so an existing output cannot be replaced.
    for src,out in jobs:
        if out.exists(): p.error(f"Output already exists; not overwritten: {out}")
        if src==out: p.error("Overwriting the original image is not allowed")
    failures=[]
    for src,out in jobs:
        out.parent.mkdir(parents=True,exist_ok=True)
        cmd=[cli,"run",str(src),"--cmd","plugin.install","--params",json.dumps({"path":str(wasm)}),
             "--cmd","plugin.run","--params",json.dumps({"id":ID,"params":config},ensure_ascii=False),"--out",str(out)]
        print(f"Processing {src.name} → {out.name}",flush=True)
        result=subprocess.run(cmd,capture_output=True,text=True)
        if result.returncode:
            failures.append(src.name); print(result.stderr or result.stdout,file=sys.stderr)
        else: print(f"Saved: {out}")
    if failures: raise SystemExit(f"{len(failures)} file(s) failed: {', '.join(failures)}")

if __name__=="__main__": main()
