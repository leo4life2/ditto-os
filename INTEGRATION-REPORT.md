> Versioned historical integration reports. Current firmware, acceptance gaps and newer evidence are in [WORK-STATUS.md](WORK-STATUS.md) and [LOCAL-ACCEPTANCE.md](LOCAL-ACCEPTANCE.md). A result below applies to its named binary; notably, 0.9.0's frozen sleep overlay has since been superseded by the lightweight animated overlay.

# Approved interaction design — firmware 0.9.0

Implemented and flashed the complete September 21 Ditto design handoff. C’s delayed, understated attention now responds to real events while contact physics remains immediate. The controller covers taps, pulls, flicks, holds, tilt, swing, shake/stop, repeated pokes, poke-to-drag, quiet idle and sleep. See [INTERACTION-DESIGN.md](INTERACTION-DESIGN.md) for the exact scene mapping and thresholds.

One final eye pose preserves dots, normal blinks and coordinated winks. Side/Happy silhouettes and automatic interaction x_x cannot reach the renderer. One mouth pose transitions from the current state; the approved tap holds its 3:1 oval for **675 ms**, then uses the exact **0.66 RoundMouth** blend before returning to a smile. Repeats extend the current reaction; the third poke gives a patient viewer look. Speech, drag and sleep supersede obsolete facial reactions without replaying a queue.

Shake recovery is event-triggered: rapid alternating input qualifies a shake, cessation adds **3.5× damping over 120 ms**, and even a gentle renewed tilt/swing or new touch restores the loose response. No stiffness, force-polarity, material-law or touch-force tuning changed. The gravity transform includes the added attention/breath pose so real-world down stays consistent. The mesh, glossy shader, board/TE driver and asset generator are byte-identical to 0.8.3.

The user chose **“Animate while settling, then freeze for battery.”** Sleep reaches the 24%-height / 165%-width eyes, tiny breathing oval and three lavender Zs, animates for about **1.9 s**, then parks. Minute clock/date refreshes leave character pixels unchanged. Existing 10% brightness, slow dim/fast wake, parked audio/physics and touch/IMU wake remain.

## Final-build evidence

- Portable checks cover relative tap timing at three different uptimes, repeat/drag interruptions, hold/release, voice/sleep arbitration, all-axis shake detection, exclusion of noise/pickup/slow motion, and **12,000 mixed irregular-timestep updates**. Dot silhouettes pass **160** conflicting-transform combinations. World-gravity/pose mapping, material, rounded impact, motion, activity and brightness regressions pass.
- **2,880 native 410 × 502 frames** cover the eleven approved scenes plus wake/speech using production behavior and synthetic input. The sleep preview verifies all frames after parking are byte-identical. Another **1,920 frames** cover all 16 existing emotes and face attachment. Contact sheets, including sleep, tap mouth stages and shake recovery, were visually inspected.
- Same-tile serial/two-worker pixels and deformation are byte-identical across **480 frames**. A different tile height changes **99 of 98,793,600 pixels**, including five one-pixel coverage disagreements. This is bounded rounding/coverage variation, not exact tile-height invariance.
- Actual-device checks passed tap timing, repeated contacts, pull/hold/flick/release, poke-to-drag, all-axis shake/stop, gentle resume, speaking interruption, sleep settling/freeze, clock update and touch/pickup wake. Separate captures cover Happy/Giggle/Celebrate/Wink, UI moods, strong drags, releases and interrupted emotes. Inputs are injected through the actual firmware/fusion path; stationary noise uses the real IMU. These are not robotic physical gesture measurements.
- The final **60.3-second** simultaneous audio/Wi-Fi/emote run averaged **24.05 FPS**, with peak rolling frame p95 **56.04 ms**. Command latency p95: **19.46 ms**. It processed 3066 input and 3016 output audio packets. No display/audio errors, unexpected resets, TE timeouts or missed scan deadlines. Smallest sampled predicted scan margin: **13.58 ms**.
- Home animation with real sensors averaged **26.25 FPS** over a separate approximately 19-second sample. The console is restored at `http://127.0.0.1:8781`; real IMU/touch and Mac clock sync are active.

## Limits

This remains a **24–26 FPS glossy prototype, not a locked 30 FPS release**. Lowest sampled free internal RAM under the combined workload was **2427 bytes**; headroom is tight. The behavior run recorded 8 rejected touch reports / 1 automatic digitizer recovery; the eye run recorded 0 / 0, and the combined workload recorded 0 / 0. The historical digitizer issue is not claimed fixed. Extreme stretching still clips the avatar at the viewport boundary, visible in the strong-drag capture.

Framebuffer inspection cannot prove physical tearing or optical appearance at 10% brightness. The existing TE presentation path is preserved and its telemetry is clean; the prior user confirmation that breakup disappeared belongs to 0.8.3. Battery runtime and cloud conversation remain separately unqualified/unconnected. The handoff’s desktop 0.733 s settling figure is not represented as a universal hardware guarantee.

Evidence: `test-results/ditto-interactions-0.9.0/validation-summary.json`, portable results, native videos/CSVs, device screenshots, `approved-hardware-check.json`, `eye-hardware-check.json`, `soak.json`, and build/flash logs. The first passing candidate is labeled under `first-candidate/`; the top-level device checks belong to the final binary.

Application SHA-256: `d02dfce494a7bebbd9a673321a0fb37dcee4f4b44b30d06cbe570acc5a009cac`. Unchanged avatar SHA-256: `3794f2e7c30b50afbd4fd4363dfa37794bf83df3f025c6d9c88b010937f007de`.
Package: `outputs/scout-os-ditto-interactions`.

---

# Glossy liquid and synchronized presentation — firmware 0.8.3

Flashed at native **410 × 502**. Ditto retains the same purple pigment, dot-eye controller and body/IMU/touch physics. The wet coat now has white reflections over softer body shading. The lower-left streak came from inadequate normal sampling at the foot/body join; the generator now checks midpoint-normal interpolation error. The same implicit surface is sampled with **958 vertices / 1,816 triangles** (formerly 845 / 1,590).

The previous display path sent tiles as they finished rendering without consuming the panel TE signal. The replacement completes a whole frame, swaps two PSRAM buffers, and lets a dedicated presenter upload the immutable frame in scan order. GPIO13 TE selects the scan phase at line 64; paired DMA strips are written behind the current scan and checked against the next pass using completion timestamps. Two DMA staging buffers overlap copying with transmission. QSPI remains **40 MHz**. The user physically dragged/shook the device and reported **“Breakup is gone.”** A subsequent aligned-copy change retained the same presentation algorithm.

The glossy shader uses inverse-transpose normal transforms and quadratic reconstruction of the normal/light coordinate, followed by a precomputed response/fade/dither palette. This keeps highlights from becoming visibly triangular. The palette contains lighting values, not animation frames. The existing 0–256 UI attenuation and screen-space RGB565 dither remain.

## Final-build verification

- Host review: **1,920 frames / 16 emotes**, plus **480 interaction frames** covering left/right drag, release, strong stretching, x_x and recovery. Every emote face vertex remains on the surface. Native screenshots and contact sheets were visually reviewed.
- Same-tile serial/two-worker glossy output and deformed body data are byte-identical. A deliberately different tile height changes **93 of 98,793,600 pixels** through rounding/coverage, including one single-pixel coverage disagreement; this is recorded rather than described as exact tile-height invariance. The legacy raster/reference check also passes across 112 frames. Dot silhouette checks pass across 160 combinations.
- Actual device captures cover Happy/Giggle/Celebrate/Wink, moods, left/right/strong drags, recovery, interrupted expressions and sleep/tap wake. Sleep still reaches 10%, parks rendering and audio, and wake restores the saved brightness. Real touch and IMU are restored. The focused run has zero TE timeouts, missed scan deadlines and display/audio errors.
- Final simultaneous audio/Wi-Fi/emote test: **60.2 seconds**, **24.13 FPS** average, peak rolling frame p95 **56.73 ms**. No display/audio errors, missed scan deadlines, TE timeouts or unexpected resets. Smallest sampled predicted next-scan margin: **13.43 ms**. The panel TE rate is about 59.9 Hz; this is not the animation frame rate.
- Normal home animation averaged **26.50 FPS** in the focused sample. The richer shader and completed-frame buffering currently cost frame rate: **this is not a locked 30 FPS release**, and large headroom is not claimed. Fixing optical tearing does not remove every frame-time variation. Existing digitizer, extreme-deformation cropping and battery-runtime limits remain.

The `blob_render.h`, `blob_physics.h`, `blob_material.h`, `blob_motion.h` and `ditto_eyes.h` hashes are unchanged from 0.8.2. Mesh and regenerated face seating samples differ because of curvature refinement. C’s separate personality refinement is outside this change.

Evidence: `test-results/ditto-gloss-0.8.3/validation-summary.json`, `soak.json`, `eye-hardware-check.json`, `optical-feedback.json`, host/device contact sheets and build/flash logs. Earlier experiments are retained under `development/`; their timings do not describe the final build.

Application SHA-256: `a53a14155214e0889b20b9908588ac3413c5145fe3de69a7f488dfa47d651e7d`. Avatar SHA-256: `3794f2e7c30b50afbd4fd4363dfa37794bf83df3f025c6d9c88b010937f007de`.
Package: `outputs/scout-os-ditto-gloss`.

---

# Dot-eye correction — firmware 0.8.2

Flashed. `DittoFace::Side` and `DittoFace::Happy` are removed from both the runtime and generator. Side-eye uses horizontal gaze of the original dot meshes. Happy, Giggle and Celebrate retain dots. One eye-pose controller selects normal blinking, wink, sleep or the existing x_x reaction; these no longer multiply one another or independent UI eye scales/rotations. An overridden blink is skipped to its end instead of resuming halfway. The normal 4-second blink cycle and its brief eased closure remain. Mouth, body emotes and the separately selected C/deadpan personality direction are outside this change.

## Validation

- The eye-pose unit check covers normal blinks, gaze, wink/cross/sleep exclusivity and override release. A geometry check keeps the dot silhouette invariant across **160** emote/time/gaze combinations, even with deliberately conflicting legacy eye scales and rotations.
- Rendered **1,920 native-resolution frames across all 16 emotes**, checking every face vertex remains seated on the skin. Visually reviewed all emotes plus enlarged Happy/Giggle/Celebrate/Wink details. A separate **480-frame** sequence covers left/right dragging, release, ordinary blinking, strong stretching and recovery; its contact sheet and motion preview were reviewed through sampled frames.
- The old and new renderer produce **byte-identical body positions, normals and node transforms across those 480 interaction frames** (8,732,160 bytes). Body physics, material, motion, activity, brightness and avatar asset file hashes are unchanged. This is an eye rendering fix.
- On-device captures cover Idle, Happy, Giggle, Celebrate, Wink, Sad and Curious; all four UI moods; left/right/strong dragging and recovery; an interrupted Wink→Happy→Idle sequence; and sleep/tap wake. The final run reported no display/audio error increments. Real touch and IMU remain enabled. Early harness traces are retained under `development-touch-lease/`: status sampled after a lengthy screenshot transfer describes the later automatic release, not the captured held pose. The final check records the held state before capture and avoids queued duplicate presses.
- A full **60.1-second** simultaneous audio/Wi-Fi/emote workload averaged **30.20 FPS**, with no display/audio errors or unexpected resets. Peak rolling frame p95: **34.85 ms**. Frame-headroom, intermittent digitizer, extreme-deformation cropping and battery-runtime limits from prior reports remain.

Evidence: `test-results/ditto-dot-eyes-0.8.2/validation-summary.json`, `body-preservation.json`, `emotes-contact-sheet.png`, `interaction-review.png`, `interaction-review.mp4` and device captures. Existing x_x stretching eyes are retained as an exclusive expression; only the Side/Happy silhouettes are removed.

Application SHA-256: `2b97a81c30e06deb90880d88f39e41692b82d1b4623779fd4cf73b540b1eed72`. Unchanged avatar SHA-256: `8c281fb08a9635426f4500a824f87c0b503285855d638b6baf3990d6ab2df522`.
Package: `outputs/scout-os-ditto-dot-eyes`.

---

# Physical gravity — firmware 0.8.1

Flashed. Ditto’s supported weight and bulk compression now follow the real-world down vector estimated from the accelerometer and gyro. The gyro propagates orientation; the accelerometer corrects its gravity reference. Screen-bottom gravity and its fixed base pivot are removed. Upright, sideways, inverted and screen-up poses compress along their actual down direction, with smooth diagonal deformation and continuing material memory.

Six symmetric log-strain components replace the former diagonal-only strain. A per-frame 3×3 eigensolve produces a volume-preserving positive deformation and its analytic normal transform. Camera orbit and avatar pose are accounted for before applying gravity to the mesh. A rounded support envelope follows the gravity direction with 180 ms smoothing. Mesh, face, material softness, tap/drag force coefficients, sensitive fast-shake modes and the chosen linear-acceleration sign are retained. Native rendering remains 410 × 502 with the same 845-vertex / 1,590-triangle asset. Details and limits: [MOTION-PHYSICS.md](MOTION-PHYSICS.md).

## Final-build validation

- Eight portable suites passed: directional material/pose mapping, material law, general touch, rounded impacts, fused motion, rapid shaking, activity detection and brightness curves. Arbitrary directions compress identically, reorientation retains existing strain, analytic normals and volume are correct, and 10,000 mixed-load steps remain finite.
- Eight controlled orientations ran through the actual firmware and renderer, including both depth directions and diagonals. Axial scale after about 2.25 seconds was **80.66–80.70%**, with reported FPS **30.01–30.30**. Maximum bulk determinant error: **8.3e-07**. A continuous 360° input rotation passed; maximum gravity-vector discrepancy against the synchronized reference was **0.0119 g**. These are injected sensor tests, not robotically measured physical rotations. Real IMU sampling was restored and verified afterward.
- Actual framebuffer captures were visually reviewed for sideways, inverted, diagonal and screen-up loading. The face and lighting remain attached. Local taps at four sites, repeated taps, downward/upward drags, release recovery and material settling passed.
- Six 4/6 Hz shake runs covered all three acceleration axes; mean reported run FPS **29.48**. Sleep parks upright at 10%, stops physics and audio, and motion/touch wakes the device. The existing 60-second inactivity and brightness implementations are unchanged; the full prior power audit below belongs to 0.8.0.
- A complete **60.1-second** simultaneous microphone, silent speaker PCM, emote and Wi-Fi scan workload averaged **30.17 FPS**. **1.3%** of frames completed within 25 ms; peak rolling p95 **34.99 ms**. No reported display/audio errors or unexpected resets. This remains short prototype validation, not a battery/runtime qualification.
- Regression testing exposed a USB transport boundary: expanded live status could exceed the driver's 4 KiB TX ring and be silently dropped. `emit` now sends bounded 1 KiB pieces. A dedicated check parsed **64** replies, including a **4181-byte** payload, with no malformed JSON; subsequent interaction and load tests passed. The failed traces and diagnostic raw stream remain under `development-usb-timeout/`.

## Scope and provenance

This is a supported viscoelastic gel approximation, not free fall, wall collisions, splitting or a particle fluid. Existing extreme-deformation cropping, intermittent digitizer reliability, full-load headroom, sustained-acceleration/tilt ambiguity and unmeasured battery runtime remain limits. Previous hardware/power measurements below remain labeled by version.

Evidence: `test-results/ditto-world-gravity-0.8.1/validation-summary.json` and `regression-suite.json`. An early continuous-rotation test paired stale accelerometer angles with newer gyro samples; its preserved failing trace is under `development-injection-timing/`. The corrected test generates matched readings on each injection, and passes on the final build.

Application SHA-256: `1bb00f8704379bb5cbe2f6b42d21402fb20d73834837967114c7a8729a9989f6`. Unchanged avatar SHA-256: `8c281fb08a9635426f4500a824f87c0b503285855d638b6baf3990d6ab2df522`.
Package: `outputs/scout-os-ditto-world-gravity`.

---

# Motion-aware standby — firmware 0.8.0

Flashed. Touch and measured movement share the 60-second inactivity timer. Pickup/translation/rotation or a screen touch wakes both the character and the display. Sensor noise and startup calibration settling are filtered separately from the accepted jelly physics. Sleep dims to 10% with a 1.6-second smootherstep; wake restores the saved setting with a 180 ms ease-out. Interrupting a dim starts from its current level.

Once the sleep pose settles, physics and animation stop. The finished frame stays on screen and refreshes at the minute boundary for clock/date changes. Audio converters, amplifier and shared I2S path suspend; the workers block. CPU frequency scaling is enabled, automatic Wi-Fi retry work pauses while sleeping, and touch/IMU checks continue at roughly 32 ms intervals. Awake animation retains 240 MHz and native 410 × 502 rendering. See [POWER-AND-SLEEP.md](POWER-AND-SLEEP.md) for the subsystem audit.

## Measured validation

- Portable activity and brightness tests passed, including sensor jitter, startup gyro bias, slow tilt, all axes, stale data, the 60-second deadline, monotonic dimming, fast wake, interrupted fades and direct manual brightness changes.
- Quiet task load averaged **0.57%** across both cores versus **56.15%** awake. These are FreeRTOS task-runtime measurements; interrupt time may be charged to the interrupted task. Clock residency independently measured **76.7% at 80 MHz** during the quiet sample, with brief 240 MHz work. Wi-Fi was unassociated in this measurement. This is not a current-draw or battery-runtime measurement.
- Parked physics and audio-input counters did not advance; renderer/audio workers recorded no task runtime in the quiet interval. Real IMU and touch samples kept advancing. Midnight changed the clock from 23:59 to 00:00 and the date from September 21 to 22; a pixel comparison found no changes in the sleeping avatar.
- Controlled 0.13 g impulses and 0.3 rad/s turns on all axes woke in **41–48 ms** after the first USB injection; saved brightness and audio were restored in **211–249 ms**. Slow tilt, touch during a partial dim, repeated audio suspend/resume and timer wake passed. Injected timing is not measured physical fingertip/pickup latency.
- The real rest observer passed: the device reached 10% after **61.69 seconds since its last activity**, stayed parked with fresh sensors, and woke on tap. Physical handling extended the overall observation to 112.23 seconds; this verifies that movement resets the deadline. The earlier controlled test's quiet-phase assertion was interrupted by actual rotation and two real contacts. That trace is retained rather than relabeled as a full pass.
- Awake baseline: **30.43 FPS**. A complete 60-second simultaneous microphone, silent speaker PCM, emotes and Wi-Fi-scan run averaged **30.25 FPS**. No display/audio error increments, audio power errors, runtime driver errors or unexpected resets were recorded. Peak reported rolling frame p95 was **30.41 ms**, and **75.5%** of frames finished within 25 ms; the historical 95% headroom target remains unmet.
- Codec resume retains its configured format and restores gain/volume, avoiding the redundant driver disable/reformat messages found during development. All final runtime logs are retained. Slightly shorter render/DMA tiles retain internal-memory headroom without changing native resolution or the avatar asset.

## Remaining limits and provenance

The controlled feature run recorded **8 rejected touchscreen reads / 1 automatic digitizer recovery**. The separate rest run recorded 0 / 0, and the combined run 0 / 0. The existing intermittent digitizer fault is not claimed fixed. Real sensor freshness remained available; historical extreme-deformation cropping and combined-load headroom limits remain. Connected-network standby and battery runtime have not been measured. This is responsive standby with USB and wake sensors available, not deep sleep or physical panel refresh-rate switching.

`test-results/ditto-motion-wake-0.8.0/validation-summary.json` identifies the final checks. Development attempts are preserved in subdirectories. Restarting the USB bridge causes a new USB/UART boot and clears live counters; it does not erase the saved evidence above.

Application SHA-256: `98cc1d1472f22a49de6c3d58584e013d178d710892bef97227a6a37fa349dea1`. Unchanged avatar: `8c281fb08a9635426f4500a824f87c0b503285855d638b6baf3990d6ab2df522`.
Source and firmware package: `outputs/scout-os-ditto-motion-wake`.

---

# Yielding material — firmware 0.7.9

Flashed and left awake on Home with fresh real accelerometer/gyro input. A force-driven viscoelastic bulk material now shares weight, tap impulses, held finger pressure, drag tension and acceleration. Weak long-term elasticity and relaxing internal strain allow sustained loads to change the resting shape; the accepted quick jelly waves remain. There is no programmed two-second shrink animation or target-height ramp.

The three trace-free log strains give a volume-preserving bulk transform and analytic normal correction. The body and face deform together. The screen uses a fixed support reference near the base; estimated gravity magnitude loads that support even when the screen faces up. Actual orientation continues to drive directional shears. This is an art-directed cohesive-gel approximation, not a particle-fluid, spill or collision simulation. Existing local touch deformation is not exactly volume-conserving.

## Validation

- Six portable suites passed: new material law, touch, rounded impact, all-axis motion, rapid shaking, and IMU freshness/recovery. Material checks include unchanged shape at zero load, creep under sustained pressure on any axis, unloading, unit volume, timestep consistency, extreme-force bounds, touch coupling and combined-deformation normals.
- Portable supported load: height 92.2% at 0.5 s and 80.7% at 2 s, approaching 80.1% at rest. This calibration follows from material coefficients and applied stress; elapsed time with zero load leaves the body unchanged.
- Device material check: height **80.68%** at the end of a ~2.15 s supported-load interval; width/depth **111.33%**. Downward dragging changed bulk height to **69.78%**, upward dragging to **99.41%**. Tap impulse compressed the contact axis, release recovered gradually, volume-product readbacks stayed within 0.0001 of one, and sleep returned the upright pose at 10% brightness with tap wake.
- Material test rendering: **30.264 FPS**. Active all-axis 4/6 Hz shakes: **546 frames / 18.069823 s = 30.216 FPS**. No display/audio error increments or unexpected touch/IMU recoveries in these focused runs. Real IMU freshness and changing raw axes were verified again after tests.
- Visual QA: 540 native-rendered frames across unloaded, loaded, pressed, lifted, released and shaken states. In comparison frames, silhouette height changed from 258 to 210 pixels (81.4%). Face and shading remain attached. Heavy downward compression spreads the silhouette past viewport edges in 81/540 frames; there is no wall collision boundary. Still captures from the actual device are included.

These are focused firmware/input-injection tests, not full OS, battery or cloud-voice qualification. Historical intermittent digitizer recovery and combined audio/Wi-Fi headroom issues below remain open. Hardware sensor recovery and the increased motion sensitivity from 0.7.8 are retained.

Application SHA-256: `a59cb24a8bd53ad8e8e3e0ac0f1f87a4c308432b091adbe1614d724d84a49c97`. Avatar unchanged: `8c281fb08a9635426f4500a824f87c0b503285855d638b6baf3990d6ab2df522`.
Evidence: `test-results/ditto-material-0.7.9/`. Package: `outputs/scout-os-ditto-material`. [Preview](http://127.0.0.1:8782/ditto/).

---

# Historical validation — firmware 0.7.8

Flashed and left awake on Home with real accelerometer and gyroscope data. The accepted 0.7.7 touch/jiggle damping and geometry bounds are retained.

## Failure and fix

Before the change, 20 readbacks over 2.18 seconds had **one unique accelerometer vector and one unique gyro vector**, even though the accepted-sample counter advanced 129 times and sample age stayed low. The old reader tested only whether a burst read was coherent and I²C succeeded; it refreshed the host timestamp even when repeatedly reading the same sensor output. It could therefore conceal a stalled IMU. The exact cause of the sensor stopping is not established.

The reader now requires the QMI8658's 24-bit sample counter to advance before passing a reading to fusion or refreshing sample age. A repeated counter cannot keep old motion alive. More than 150 ms without progress marks it unavailable, and 500 ms triggers a soft reset, configuration/readback and a fresh filter history while preserving learned gyro bias. Failed recovery retries are limited to once every 2 seconds. Status exposes the sensor counter, real sample age, stale reads, stalls, restart attempts and the control-register values observed at a fault. Startup also resets the IMU.

Timestamp semantics and reset sequence come from the [QMI8658C datasheet](https://qstcorp.com/upload/pdf/202202/QMI8658C%20datasheet%20rev%200.9.pdf), sections 5.6 and 5.8, and the saved vendor SensorLib reset implementation.

## Sensitivity

Small-input portable comparisons against 0.7.7: 0.10 g acceleration produces **2.57–2.71×** displacement, 0.25 rad/s rotation **3.27–3.43×**, small tilt **2.38–2.43×** bending, and a gentle surface-wave input **2×** deformation. Gyro, linear-acceleration and shake dead bands are reduced. Force directions, fusion, spring damping, spring limits and the accepted tap/drag behavior are unchanged. This is a model response comparison, not a claim about subjective feel.

## Validation

- Five portable suites passed: sample-counter health including wrap, stalled-data freshness and reset cooldown; six-axis physics/fusion/normals; 2–6 Hz shaking; touch physics; rounded impacts.
- Actual hardware fault test stopped both sensors through CTRL7. The normal watchdog detected stale output, marked it unavailable, and automatically restored fresh samples in **0.714 seconds**. The captured fault register showed CTRL7 = 0. No OS reboot was needed during this check.
- Live real-data checks before/after recovery and at final handoff require progressing hardware counters and changing raw accelerometer and gyro vectors. Four gravity directions, all acceleration/rotation axes, invalid input rejection, diagnostic expiry, sleep at 10%, and tap wake passed.
- 4/6 Hz injected shakes on all three axes: **545 frames / 18.070433 s = 30.160 FPS**. No display/audio error increments or unexpected IMU/touch resets in these focused runs. The fault test's one deliberate recovery is recorded separately.
- Native visual review: 480 live-rendered frames across tilt, acceleration and rotation, including an eight-pose sheet. Face and lighting remain attached. Strong combined motion can push part of the outer silhouette beyond the screen edge (49/480 frames in this deliberately vigorous sequence); small-motion sensitivity is increased without reducing the accepted deformation bounds.

Synthetic input checks exercise firmware behavior; they do not replace physically moving the device or judging feel. Final handoff separately verifies actual sensor data. Historical intermittent touchscreen recovery and combined audio/Wi-Fi headroom limits below remain unresolved.

Application SHA-256: `47ee181280fe19caa89cf50e6dfbfd8d16793a5639685016d4e88b720bb8f212`. Unchanged avatar: `8c281fb08a9635426f4500a824f87c0b503285855d638b6baf3990d6ab2df522`.
Evidence: `test-results/ditto-sensitive-0.7.8/`. Package: `outputs/scout-os-ditto-sensitive`. [Preview](http://127.0.0.1:8782/ditto/).

---

# Historical validation — firmware 0.7.7

Flashed and left awake on Home using real sensor input. All interactions are softer and carry more follow-through: rounded taps excite body waves as well as a dent, delayed ripple and root nudge; drag motion feeds a counter-wave and the released pull coasts with much lower damping; shakes bend farther and keep oscillating longer. Held pull remains responsive. Gravity equilibrium, sensor filtering and the selected acceleration direction are preserved.

## Verification

- Four portable suites passed: touch physics, rounded impacts, motion/normal transforms, and rapid shakes. Bounded integration, eventual settling and sleep reset remain checked. The local dent/ripple Jacobian has a sampled minimum of 0.1348.
- Matched inputs versus 0.7.6 produced 3.71× tap, 4.44× drag-release and 2.06× shake activity in the 0.5–1.5 seconds after input stopped. These are normalized simulation-activity comparisons, not physical viscosity measurements.
- Native visual review: 360 tap frames, 400 drag frames and 480 shake frames, including the inspected eight-pose contact sheet. All 1,240 frames remained inside the viewport; face and lighting followed the skin.
- Actual firmware with injected touches: four contact sites, delayed ripples, body nudges, 16 repeated taps counted once each, drag/release, 10% sleep and corner-tap wake passed. Repeated taps: **246 frames / 8.110498 s = 30.331 FPS**. Touch errors/recoveries were 0/0 in this focused tap run.
- Actual firmware with injected motion: 4 and 6 Hz on all three axes, around 60 samples/s. Active intervals: **544 frames / 18.068667 s = 30.107 FPS**. Both directions and retained after-wobble were observed; maximum residual wave after 4.5 seconds was 0.005192. No display/audio error increments in these active intervals. Sleep/wake and restoration of real sensors passed.

## Remaining limits

During the shake test's neutral settling interval, **8 rejected touchscreen reads caused one automatic digitizer restart**. Touch recovered and remained available through the remaining checks. This matches a previously observed failure pattern; its root cause is still unresolved. Passing interaction assertions do not mean the run was error-free. The raw counters are preserved in `shake-hardware-check.json`. Restarting the bridge creates a fresh USB/UART boot and resets live counters; it does not erase this finding.

These tests inject input into the real firmware; they do not establish hands-on feel or physical shaker response. Combined audio/Wi-Fi stress was not repeated, and the previous headroom limit remains open below.

Application SHA-256: `fc82c8099312f34b9fdda7567f7a8251f86a7e0e6256561cef693e3dc374c1eb`. Asset unchanged: `8c281fb08a9635426f4500a824f87c0b503285855d638b6baf3990d6ab2df522`.
Evidence: `test-results/ditto-jiggle-0.7.7/`. Package: `outputs/scout-os-ditto-jiggle`. [Preview](http://127.0.0.1:8782/ditto/).

---

# Historical validation — firmware 0.7.6

Flashed and left on real sensor input. Each contact injects momentum once into the local dent and whole-body X/Y/Z response. A coupled surface spring creates an outward annular ripple after the dent. The contact footprint broadens to about 47 px at half strength. Release only classifies the tap. A held contact does not repeatedly inject impact energy; ongoing drag forces retain their tuning.

Portable checks passed: rounded footprint, delayed ring, center-depth nudge, no second release impulse, no repeating held impulse, positive combined deformation Jacobian (minimum 0.1952), analytic normals, settling and sleep reset. The existing touch/motion/shake suites also passed. In the center-hit reference, the dent peaks at 0.262 around 92 ms, the outward ring follows around 175 ms, and whole-body depth reaches -0.076 model units.

Native visual review: 360 frames across four tap sites plus an eight-pose impact/ripple sheet. Device: four hit sites showed local indentation, a delayed ripple and whole-body displacement while broad squeeze pressure remained zero; drag/release, 10% sleep and corner-tap wake passed. Sixteen repeated taps were counted exactly once each: **245 frames over 8.081 s = 30.317 FPS**. No display error increments in that interval; touch errors/recoveries were 0/0 in the focused run.

The initial fixed 35 ms test delay raced UI consumption of the first contact. The final readback already contained the impact. The check now waits for the rendered hit within the short-tap window; no firmware change was needed for the rerun. Initial evidence is retained under `first-attempt/`.

These are injected touches on the actual firmware, not hands-on feel verification. Combined audio/Wi-Fi stress was not repeated, so historical headroom and touchscreen-recovery issues below remain open.

Application SHA-256: `a170aa279676ee2d4eda3173bb6dfd2d2ce22fd1a8d75c213ea43c518f24e186`. Asset unchanged: `8c281fb08a9635426f4500a824f87c0b503285855d638b6baf3990d6ab2df522`.
Evidence: `test-results/ditto-ball-0.7.6/`. Package: `outputs/scout-os-ditto-ball`.

---

# Historical validation — firmware 0.7.5

Flashed and left on real sensor input. Local tap velocity is 8 instead of 5 (+60%); directional/roll recoil doubles. The local displacement cap is 0.28, damping 6 and boundary restitution 0.45. The sharp spatial kernel, held-drag springs and 0.7.4 motion tuning remain unchanged.

Portable comparisons measured local peak displacement 0.2251 → 0.2800 and rebound -0.1076 → -0.1688, with 2x directional recoil. Drag spring trajectories were identical. The local deformation Jacobian remains positive (minimum 0.0834); normal checks at maximum indentation/rebound passed. Existing portable motion/shake checks passed.

Native rendering: 360 frames, four tap locations, eight inspected impact/rebound poses. Hardware: four localized tap sites, zero broad tap squash/pressure, drag/release, sleep at 10% and corner-tap wake passed. Touch errors/recoveries: 0/0. Tests use injected touch through the firmware's shared input path; subjective strength remains for hands-on review. No new combined-load performance measurement is claimed.

Application SHA-256: `0648fec4dea0e221d408159097b3a207be7308079247f6a6efe25e6c8c4b4aa0`. Asset unchanged: `8c281fb08a9635426f4500a824f87c0b503285855d638b6baf3990d6ab2df522`.
Evidence: `test-results/ditto-tap-0.7.5/`. Package: `outputs/scout-os-ditto-taps`.

---

# Historical validation — firmware 0.7.4

Flashed and left on real sensors. Motion damping is reduced roughly 45–51%, mass/bend stiffness is lower, acceleration deformation gain is higher, and bend/depth/wave travel limits are larger. Boundary rebounds retain more velocity. Gravity equilibrium and chosen acceleration/gyro directions are preserved; sensor filtering, mesh/resolution, touch controls and sleep behavior are unchanged.

## Validation

- Portable motion, touch and 2–6 Hz all-axis shake checks passed, including bounded combined input, volume determinant, analytic normals, noise rejection and sleep reset. Settling windows reflect the intentionally longer decay.
- Direct comparison with 0.7.3: gentle 0.8 g / 4 Hz wave peaks increased on all axes; residual modal energy at 0.5–1 s after stopping increased about 13.6x. Gravity estimates were identical. This describes the reduced-order simulation, not measured physical viscosity.
- Native preview: 480 frames and an eight-pose sheet inspected. No frame touched the viewport boundary. Face/lighting remained coherent through the larger deformations.
- Device: three seconds each at 4 and 6 Hz on all three axes, approximately 60 injected samples/s. **547 frames / 18.064 s = 30.282 FPS** across active shake intervals. Both motion directions and after-wobble were present; the largest residual wave after 3 seconds was 0.001707. No display/audio error increments in the shake intervals and no rejected touch reads in this focused shake run.
- Hardware regression: live IMU, four gravity directions, all acceleration/gyro axes, invalid diagnostics, diagnostic expiry, live-sensor restoration, 10% sleep and saved-brightness tap wake all passed.

Inputs are injected into the actual device firmware; this is not physical shaker-table testing or subjective feel verification. Combined audio/Wi-Fi stress was not repeated. The historical headroom and touchscreen-recovery issues below remain open.

Application SHA-256: `064e05df8b79a93ceb3eaa8026d5511086eb381e045daeb8591daec4afe31d00`. Unchanged asset: `8c281fb08a9635426f4500a824f87c0b503285855d638b6baf3990d6ab2df522`.
Evidence: `test-results/ditto-fluid-0.7.4/`. Package: `outputs/scout-os-ditto-fluid`. [Preview](http://127.0.0.1:8782/ditto/).

---

# Historical validation — firmware 0.7.1

Flashed and running with the real QMI8658 sensor restored. Tilt, translation and rotation now drive live liquid deformation alongside the stronger narrow tap. The character preserves momentum, bends, twists, compresses in depth and settles. Sleep remains at 10% brightness; a tap wakes it. Implementation, coordinate frame, calibration, limitations and sources are in [MOTION-PHYSICS.md](MOTION-PHYSICS.md).

## Passed checks

- Portable physics: six-axis frame mapping, known 90-degree rotation, transient translation versus gravity, measured-scale gyro bias, all six motion responses, momentum, settling, 30/60 Hz consistency, unit volume determinant and analytic normals. 30,000 combined touch/motion updates stayed bounded; sleep reset and invalid-input checks passed.
- Existing stronger tap, concentrated deformation, springy drag, expressions and touch recovery checks passed. Local impulse is 5.0 versus 2.8 before the strength request; its footprint is unchanged. Reference peak local displacement increased from 0.1205 to 0.2251, with rebound from -0.0509 to -0.1069.
- 480 native 410 × 502 host frames were identical with one and two renderer workers. Tilt, slosh, twist and selected actual device captures were visually inspected. The surprise-mouth target preserves vertex ordering to avoid its old collapsing transition. Asset SHA is unchanged.
- Hardware: live IMU configuration/readback, four gravity directions, all three acceleration and three gyro inputs, retained tap/drag, invalid diagnostic data, automatic diagnostic lease expiry and live-sensor restoration, 10% sleep and corner-tap wake all passed. The actual gyro bias is about 0.09 rad/s on one axis; the corrected filter removes it when stationary.

## Combined load and limits

Motion and touch injection ran alongside microphone capture, silent speaker output, emotes and Wi-Fi scans for **120.362 seconds**. Actual physical touches also occurred (165 contacts recorded during the measured interval).

- Average **30.242 FPS**, 3,640 frames.
- **31.15%** completed within 25 ms; the target is 95%. **59** exceeded 33.333 ms (1.62%). Extra headroom target **not met**.
- No display, audio RX/TX, audio overflow or unexpected OS reset errors. Audio input/output packets: 6,069 / 6,019.
- **8 rejected touchscreen reads caused one automatic digitizer restart.** Touch recovered and remains available. The raw failed report was all zero during the last rejection, which is consistent with a failed read, but the root cause is not established. The same controller-recovery path existed before this motion update. This run fails the strict zero-error acceptance gate; it must not be called fully clean.
- IMU accepted 6,858 samples during load, with 803 coherence/read retries and 0 rejected batches during that interval. One earlier rejected IMU batch is retained in the test run’s lifetime counter. Invalid samples were not applied.
- Peak sampled physics/deformation time: 6.513 ms. Internal free heap at end: 10,663 bytes; UI/render stack high-water free values: 1472 / 1720.

This is a targeted two-minute test, not full OS, battery-life or cloud-voice qualification. Injected direction checks do not establish physical feel; physical axes follow the vendor mapping and still need hands-on review. Actual changing raw motion was observed during readback. Sensor fusion cannot perfectly separate sustained linear acceleration from gravity or constant slow rotation around gravity from gyro bias.

Reopening the USB bridge starts a fresh device boot (USB/UART reset reason 11), so handoff counters are zero; the failed stress-run counters remain preserved above and in the raw evidence. No OS reset occurred inside the measured load interval. The handoff readback showed corrected angular velocity [0, 0, 0] at rest and about 0.005 g of residual linear acceleration.

The device is left awake on Home, using real sensor data and no injected touch/motion. The USB bridge is restored at 8781, and the [preview](http://127.0.0.1:8782/ditto/) includes the new motion demonstration.

Application SHA-256: `7a0fbebffa5834728a4c415e06e56451d5d4987bb0fbe3f30c39d50a45dbf895`. Asset SHA-256: `8c281fb08a9635426f4500a824f87c0b503285855d638b6baf3990d6ab2df522`.
Evidence: `test-results/ditto-motion-0.7.1/`. Earlier 0.6.2 and 0.7.0 results remain separate. Package: `outputs/scout-os-ditto-motion`.
