# Approved Ditto interactions — current through 0.10.11

This implements the September 21 design handoff as live, event-driven behavior. Reference videos remain review artifacts; no video frames are embedded in firmware. The user initially chose to freeze after settling, then approved lightweight sleeping overlays. The body/physics settle and park; only the Zs and smooth translucent droplet continue animating.

## Ownership and priorities

`main/ditto_behavior.h` owns attention, the final mouth target, deliberate blinks, and subtle ambient pose. Touch physics runs immediately before behavior. `main/ditto_eyes.h` resolves one shape per eye: sleep takes precedence over sustained-shake dizziness, then wink, then a requested blink, then ordinary blinking. Overrides cannot multiply each other. Gaze translates the original dots. Side/Happy semicircles and automatic x_x do not enter the renderer.

`main/blob_render.h` applies the final face once, seats it on the skin, and deforms it with the body. UI moods and authored eye/mouth transforms cannot apply another flattening. Speaking blends to one amplitude-driven mouth target and clears obsolete reactions on completion. Contact remains physically responsive during speech. Sleep cancels facial work; wake does not replay it.

## Scene mapping

| Scene | Runtime behavior |
| --- | --- |
| Tap | Smile → 3:1 horizontal oval → 0.66 `RoundMouth` blend → smile. Contact-location-aware force remains immediate. |
| Slow pull | Delayed gaze follows the finger; the mouth opens. Release starts one open-mouth/return sequence. |
| Quick flick | Immediate physics, delayed gaze and the approved mouth recovery. A sustained flick becomes a pull. |
| Hold | Looks at the finger, then back to the viewer while held. Holds the reaction until release. |
| Tilt | Calm smile and a brief downhill glance; physical gravity remains measured in world space. |
| Swing | Original slosh coefficients, delayed lateral gaze, and one recovery after motion stops. |
| Shake → stop | Loose active shaking; classifier arms stronger damping only after rapid alternating input ends. Brief shaking recovers through the longer oval, open mouth and smile. Sustained vigorous shaking adds opposite-turning evenly spaced spiral eyes and an open pink wavy mouth, then recovers. |
| Repeated pokes | Every press reaches physics. Contacts within 1.2 s extend the current reaction; the third gives a patient viewer look. No emote queue. |
| Poke → drag | Retargets from the current mouth/gaze into the new contact; one recovery after release. |
| Quiet idle | Small breath, ordinary blink, occasional brief glance. Glances are spaced 12–22 s apart rather than looping a demonstration. |
| Sleep | About 1 s into the supported rest pose; thick dashes, tiny breathing oval, three lavender Zs. The body settles for about 1.9 s, then parks; only Zs and droplet continue at nominal 12 Hz. |

All finite horizontal-oval reactions share a **2,300 ms hold**: tap, quick flick, and releases after slow pull, hold, swing or shake. This replaces .45’s three-second hold with the user’s preferred 2.3 seconds everywhere. Entry and recovery speeds remain unchanged. A held gesture can keep its oval indefinitely; its release then receives the same full hold. New input retargets immediately and never waits for a facial timer.

The tap timeline is relative to each accepted contact: smile-to-oval at 20–200 ms, oval hold until 2,500 ms, open-mouth transition until 2,730 ms, open-mouth hold until 3,330 ms, return to smile until 3,830 ms. Repeated taps extend the current reaction without queuing another one. Oval half-width/height are 0.125 / 0.0417 model units. Other gestures retain their distinct entry/recovery profiles around the shared hold.

Sustained shaking earns dizziness only after **1.6 seconds of qualified shaking with fresh reversals**; charge decays at twice real time when that condition stops. Gentle movement, taps, tilts, slow swings and short shakes do not trigger spirals. Continued vigorous shaking extends recovery; after it stops, dizziness holds for 4.5 seconds, then fades. A new touch, speech or sleep cancels it immediately. The original dots shrink away as the two spirals appear, leaving unclogged centers. Each spiral has 1.65 evenly spaced turns and a 0.0085-model-unit half-width (13.3% heavier than .45), turning in mirrored directions at one revolution every 1.5 seconds, projected from the deformed face; analytic stroke coverage gives uniform soft edges and round caps. The open pink mouth has rounded, wavy lips and an antialiased dark outline. Its fill adds 48 triangles; all extra face geometry and projected paths use one 25,702-byte PSRAM allocation, with no per-frame allocation. Strokes test the body depth without modifying it. The projected paths are binned by viewport row before rendering, so each worker only checks nearby segments. Ordinary frames skip the extra face work. Base avatar triangle telemetry excludes the mouth fill.

## Shake recovery

`BlobMotion::ShakeRecovery` uses fresh gravity-separated signals, not rendered wobble. It requires two sufficiently rapid direction reversals above 0.11 g in the shake band, with reversal intervals between 55 and 300 ms. Ordinary sensor noise, a pickup, slow tilt and slow swing do not qualify. After 120 ms of quiet, the damping multiplier ramps smoothly from 1 to **3.5 over 120 ms** across translation, bend, spin, depth and surface-wave modes.

Meaningful renewed input—shake band above 0.055 g, linear acceleration above 0.065 g, angular speed above 0.12 rad/s—or a new touch restores the original loose response. Recovery does not change stiffness, force gains, displacement limits, gravity or the selected acceleration sign. Sensor fusion and physical sensor configuration are unchanged. The gravity-to-body transform includes the added attention and breath pose so those motions do not redefine physical down.

The classifier thresholds are prototype tuning supported by repeatable device injection and a real stationary IMU observation. They are not a shaker-table qualification. The handoff's 0.733 s desktop settling figure is not asserted as a universal hardware settling time.

## Sleep and power

Sleep eyes end at **24% awake height / 165% width**. Breathing uses a 5.5 s period and approximately ±0.6% vertical scale at rest. Three analytic lavender Zs have heights **9, 12, 15 px** and a 5.2 s drift cycle. Body breathing stops after settling. The Zs and smooth translucent droplet continue on a 5.2 s cycle, composited into small patches over the cached body. Clock/date refreshes preserve the character pixels. See `LIGHTWEIGHT-SLEEP.md` for current measurements. The screen remains at 10%, with the existing 1.6 s dim and 180 ms wake curves. Physics/audio stop, and real touch/IMU remain available for wake.

## Verification

- `tests/ditto_behavior_check.cpp`: shared oval timing across all reaction/release routes, repeats, immediate interruption, sustained versus gentle shaking, dizzy recovery, eye ownership, voice, sleep, all axes, and 12,000 mixed irregular-timestep updates.
- `tests/oval_reactions_hardware_check.py`: seven injected input routes on the actual renderer, per-route native captures, oval holds, dizzy recovery/interruption, and fault counters. The .45 complete route sweep passed; .46 tap/flick passed but a pull trial received real shaking and became dizzy as expected. Final .47 targeted dizzy/capture/tap-interruption checks passed. Scope: `test-results/ditto-tuning-0.9.47/RESULTS.md`.
- `tests/dizzy_stroke_check.cpp`: antialiased path coverage, overlap union, scene-depth rejection, row/column clipping and exact full-frame versus tiled output.
- `tests/dot_eye_geometry_check.cpp`: final silhouettes across 160 emote/time/gaze combinations with conflicting legacy transforms deliberately supplied.
- `tests/world_gravity_check.cpp`: actual pose/gravity mapping, including attention/breath/squash, plus material and mixed-load checks.
- `tools/preview_approved_interactions.py`: 2,880 native frames across the eleven approved scenes plus wake/speech; actual production behavior receives synthetic input rather than scripted face weights.
- `tools/preview_emotes.py`: all 16 semantic clips and surface attachment checks.
- `tests/approved_behavior_hardware_check.py`: actual firmware gesture timing, sensor fusion, stronger recovery, gentle resume, voice interruption, real stationary IMU, sleep freeze, clock updates and touch/pickup wake.
- `tests/eye_hardware_check.py`: device framebuffer captures of moods/emotes, strong drags, releases, transitions and sleep.
- `tests/liquid_render_check.py`: same-tile serial/two-worker frames and deformation equality.
- `tests/soak.py`: simultaneous audio transport, Wi-Fi scanning, emotes and display synchronization counters.

Run the hardware tools with `SCOUT_EVIDENCE_NAME=ditto-interactions-0.9.0` and only one USB owner. Build/flash and final results are in [INTEGRATION-REPORT.md](INTEGRATION-REPORT.md). Framebuffer captures cannot establish physical panel tearing or perceived 10% optical brightness; the unchanged presentation path has the prior hands-on confirmation. Current performance and hardware limitations remain explicit in that report.

## Settings and password feedback (0.9.12)

The selected network name is displayed in full within the 50 px safe area; wide 32-byte ASCII names use two monospace lines. Scan-list rows use explicit middle ellipses when necessary, preserving the beginning and end. A spoken selection resolves the complete name. Password review stays at 24 monospace characters per row, up to 63 bytes. This prototype's spelling/editing UI uses ASCII.

Inserted/replaced ranges are coral and underlined, including ranges spanning two rows. Deletions show a caret at the surviving gap, at the end, or in an empty field, and report the removed count. No-op edits retain the previous state. A rejected short secured password shows “Use at least 8 characters” on the device and returns the same reason; it does not begin a network connection. An accepted correction clears that validation message. Drafts say “Review password”, without claiming a successful or ready connection.

The 0.9.12 review uses actual framebuffer captures and synthetic tool calls. It covers settings boundaries, microphone state, a 24-hour timer, password edits and asynchronous scan ordering, conversation state injection, four extreme drag directions and a sleep/touch-wake cycle. It does not establish real voice, physical finger latency or secured Wi-Fi association. Extreme pulls can carry body pixels to the physical display edge; essential clock/date pixels remain inside their safe area. The home screen retains no captions or black fades.

## Authored emote rig (0.10.2)

A five-joint soft skeleton now owns head/hand emotes before the unchanged liquid deformation. Physical interactions retain priority. No/Sad use a closed squiggly mouth; Wink closes its left eye into a small fixed `>` and reopens in 515 ms while the right stays a dot; 0.10.3 removes the growing morph and bypasses slow body easing. In 0.10.4, the fixed eyelid is larger/heavier and held for 240 ms, with the same quick 40 ms closure and 75 ms reopening. Normal blinking, dizziness and sleep remain mutually exclusive eye owners. Rig definitions, preview and evidence: `RIG-AND-EMOTES.md`.

In 0.10.5, the fixed wink contour grows about 24% with 8% extra stroke weight; timing and eye ownership are unchanged.

In 0.10.8, the wink holds its closed target for 400 ms and carries a softly shaded rotating sparkle beside the winking eye. Both yield to physical reactions, sleep and clip cancellation.

In 0.10.10, only the wink star’s appearance changes to a rounded, plump five-point golden star. Wink timing, physical takeover and all body physics remain as before.

In 0.10.11, all open-mouth shapes use a pink interior with a dark antialiased border. Physical oval/melon reactions, speech, dizzy and sleeping mouths share the same fill path; closed line mouths remain dark. Openness fades the fill in/out while the existing reaction timings and priority rules stay unchanged. The sleeping fill becomes part of the cached image and adds no ongoing sleep redraw work.
