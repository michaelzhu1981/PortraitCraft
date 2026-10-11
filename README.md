# PortraitCraft for PhotoCraft

PortraitCraft is an installable WebAssembly portrait-retouching filter for PhotoCraft. It provides smoothing at three detail scales, skin-color masking, texture and edge protection, and color adjustments.

Current release: **1.0.0**. Install [`dist/PortraitCraft.wasm`](dist/PortraitCraft.wasm). The plugin runs offline and requires no Python, Node.js, Zig, or network connection. Development and batch processing require additional tools.

The plugin targets **PhotoCraft 0.7.0 / WebAssembly ABI v1**. The official CLI is used for integration verification. The current release has not been reinstalled or visually verified in the running desktop application.

## Installation

1. In PhotoCraft, select **Filter → Plug-ins → Install Plug-in…**.
2. Install the downloaded `PortraitCraft.wasm` module.
3. Replace the existing installation when prompted. The stable plugin ID, `io.photocraft.portrait-smooth`, identifies earlier versions of this plugin as well.
4. Open a photograph in **RGB Color** mode and select its pixel layer. Duplicate the layer or convert it to a Smart Object if you want an editable workflow.
5. Open **Filter → Plug-ins → PortraitCraft…**. The default preset is Gentle. Inspect skin texture at 100% zoom.
6. Select **Skin mask** or **Red overlay** in `view` to check coverage. Adjust `tolerance`, use Sampled skin, or restrict the effect with a PhotoCraft selection.
7. Set `view` back to **Result** before applying. Mask, overlay, and split views are actual filter outputs; applying them writes that visualization into the layer. Use the host Preview checkbox to compare, or Cancel to discard.

Installation loads the module into the current process. To load it after restarting, enable **Edit → Preferences → Plug-ins → Use Additional Plug-ins Folder** and select the folder containing `PortraitCraft.wasm`. This project does not change your preferences.

## Language and upgrading

Project documentation, CLI help, and plugin-authored choice values use English. The preset order is **Gentle → Natural → Strong smoothing → Custom**. The menu entry uses the product name **PortraitCraft** in every language.

PhotoCraft controls the language of its menus, buttons, parameter labels, and choice strings already covered by its built-in translation catalog. Set the host's interface language to **Auto / Follow System** to use the operating system language. Custom plugin text has no general translation mechanism in ABI v1: missing host translations fall back to the English source text. This release does not add complete automatic localization or tooltips, and does not modify or rebuild PhotoCraft.

The current [`dist/PortraitCraft-1.0.0.zip`](dist/PortraitCraft-1.0.0.zip) archive contains the English documentation and updated binary. Other ZIP files in `dist` are historical snapshots from before the version was reset to 1.0.0.

## Presets and preview

Each preset supplies a complete smoothing recipe with distinct detail strengths, texture retention, threshold, and processing scale. Presets do not automatically change color, brightness, or mask coverage.

| Preset | fine | medium | large | texture | edge | threshold | scale |
|---|---:|---:|---:|---:|---:|---:|---|
| Gentle | 55 | 30 | 10 | 65 | 85 | 20 | Automatic |
| Natural | 85 | 65 | 35 | 35 | 75 | 45 | Automatic |
| Strong smoothing | 100 | 90 | 65 | 15 | 65 | 70 | Automatic; at least 2 |

**PhotoCraft ABI v1 cannot update, hide, or disable sliders when a preset changes.** The seven smoothing controls (`fine`, `medium`, `large`, `texture`, `edge`, `threshold`, and `scale`) apply only in **Custom** mode. Amount, mask, color, sharpening, and view controls apply in every mode. Custom's initial values match Natural; selecting Custom does not copy the last selected preset's values. To fine-tune Gentle or Strong, enter the table values manually. Default `amount` is 85; reduce it for a lighter effect.

The desktop default is Gentle because PhotoCraft moves the default choice to the top of the dropdown. The batch helper retains Natural as its default.

For `scale=0`, the processing canvas area selects a multiplier: below 4 MP uses 1, 4–10 MP uses 2, 10–20 MP uses 3, and 20 MP or larger uses 4. Strong uses at least 2. This adapts to image dimensions, not detected face size. Full processing canvas dimensions are used consistently across host bands.

Compare presets with the same amount, mask, and zoom. Check that the skin mask actually covers the skin; adjust tolerance or use a sampled color if needed. Split view shows the original on the left and the result on the right. Restore Result before applying.

**PhotoCraft 0.7.0 enables Low Resolution Previews by default and downsamples documents larger than 5 MP for preview.** Downsampling removes fine texture before the plugin runs. For accurate inspection, disable **Edit → Preferences → Performance → Low Resolution Previews**, reopen the filter, and inspect at 100% zoom. Full-resolution previews take longer to compute. Zooming the canvas alone does not disable preview downsampling. You can also apply to a duplicate layer and compare it with the original. The plugin cannot change this host preference.

The smoothing and texture formulas differ from the original development build. Existing preset results are not guaranteed to reproduce that build.

## Parameters

Parameter keys are stable ASCII identifiers. The host generates their visible labels; ABI v1 offers no custom labels or parameter tooltips.

| Key | Meaning |
|---|---|
| `preset` | Gentle / Natural / Strong smoothing / Custom; complete recipes or manual smoothing settings |
| `amount` | 0–100: blends the entire retouching effect with the original; 0 preserves the original exactly, except in mask and overlay views |
| `fine` | Custom only: smoothing of fine skin texture |
| `medium` | Custom only: smoothing of medium-scale unevenness |
| `large` | Custom only: smoothing of broader skin-color variations |
| `scale` | Custom only: 0 selects automatic scale; 1–4 selects a fixed multiplier for neighborhood radii of 2, 5, and 12 pixels |
| `texture` | Custom only: higher values reduce fine- and medium-scale smoothing to retain texture |
| `edge` | Custom only: higher values protect regions with greater local variance |
| `threshold` | Custom only: higher values allow smoothing of stronger local variations; 0 does not disable smoothing |
| `mask` | Automatic skin / Sampled skin / Whole image; Whole image is useful with a host selection |
| `tolerance` | 0–100: higher values include a wider skin-color range |
| `sampleR` / `sampleG` / `sampleB` | 0–255: reference RGB for Sampled skin; obtain values with the host's eyedropper |
| `warmth` | −100 to 100: negative cools, positive warms |
| `tint` | −100 to 100: green/magenta shift |
| `brightness` | −100 to 100: brightness adjustment; 0 is neutral |
| `contrast` | −100 to 100: contrast adjustment; 0 is neutral |
| `saturation` | −100 to 100: saturation adjustment; 0 is neutral |
| `sharpen` | 0–100: fine-detail sharpening inside the skin mask; default 0 |
| `view` | Result / Skin mask / Red overlay / Split view; restore Result before applying |

Color adjustments and sharpening are constrained by the skin mask and amount. Alpha remains unchanged. Hidden colors in transparent pixels do not contribute to smoothing.

## Limitations

- RGB 8-, 16-, and 32-bit layers are supported using the host's f32 buffer. Float output is not clipped to 0–1. CMYK, Lab, grayscale, indexed color, and standalone Alpha/Quick Mask targets fail with error code 2. Develop RAW files in the host first.
- Skin detection uses a chroma heuristic, without a face model. Warm backgrounds, wood, and clothing may be included. Colored lighting, very dark skin, and clipped highlights may be missed. Refine coverage with selections or sampled skin; eyes, lips, and hair are not guaranteed to be excluded.
- Smoothing reduces fine noise and uneven skin tone. It does not replace a healing brush for conspicuous blemishes, scars, or unwanted objects.
- ABI v1 cannot supply a separate custom panel, plugin eyedropper, mask brush, or automatic output layer. Use PhotoCraft's eyedropper, selections, duplicated layers, and Smart Objects. JSON preset import is available through the helper script, not through the host dialog.
- Classification uses display-range RGB. The plugin does not read or convert ICC profiles; adjust settings for wide-gamut or linear HDR photographs.
- The module does not read files or upload photographs. Large images remain subject to the host's memory and execution limits.

## Single-image and batch processing

Install `photocraft-cli` from the [official PhotoCraft 0.7.0 release](https://github.com/storytold/photocraft/releases/tag/v0.7.0) and make it available in PATH.

Run from the project directory with Python 3.10 or later:

```sh
python3 scripts/retouch.py photos/portrait.jpg output/portrait-retouched.png
python3 scripts/retouch.py photos/originals output/retouched --params presets/gentle.json
python3 scripts/retouch.py photos/portrait.tif output/portrait.tif --preset 'Strong smoothing'
python3 scripts/retouch.py photos/portrait.jpg output/custom.png --params presets/custom-natural.json
```

Batch processing is non-recursive. Output names retain the source extension, such as `a.jpg.retouched.png`, to prevent collisions between JPEG and TIFF files sharing a stem. The helper refuses existing outputs, input/output identity, and mask or comparison exports. It does not convert color mode automatically. Failures identify the file and return a nonzero exit status.

PSD and PCraft inputs process only the active layer or Smart Object, not every layer. PNG/JPEG and default TIFF exports are flattened; use `.pcraft` to retain layers. Use a float-capable format for 32-bit results; PNG/JPEG reduce bit depth.

## Building and verification

Development requires Python 3.10+, Zig 0.13.0, and Node.js 20+. Host integration requires NumPy, Pillow, and the PhotoCraft CLI. Downloading the compiler initially requires a network connection; subsequent builds require neither third-party C libraries nor WASI.

```sh
python3 -m pip install --target .tools ziglang==0.13.0
python3 -m pip install numpy pillow
python3 scripts/build.py
node --test tests/wasm.test.mjs
python3 tests/host_integration.py --cli "$(command -v photocraft-cli)"
python3 tests/test_cli.py
python3 scripts/package.py
```

Set `ZIG` to an existing Zig executable if necessary. The build writes `dist/PortraitCraft.wasm` and `dist/SHA256SUMS`. Run the host tests before batch tests to generate their shared fixture. Batch tests need the CLI in PATH.

Checks cover ABI/no imports, exact zero-strength preservation, preset strength ordering, threshold/edge protection, automatic scale, skin/background isolation, texture retention, individual smoothing and color controls, transparent edges, RGB bit depths and HDR, NaN/Infinity isolation, explicit non-RGB errors, seamless maximum-radius tiles, and canvas-based split view. Official-host checks cover presets, Custom mode, installation, processing, export, undo, mask output, bit depths, selections, Smart Objects, and band boundaries. Batch checks also exercise legacy Chinese parameter conversion. See [`docs/verification.json`](docs/verification.json) for the current release's CLI verification report.

Fixtures are deterministic synthetic skin/blue patches and texture variations. Multi-skin-tone portrait quality has not been evaluated.

## Implementation and references

`src/portrait.c` calculates skin- and alpha-weighted local means and second moments at three scales. Local variance protects edges; the results are combined as detail bands and blended through the mask. Separable running sums make each scale O(pixel count), independent of neighborhood area. Maximum support is 48 pixels, matching the manifest overlap. Double-precision accumulation reduces drift across long bands.

- [PhotoCraft 0.7.0 plugin ABI, pinned source](https://github.com/storytold/photocraft/blob/79261615956ab21334e49ca1d384390272e647da/docs/plugins.md)
- [PhotoCraft plugin dialogs](https://github.com/storytold/photocraft/blob/79261615956ab21334e49ca1d384390272e647da/crates/ui-egui/src/plugin_ui.rs)
- [PhotoCraft language handling](https://github.com/storytold/photocraft/blob/79261615956ab21334e49ca1d384390272e647da/crates/ui-egui/src/i18n/mod.rs)
- [PhotoCraft filter previews](https://github.com/storytold/photocraft/blob/79261615956ab21334e49ca1d384390272e647da/crates/ui-egui/src/filter_dialog.rs)
- [PhotoCraft preview resolution preference](https://github.com/storytold/photocraft/blob/79261615956ab21334e49ca1d384390272e647da/crates/ui-egui/src/proxy.rs)

PhotoCraft belongs to its respective owner. This project is not affiliated with PhotoCraft.
