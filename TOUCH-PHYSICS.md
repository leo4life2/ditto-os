# Ditto slime interaction

Firmware **0.9.0** makes the existing native-resolution Ditto behave like a small piece of slime. The live mesh is still 1,816 triangles; no animation video is played by the device.

The sections below retain the design history; the later updates specify the current tuning.

The current face is governed by [INTERACTION-DESIGN.md](INTERACTION-DESIGN.md). Dot eyes, gaze, the approved oval/open-mouth sequence and smooth interruption replace the historical half-lidded, curved and cross-eye reactions. Body force coefficients remain unchanged in 0.9.0.

## Historical touch behavior (0.6.1)

- **Tap:** a concentrated, immediate indentation along the picked surface normal. Its half-strength radius is about 0.20 model units (nominally 29 px at the current framing), down from about 0.60 for the broad handle. Whole-body tap squash and compression kicks are removed; only a small root recoil remains. Contacts under 240 ms and under roughly 13 px of movement count as taps. The last presented triangle projection supplies a perspective-correct material point; background taps wake but do not hit the blob.
- **Hold:** the surface yields gradually and the face squeezes into `x_x`.
- **Pull:** a patch of skin follows the grab point while the rest lags behind. Root translation and rotation are deliberately small.
- **Pull down:** the top collapses into a broader, flatter puddle. Horizontal/depth expansion and a small bulge compensate for compression visually.
- **Release:** stored spring velocity drives a stronger overshoot and delayed waves across the body. Only released pull, squash and dent damping was lowered; held-drag spring tuning stays unchanged. Tap recoil has separate bounded springs.
- **Circle:** tangential motion agitates the body and its slosh modes.

A material-space deformation handle controls local stretch and depth. Two slower modes create the delayed cross-body wobble. The shape is bounded so it returns to the original rest mesh without inverted geometry or accumulated mesh damage. Integration uses steps of at most 1/120 second. This is a lightweight viscoelastic approximation: it does not simulate separate fluid particles, splitting, spills or exact volume conservation. The digitizer supplies position, not pressure; holding and motion provide the squeeze input.

Analytic transformed normals keep lighting attached to the deformed surface. Deformation runs across both CPU cores. There are no per-frame allocations. `main/blob_physics.h` holds the simulation; `main/blob_render.h` applies it to the body and face.

## Historical expression (superseded by 0.9.0)

The face is live geometry seated on the same curved surface:

- Gentler interaction produces half-lidded side-eye and a crooked smile, with a small gaze shift toward the touch.
- A sustained hold or stronger stretch produces `x_x` eyes and a readable flat/wavy mouth.
- Pulling also briefly opens the mouth before the stronger squish expression takes over.
- Reactions ease in and out. Side-eye follows a drag briefly (1.25-second timer), then the normal smile and blinking return. Accumulated annoyance no longer holds the eyelids down. A simple tap does not trigger side-eye.

`tools/prepare_ditto.py` generates `main/ditto_face.h` from the same implicit surface as the body. Face morphs use the existing eye and mouth topology and add no triangles. After each blink/tilt transform, the face is re-seated on a lookup of the actual triangulated skin before deformation. This prevents a blink from burying the ink in the skin. The transformed face coordinates and inverse transforms are checked across 1,920 desktop frames.

## Historical emotes and presentation (0.6.0)

Sixteen action clips are available through `avatar.emote`: Idle, Listening, Thinking, Speaking, Wave, Yes, No, Happy, Curious, Sleep, Surprise, Point, Celebrate, Sad, Wink and Giggle. Wave and Point deform the right blobby hand; Happy, Giggle and Celebrate add cheerful curved eyes; Wink closes one eye. A new touch interrupts a gesture through the existing input path. The bottom fade uses a per-row quintic easing curve with zero slope and curvature at its endpoints, eliminating the previous abrupt fade onset without added per-pixel work.

## Sleep

After 60 seconds without touch or qualified physical movement (and without a voice/audio session), the display eases to **10% brightness over 1.6 seconds**. Ditto returns to its upright closed-eye pose, then physics and animation stop. Touch, pickup or rotation wakes it, restoring saved brightness in **180 ms**. The finished image stays on screen with a refresh at each minute boundary for clock/date changes. See [POWER-AND-SLEEP.md](POWER-AND-SLEEP.md) for audio, CPU, sensor and radio behavior and measurement limits.

## Diagnostics and testing

Current evidence is in `test-results/ditto-sharp-taps-0.6.1/`. Prior 0.4.0 touch and 0.3.2 two-hour results remain historical.

Status includes `tap_count`, `tap_hit`, `tap_local_x/y` and `tap_bounce_x/y`. It also reports `slime_stretch`, `slime_press`, `face_x_eyes` and `face_side_eye` alongside the touch and rendering counters. The local-only `debug.touch` diagnostic shares the physical input path but is excluded from the AI action catalog. It has a 250 ms lease and reports physical contacts separately from injected ones.

```sh
c++ -std=c++17 -O2 -I main tests/blob_physics_check.cpp -o /tmp/scout-slime-check
/tmp/scout-slime-check
python tools/preview_slime.py
python tools/preview_emotes.py
python tests/validate_touch_release.py
```

The portable checks cover local versus distant deformation, directional response, settling, timestep consistency, bounded stress, analytic normals and expression recovery. Desktop QA renders 400 native frames and compares serial/parallel deformation byte for byte. Hardware QA exercises squish and expression transitions, actual framebuffer captures, sleep/brightness/wake, OS controls and timer navigation, followed by five minutes of touch simulation, microphone input, silent speaker output and Wi-Fi scans. Injected input does not measure fingertip latency or perceived physical feel.

Stop the USB bridge before direct serial tests or flashing. The validation runner restores it afterward, including after a failed performance gate.

## Digitizer recovery

A hardware validation run caught a controller that kept returning invalid reports until reset. The final firmware retains the single-read retry, then resets only the digitizer after eight consecutive rejected samples. Reset attempts are limited to one every two seconds. Corrupt reports never become touches. `touch_errors`, `touch_retries`, `touch_recoveries` and `touch_available` remain visible; recovery does not erase failure counters. The local-only `debug.touch.reset` diagnostic requests the same reset path and verifies fresh valid samples afterward. Portable tests cover the failure threshold, successful-read reset and cooldown. This is recovery from an observed fault, not proof of its electrical root cause.

## Focused tap update (0.6.1)

The tap uses its own bounded spring and a 3D radial falloff, separate from the broad drag handle. It starts on contact, so no release delay is needed to see the impact. Normal transformation uses the analytic inverse transpose of the local deformation before applying the existing drag/slosh deformation. The tap cannot feed global squash or the broad compression spring during its 240 ms classification window. A stationary longer hold eases into broad pressure over the next 120 ms; a moving grab activates the established broad pressure immediately after exceeding the tap-motion threshold. Existing pull stiffness, held-drag damping and release damping remain unchanged.

Targeted checks assert zero whole-body squash/broad compression during taps, under 5% of central dent displacement 0.6 model units away, correct lighting normals through indentation/rebound, four actual device tap sites, retained drag, and sleep/wake. A two-minute repeated-tap workload exercises audio and Wi-Fi. The earlier 0.6.0 five-minute and full-control results are historical. The current measurements are in `test-results/ditto-sharp-taps-0.6.1/`.

## Motion update (0.7.1)

The stronger narrow tap is retained, and all six inertial-sensor axes now drive independent liquid motion modes. Gravity, linear acceleration, angular speed and angular acceleration influence the body and expressions alongside touch. See [MOTION-PHYSICS.md](MOTION-PHYSICS.md) for units, sensor fusion, volume-preserving deformation, calibration, limits and validation.


## Stronger localized taps (0.7.5)

Tap impulse rises from 5 to 8 model units/s (+60%), while directional recoil rises from 0.28 to 0.56 and roll recoil from 0.12 to 0.24. The local spring retains stiffness 240, uses damping 6 instead of 7, and allows 0.28 displacement instead of 0.25. Its boundary rebound retains 45% of velocity instead of 15%; other touch springs keep their original parameters.

The spatial kernel is unchanged: the half-strength radius is still about 0.20 model units (29 pixels). Quick taps still do not feed the broad squeeze/drag modes. At the new maximum displacement the local deformation Jacobian stays positive with a worst-case determinant above 0.08. Tests cover its normals at both full indentation and rebound.

A direct comparison found peak local displacement 0.2251 → 0.2800, rebound -0.1076 → -0.1688, and doubled directional recoil. Held-drag spring trajectories remained identical. Motion/IMU tuning stays at the accepted lower-viscosity 0.7.4 settings. Evidence: `test-results/ditto-tap-0.7.5/`.


## Rounded ball impact (0.7.6)

Contact begins one impulse shared by the local dent and the whole-body response. The core kernel changes from `(1 + 10 r²)^-2` to `(1 + 4 r²)^-2`: its half-strength radius is about 47 pixels instead of 29 pixels. Poke velocity is 5.2, spring stiffness 175 and damping 7, with a 0.30 displacement cap. This spreads the strong pinpoint poke into a softer, rounded impact.

A coupled spring follows the dent with the opposite sign and deforms an outer-minus-core annulus. The result is an outward ripple delayed behind the initial depression. Its amplitude is bounded at 0.14. The combined dent/ring Jacobian remains positive across all allowed signed amplitudes, with a measured numerical minimum above 0.195. The analytic normal transform includes both kernels.

Whole-body X/Y/Z velocity receives a single kick along the inward surface normal on touchdown; off-center impacts also generate a small torque. A center tap therefore nudges the character into the screen as well as denting it. These modes spring back to rest. Release only classifies the tap; it does not inject a second kick. A held contact does not continually repeat the impact. Beginning a drag has this initial contact response, then the existing drag forces take over.

Portable tests check rounded footprint, delayed ripple, center depth nudge, no repeated hold/release impulse, bounded deformation/lighting, settling and sleep reset. The existing touch/motion/shake suites also pass. Status adds `tap_ripple` and `tap_bounce_z`. Hardware evidence: `test-results/ditto-ball-0.7.6/`.


## Coupled jelly motion (0.7.7)

All interactions now excite a softer, longer-lived response. The rounded impact footprint from 0.7.6 remains, but a tap also imparts velocity to the three body-wave modes. The initial contact therefore produces local denting, a delayed ripple, whole-body translation/torque and a traveling wobble. It is still a single impulse; neither holding still nor release repeats it. Local poke stiffness/damping are 145/4, with a 0.31 cap; ripple stiffness/damping are 70/2.4, with a 0.17 cap. The combined local dent/ripple Jacobian remains positive (sampled minimum 0.1348).

The held pull stays responsive (stiffness 155, damping 12). After release, stiffness/damping drop to 95/2.7, and the pull limit is 0.78 model units. Squash and indentation also coast longer on release. Actual drag deltas feed a counter-wave into the liquid modes, so the rest of the body trails the finger. Slow slosh springs follow the pull in the opposite direction, making the return overshoot visibly. Whole-body recoil stiffness is 40–48 with damping 2.2–2.5, and velocity restitution at its bounds is 0.5.

A matched-input portable comparison with 0.7.6 measured average simulation activity 0.5–1.5 seconds after input stopped: tap 0.060 → 0.222 (3.71×), drag release 0.071 → 0.317 (4.44×), shake 0.106 → 0.219 (2.06×). This describes the model's normalized activity, not physical viscosity. Longer settling tests still verify eventual rest, bounded deformation, normals, expressions and sleep reset.

Status adds `tap_seen_press`, the contact ID consumed by the renderer. Tests wait for this ID instead of mistaking a previous tap's lingering wobble for the next impact. Evidence: `test-results/ditto-jiggle-0.7.7/`; device results are in [INTEGRATION-REPORT.md](INTEGRATION-REPORT.md).


## Yielding bulk material (0.7.9 — original implementation)

`blob_material.h` adds a shared viscoelastic bulk response. It integrates applied stress against weak long-term shape restoration, transient elasticity and viscosity. An internal strain relaxes toward the present deformation, allowing creep under sustained load rather than only oscillation about the original rest mesh. There is no target-height timer: zero applied stress leaves a resting body unchanged, and weight, finger pressure, drag tension and acceleration use the same constitutive law. Existing local dents, ripples, sensitive motion and quick jiggle remain layered with this response.

The three diagonal log strains are trace-free. Their exponentials scale the geometry with determinant one; compression in one direction redistributes bulk volume to the others. The corresponding inverse-transpose updates lighting normals. The bulk transform applies to the face and body together, and requires only three per-frame exponentials plus per-vertex multiplies. It uses no extra mesh vertices or runtime allocations. Existing local touch approximations are not made exactly volume-conserving by this bulk constraint.

The body uses a fixed on-screen support reference near its base. Estimated gravity magnitude loads that support even when the display faces upward; real orientation continues to drive the existing directional shears. This is an art-directed cohesive gel model, not a particle fluid, free-falling object or collision simulation. It cannot split, spill or produce an arbitrary free surface.

Bulk coefficients: long-term stiffness 4.5, transient stiffness 4, viscosity 2 and internal relaxation time 0.35 s in normalized units. Supported load is 1.5 times gravity magnitude. Finger stress is proportional to held pull and press; a tap injects one compressive impulse along its contact normal. Inertial loading adds transient stress while existing rotational/shear modes handle turning and waves. Strain magnitude is bounded at 0.48 without violating the trace-free constraint.

A portable supported-load check yields 92.2% height at 0.5 s, 80.7% at 2 s and roughly 80.1% once settled. Applied pressure on any axis gives the same response. Zero load produces no timed shrinking, removing the load allows gradual recovery, and sleep resets to the previous upright pose. Status exposes `material_scale` and `material_memory`. Tests cover zero load, creep, all-axis compression, unloading, volume, timestep consistency, bounded stress, contact coupling and complete-deformation normals. Device measurements are in `INTEGRATION-REPORT.md`.

## Gravity direction update (0.8.1 — current)

The original fixed-base, diagonal-only load described above is superseded. Compression and support follow the fused real-world gravity vector, with full symmetric strain including diagonal shear. Bulk volume remains one; the face and highlights deform with the body. The accepted material softness, tap/drag forces, wave damping and chosen acceleration sign remain. Camera/pose conversion and diagnostic details are documented in the current section of `MOTION-PHYSICS.md`. Sleep still parks an upright pose, and touch/motion resumes live physics.
