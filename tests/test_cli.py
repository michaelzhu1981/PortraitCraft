"""Exercise the public retouch entry point, including source preservation."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile
import unittest
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]

class RetouchCLI(unittest.TestCase):
    def call(self,*args):
        return subprocess.run([sys.executable,str(ROOT/"scripts/retouch.py"),*map(str,args)],capture_output=True,text=True)

    def test_batch_keeps_same_stem_inputs_distinct(self):
        with tempfile.TemporaryDirectory(dir=ROOT/"tests/output") as folder:
            base=Path(folder); source=base/"in"; source.mkdir()
            original=(ROOT/"tests/output/input.png").read_bytes()
            # Same image encoding, different filename suffixes; PhotoCraft sniffs PNG.
            for name in ("photo.png","photo.jpg"): (source/name).write_bytes(original)
            r=self.call(source,base/"out")
            self.assertEqual(r.returncode,0,r.stderr)
            self.assertEqual({f.name for f in (base/"out").iterdir()}, {"photo.png.retouched.png","photo.jpg.retouched.png"})
            self.assertTrue(all(f.read_bytes()==original for f in source.iterdir()))

    def test_existing_output_is_preserved(self):
        with tempfile.TemporaryDirectory(dir=ROOT/"tests/output") as folder:
            target=Path(folder)/"out.png"; target.write_bytes(b"keep me")
            r=self.call(ROOT/"tests/output/input.png",target)
            self.assertNotEqual(r.returncode,0); self.assertEqual(target.read_bytes(),b"keep me")

    def test_same_input_output_is_rejected(self):
        src=ROOT/"tests/output/input.png"; before=src.read_bytes()
        r=self.call(src,src); self.assertNotEqual(r.returncode,0); self.assertEqual(src.read_bytes(),before)

    def test_mask_export_is_rejected(self):
        with tempfile.TemporaryDirectory(dir=ROOT/"tests/output") as folder:
            folder=Path(folder); params=folder/"params.json"
            for view in ["Skin mask", "肤色蒙版"]:
                params.write_text(json.dumps({"view":view}))
                r=self.call(ROOT/"tests/output/input.png",folder/"out.png","--params",params)
                self.assertNotEqual(r.returncode,0); self.assertFalse((folder/"out.png").exists())

    def test_legacy_chinese_choices_match_english_output(self):
        with tempfile.TemporaryDirectory(dir=ROOT/"tests/output") as folder:
            folder=Path(folder); params=folder/"legacy.json"
            params.write_text('{"preset":"精修","mask":"全图","view":"效果"}')
            modern=folder/"modern.json"
            modern.write_text('{"preset":"Strong","mask":"Whole image","view":"Result"}')
            for file,name in [(params,"legacy"),(modern,"english")]:
                r=self.call(ROOT/"tests/output/input.png",folder/(name+".png"),"--params",file)
                self.assertEqual(r.returncode,0,r.stderr)
            self.assertEqual(Image.open(folder/"legacy.png").tobytes(),Image.open(folder/"english.png").tobytes())
            modern.write_text('{"preset":"Strong smoothing","mask":"Whole image","view":"Result"}')
            r=self.call(ROOT/"tests/output/input.png",folder/"current.png","--params",modern)
            self.assertEqual(r.returncode,0,r.stderr)
            self.assertEqual(Image.open(folder/"english.png").tobytes(),Image.open(folder/"current.png").tobytes())
            r=self.call(ROOT/"tests/output/input.png",folder/"alias.png","--preset","精修","--params",modern)
            self.assertEqual(r.returncode,0,r.stderr)

if __name__=="__main__": unittest.main()
