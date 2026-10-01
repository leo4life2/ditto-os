# S3 glossy span verification

The production fast path is `main/surface_span_s3.S`, enabled only for ESP32-S3 in `main/CMakeLists.txt`. Other builds keep the readable C++ implementation. The native glossy input contract is halfword-aligned, disjoint color/depth destinations and a read-only 2,048-entry palette; phase 0..3, bounded signed 32-bit recurrences, and a span no wider than 410 pixels. Nonpositive counts must write nothing. The ISA loop/ABI is also exercised under concurrent firmware tasks.

`surface_span_s3_probe.cpp` compares the real assembly with the frozen preceding C++ span in `surface_span_reference.h`. It checks 12,531 cases / 2,450,898 pixels: saturation boundaries, depth rejection, empty/negative counts, odd halfword offsets, all dither phases and changing recurrences. Whole guarded output/depth arrays are compared, not only visible colors.

Prepare a fresh isolated copy using `python tools/prepare_surface_span_probe.py NEW_DIRECTORY`. It verifies production provenance, copies the current assembly, adds the framed diagnostic action only to that copy, and prints the build instructions. It never flashes or touches USB. Build the copy, stop the USB bridge, flash that probe using its guarded build wrapper, and run `python tests/surface_span_hardware_check.py --project NEW_DIRECTORY --port PORT --output FRESH.json`. Preserve the probe seal and flash log; its reported version alone cannot distinguish it from production. Restore the production firmware and bridge afterward.

The optional fixed-input timing blocks can produce constant-propagated reference specializations. They are diagnostic observations, **not a production speed comparison**. Use `tests/frame_profile.py` on the actual application, matching source/runner hashes and full-rate PCM, followed by the original-image control. Do not run a host build alongside the final comparison.

The original on-chip V1/V2 results are under `test-results/surface-span-asm-0.9.19/`. Geometry, physics, lighting interpolation and material tables remain outside the optimized span. A framebuffer PNG cannot prove physical panel timing or finger latency.
