# Mecha Scout live geometry study

**Rejected visual study.** The user rejected this geometry-only Mecha Scout as too low-poly and unlike the approved design. It was integrated into a temporary hardware build, but it is not the accepted character. The subsequent Blender art master (`modeling/scout-master.png`) was also explicitly rejected for failing to match the original design. Both are archived experiments, not candidates to continue optimizing. The recovered Labubu was subsequently tried and rejected for appearance. The installed avatar is now a procedural Ditto requested for local prototyping. See `AVATAR-PIPELINE.md`. This page preserves the rejected earlier experiment only.

The study retains the peach face, long ears, ivory helmet, coral crest, blue chest armor, gold vents, and red boots. Its geometry is authored in `tools/prepare_mecha.py`; no image frames or texture atlas are required. Rigid hierarchical joints allow the head, ears, eyes, mouth, shoulders, elbows, and legs to move independently. Surface normals provide rounded shading; small details are geometry.

Current asset: `assets/mecha-scout.a3d`, 4,730 triangles, 3,598 vertices, 31 material/joint meshes, 45 nodes, and 14 clips. It is substantially more detailed than the 1,040-triangle benchmark robot and was measured at **24.11 FPS** in a 60-second combined test on the ESP32, failing the 30 FPS target. Native host renders establish appearance and asset compatibility, not hardware performance. Remaining work includes silhouette/material refinement, expression and motion review, geometry budget, runtime clip/face integration, and a fresh combined hardware test.

## Reproduce a native preview

Run from this project directory:

```sh
python tools/prepare_mecha.py
c++ -std=c++17 -O2 -I components/a3d/src tools/render_asset.cpp -o tools/render_asset
tools/render_asset assets/mecha-scout.a3d /tmp/scout.rgb565 0 0 0.06 3.2
```

The output is 410 × 502 little-endian RGB565. Additional parameters are clip index, time in milliseconds, camera yaw in radians, camera distance in model radii, and frame count (default one). Multiple frames advance at 30 FPS on the animation timeline. Host computation speed is not a device benchmark.

The available clips are Idle, Listening, Thinking, Speaking, Wave, Yes, No, Happy, Curious, Sleep, Surprise, Point, Celebrate, and Sad. The rig is a first pass: these are authored gestures, not evidence of lip synchronization or real voice integration. Browser GIFs are previews of evaluated joint poses; the device asset contains geometry and keyframes.
