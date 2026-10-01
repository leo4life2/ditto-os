# Replaceable live 3D avatar

The current character is a procedural Ditto, requested on September 19 after the user rejected the Labubu appearance. It runs as live geometry on the ESP32-S3. The previous printing source and Blender assembly remain intact as historical work.

## What runs on the board

The current asset has **1,816 triangles, 958 vertices, four meshes, six nodes, and 16 semantic clips**. Its A3D file is 104,824 bytes. The firmware renders at the panel's native **410 × 502**, with a 30 FPS active target and reduced drawing when idle. Current interaction evidence is in `test-results/ditto-interactions-0.9.0/`; the unchanged glossy renderer and synchronized presentation were introduced in 0.8.3; measured rates and limits are in `INTEGRATION-REPORT.md`. The 30 FPS figure is a target, not a guaranteed minimum.

The blob is a smooth implicit union of rounded volumes, with its surface extracted along rays. The exporter refines shared triangle edges around curvature, silhouette and midpoint-normal interpolation error, rather than decimating a printing mesh indiscriminately. Every body edge has two incident faces. Analytic gradients supply continuous surface normals. Eyes and a small smile are separate surface-conforming meshes attached to the same body transform.

The smoothing principle is also used by [Three.js indexed geometry](https://threejs.org/docs/pages/BufferGeometry.html): shared vertex normals produce continuous lighting. This implementation uses our C++ software renderer, not Three.js/WebGL. Three.js's [Phong material](https://threejs.org/docs/pages/MeshPhongMaterial.html) shades per fragment; our current embedded path reconstructs the normal/light coordinate quadratically across each triangle, then evaluates a precomputed glossy response per fragment. This avoids lighting a few vertices and stretching a sharp highlight across a triangle. The inverse-transpose normal transform preserves lighting through squash and shear.

Materials use a fixed screen-space 2 × 2 ordered RGB565 pattern to preserve fractional color levels. The glossy palette includes 0–256 UI attenuation before quantization, so the white coat reflections fade along with the pigment without the old 32-step horizontal fade bands. The palette contains lighting values, not poses or animation frames. The lighting cache hashes all normal indices into a bounded cache and verifies the key on collision. These changes preserve native geometry resolution; there is no low-resolution render enlarged behind crisp text.

## Animation

Firmware 0.9.0 adds an event-driven behavior controller with one final eye/mouth pose, delayed attention, immediate physical contact and interruptible gesture reactions. No authored eye/mouth transform can independently distort that pose. Idle and sleep use the approved subtle breathing instead of perpetual rocking or a canned squash. Sleep settles then freezes. [INTERACTION-DESIGN.md](INTERACTION-DESIGN.md) records the accepted profiles and validation.

The device interpolates transforms locally: breathing, squashing, stretching, leaning, bouncing, nodding, looking around and blinking. The face stays attached to the blob. Animation can transition over 280 ms and respond to tilt and conversation state without waiting for a model response. Speaker amplitude can modulate the mouth; this is not phoneme-based lip sync.

Firmware 0.10.2 adds a five-joint soft skeleton (root, body, head and two hands), with weighted skinning and analytic normal transforms before the existing liquid physics. The A3D transform hierarchy still supplies the base geometry and semantic action durations. See [RIG-AND-EMOTES.md](RIG-AND-EMOTES.md) for the current runtime rig and review. The current material is a supported viscoelastic gel approximation, not a particle fluid. Touch physics and a more upright sleep pose were added in 0.4.0; see `TOUCH-PHYSICS.md`. The 16 shared actions are Idle, Listening, Thinking, Speaking, Wave, Yes, No, Happy, Curious, Sleep, Surprise, Point, Celebrate, Sad, Wink and Giggle. Some use related movements with different timing or pose adjustments.

The board plays no rendered video or sprite frames. `tools/preview_ditto.py` makes a review MP4 on the Mac with the same software renderer. That file is a preview artifact and is never embedded in firmware. Actual device captures are labeled separately in the gallery.

## Rebuild

```sh
python tools/prepare_ditto.py
bash build.sh build
bash build.sh -p /dev/cu.usbmodemYOUR_DEVICE flash
```

The exporter needs only Python's standard library and the vendored A3D converter. Rebuild these outputs together:

- `assets/avatar.a3d`: geometry, materials, hierarchy and animation channels.
- `assets/avatar-manifest.json`: identity, source, complexity and capabilities.
- `main/avatar_asset.h`: semantic clip/node IDs; absent ears are explicitly `-1`.
- `host/avatar_catalog.py`: matching action names.

`tools/preview_ditto.py` additionally needs Pillow, a C++ compiler and ffmpeg on `PATH`; it produces native views and a motion preview in `test-results/ditto-integration/`. Use `--output-dir PATH` to choose another review directory. Its compiled renderer and raw frames live in a temporary directory and are cleaned up after generation. Device captures come through the existing USB bridge. Stop the bridge before direct serial tests or flashing.

## Verification

`tests/raster_check.py assets/avatar.a3d` checks 112 native frames against the uncached/reference raster path. `tests/smooth_color_check.py` verifies identical serial/parallel rendering and fixed dither phase across odd/even tile origins on a uniform patch. `tests/depth_check.py` compares fixed-point and float depth. The earlier firmware 0.3.2 passed actions, captures and a two-hour mixed audio/Wi-Fi/animation run. Physics changes in 0.4.0 have their own evidence in `test-results/ditto-touch-0.4.0/`. `INTEGRATION-REPORT.md` records the exact firmware hash and measured limits. Physical-panel review confirmed the 0.8.3 tearing correction. Secured Wi-Fi association and real conversation retain their separately recorded acceptance status.

## Asset provenance

The mesh is generated locally from formulas as a Pokémon Ditto fan prototype. No external Ditto mesh or texture is embedded. The visual reference was [Pokémon's official Ditto artwork](https://diamondpearl.pokemon.com/en-gb/trainersguide/fundamentals/raising/). The earlier Chocobo Labubu source is archived, retains its recorded attribution, and is no longer the installed avatar.

Firmware 0.5.0 introduced local slime deformation and face morph targets generated as `main/ditto_face.h`; mesh topology and triangle count are unchanged. The reproducible 400-frame preview is generated by `tools/preview_slime.py`. Current hardware evidence is isolated in `test-results/ditto-emotes-0.6.0/`.

## Spring and emote update (0.6.0)

`main/blob_pick.h` intersects taps with the last presented body triangles and interpolates the material point. `main/blob_render.h` re-seats ink after blink/tilt transforms, before skin deformation. It historically added bounded hand motion and happy/wink faces. Those authored movements now use the soft skeleton; the current wink closes into a fixed small `>` for a 515 ms close/hold/open envelope with quick edges, a 400 ms hold and a small procedural rotating sparkle and Happy retains dot eyes. `tools/preview_emotes.py` renders all 16 clips and verifies face placement for every frame. `tools/preview_slime.py` compares 400 frames between serial and parallel deformation. No pre-rendered animation frames are installed on the device.

Firmware 0.6.1 leaves the asset and all 16 clips unchanged. It adds a separate narrow radial tap deformation in `blob_physics.h`, with analytic normals, before the existing broad drag and slosh. See `test-results/ditto-sharp-taps-0.6.1/` for current runtime evidence.
