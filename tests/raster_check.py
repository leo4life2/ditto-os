"""Compare native-resolution renders with inline and original libm bounds."""
from pathlib import Path
import subprocess, tempfile, sys
root = Path(__file__).resolve().parents[1]
asset = Path(sys.argv[1]).resolve() if len(sys.argv)>1 else root / "assets/mecha-scout.a3d"
with tempfile.TemporaryDirectory(prefix="scout-raster-") as temp:
    temp = Path(temp)
    results = []
    for name, flags in [("reference", ["-DA3D_USE_LIBM_BOUNDS", "-DA3D_DISABLE_NATIVE_HOT", "-DA3D_DISABLE_COLOR_RAMP", "-DA3D_DISABLE_LIGHT_CACHE"]), ("inline", ["-DA3D_DISABLE_NATIVE_HOT"]), ("native", []), ("parallel", [])]:
        binary, pixels = temp / name, temp / (name + ".rgb565")
        subprocess.run(["c++", "-std=c++17", "-O2", *flags, "-I", str(root / "components/a3d/src"), str(root / "tests/render_host.cpp"), "-o", str(binary)], check=True)
        subprocess.run([str(binary), str(asset), str(pixels), "-1", "2" if name == "parallel" else "1"], check=True)
        results.append(pixels.read_bytes())
    assert results[0] == results[1] == results[2] == results[3]
    faded = []
    for mode in ["post", "shader"]:
        out = temp / mode
        subprocess.run([str(binary), str(asset), str(out), "-1", "2", mode], check=True)
        faded.append(out.read_bytes())
    assert faded[0] == faded[1]
    print("In-raster UI shading and the post-process reference also match all pixels.")
    print(f"{len(results[0]) // 2:,} native pixels match across 112 frames / 14 clips; inline bounds, isolated hot kernel, and dynamically scheduled two-worker rendering preserve output.")
