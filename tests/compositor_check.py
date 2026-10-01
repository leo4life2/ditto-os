"""Compatibility entry point for the current native compositor checks.

The old enlarged-scene compositor has been retired. Exercise the actual native
fade and tiled transport implementations instead.
"""
import runpy
from pathlib import Path
for test in ("native_shade_check.py", "transport_check.py"):
    runpy.run_path(str(Path(__file__).with_name(test)), run_name="__main__")
