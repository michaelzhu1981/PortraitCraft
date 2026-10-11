#!/usr/bin/env python3
"""Run the built module in PhotoCraft's actual wasmi host, not a mock."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "tests/output"
parser = argparse.ArgumentParser()
parser.add_argument("--cli", type=Path, default=ROOT / ".tools/photocraft/photocraft-cli-0.7.0-macos-universal/photocraft-cli")
args = parser.parse_args()
subprocess.run(["node", "--test", str(ROOT / "tests/wasm.test.mjs")], check=True, stdout=subprocess.DEVNULL)
original = np.fromfile(OUT / "reference-input.f32", dtype="<f4").reshape(240,320,4)
expected = np.fromfile(OUT / "reference-output.f32", dtype="<f4").reshape(240,320,4)
source = np.rint(original*255).astype("uint8")
Image.fromarray(source).save(OUT / "input.png")
records=[]
def execute(name, params, before=(), after=(), source_path=None):
    dest=OUT / (name+".png")
    cmd=[str(args.cli), "run", str(source_path or OUT / "input.png"),
         "--cmd", "plugin.install", "--params", json.dumps({"path": str(ROOT / "dist/PortraitCraft.wasm")})]
    for command, p in before:
        cmd += ["--cmd",command,"--params",json.dumps(p)]
    cmd += ["--cmd", "plugin.run", "--params", json.dumps({"id":"io.photocraft.portrait-smooth","params":params},ensure_ascii=False)]
    for command,p in after:
        cmd += ["--cmd",command,"--params",json.dumps(p)]
    cmd += ["--out",str(dest)]
    start=time.monotonic()
    r=subprocess.run(cmd,capture_output=True,text=True,timeout=120)
    (OUT / (name+".log")).write_text(r.stdout+r.stderr)
    assert r.returncode==0, r.stdout+r.stderr
    records.append({"name":name,"seconds":round(time.monotonic()-start,3),"passed":True})
    return np.asarray(Image.open(dest).convert("RGBA"))

actual=execute("host-default",{})
diff=np.abs(actual.astype(float)-np.rint(expected*255))
assert diff.max()<=1, (diff.max(),diff.mean())
records[-1]["max_error_8bit_vs_v8"]=float(diff.max())
assert np.any(actual[:,:,:3]!=source[:,:,:3])
assert np.array_equal(actual[:,:,3],source[:,:,3])
# Verify choices actually survive the real host's parameter validation and that
# each change yields a different image, rather than just a different UI value.
preset_results={}
for preset,slug in [("Gentle","gentle"),("Natural","natural"),("Strong smoothing","strong")]:
    image=execute("host-preset-"+slug,{"preset":preset})
    preset_results[slug]=image
    assert np.array_equal(image[:,250:],source[:,250:])
    records[-1]["mean_skin_change_8bit"]=round(float(np.abs(image[20:-20,20:180,:3].astype(float)-source[20:-20,20:180,:3]).mean()),3)
assert not np.array_equal(preset_results["gentle"],preset_results["natural"])
assert not np.array_equal(preset_results["natural"],preset_results["strong"])
custom=execute("host-custom-natural",{"preset":"Custom"})
assert np.array_equal(custom,preset_results["natural"])
assert np.array_equal(execute("host-zero",{"amount":0}),source)
assert np.array_equal(execute("host-undo",{},after=[("edit.undo",{})]),source)
mask=execute("host-mask",{"view":"Skin mask"})
assert mask[100,50,0]>230 and mask[100,300,0]==0
assert np.array_equal(mask[:,:,0],mask[:,:,1])
for bits in (16,32):
    high=execute(f"host-{bits}bit",{},before=[(f"image.mode.bits{bits}",{})])
    assert np.max(np.abs(high.astype(float)-actual))<=1
selected=execute("host-selection",{},before=[("select.rect",{"x":0,"y":0,"width":100,"height":240})])
assert np.any(selected[:,:100]!=source[:,:100])
assert np.array_equal(selected[:,101:],source[:,101:])
smart=execute("host-smart-object",{},before=[("layer.smartObjects.convertToSmartObject",{})])
assert np.max(np.abs(smart.astype(float)-actual))<=1
# Cross real host band boundaries (>4 MiB) and exercise the largest support.
large=np.tile(source,(4,4,1))
Image.fromarray(large).save(OUT / "large-input.png")
result=execute("host-large",{"preset":"Custom","scale":4},source_path=OUT / "large-input.png")
assert result.shape==large.shape and np.array_equal(result[:,:,3],large[:,:,3])
assert np.any(result!=large)
report={"host":"PhotoCraft CLI 0.7.0", "wasm_sha256":hashlib.sha256((ROOT / "dist/PortraitCraft.wasm").read_bytes()).hexdigest(),
        "checks":records,"fixture":"deterministic synthetic RGB skin/blue patches; no portrait quality claim",
        "preset_metrics":json.loads((OUT / "preset-metrics.json").read_text())}
(ROOT / "docs/verification.json").write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n")
print(json.dumps(report,ensure_ascii=False,indent=2))
