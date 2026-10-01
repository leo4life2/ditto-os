# Ditto: device-driven liquid motion

Firmware 0.9.0 uses all three accelerometer and all three gyroscope axes to drive a bounded, reduced-order viscoelastic simulation. This is live geometry on the ESP32. The preview video is a desktop rendering of the same code, not a video used by the firmware.

The 0.9.0 behavior controller adds selective **3.5× damping after shake cessation**, with a 120 ms ramp and immediate restoration on new input. Tilt and swing retain the original coefficients. The classifier and scene profiles are documented in [INTERACTION-DESIGN.md](INTERACTION-DESIGN.md). Earlier tuning history below is retained by version.

## What movement does

- Tilting moves the weight toward projected gravity and bends the upper body after the base.
- Translation uses the user-requested inverted linear-acceleration response, then rebounds and settles. Only gravity-separated acceleration changes sign relative to 0.7.1; gravity and gyroscope forces are unchanged. X, Y and depth retain independent momentum.
- Fast shaking excites three additional surface modes, with different body layers moving against each other. A separate gravity-subtracted band-pass preserves fast reversals without changing the established gravity/gyro filters. The waves ring down after movement stops.
- Rotating produces counter-rotation and a delayed twist. Angular speed and angular acceleration both contribute on all three axes.
- Depth acceleration compresses/expands thickness with compensating width and height. The motion-only deformation has determinant one, so it preserves local volume. Existing touch compression remains an artistic approximation.
- Agitation opens the mouth while keeping dot eyes. Delayed attention and recovery follow the approved interaction profiles; no automatic x_x is rendered.
- Touch forces combine with these modes. Rounded taps and actual drag movements also inject body-wave velocity, coupling contact to the same jelly response. Quick taps retain a local dent and a whole-body nudge rather than directly driving broad squeeze pressure.

There are no fluid particles, mesh subdivision, prerecorded motion, GPU shaders or runtime allocations. Thirteen bounded spring modes carry position and velocity. The simulation steps at most 1/120 s; its output deforms the current 958 vertices / 1,816 triangles. Exact inverse-transpose normals keep the smooth lighting attached to the moving surface. The face follows the same deformation. Its surprise morph now preserves the smile strip's vertex order, fixing the collapsed intermediate mouth.

## Sensor path and coordinates

The QMI8658 configuration is read back at boot: ±8 g accelerometer (4096 LSB/g), ±512 degrees/s gyroscope (64 LSB/degree/s), 112.1 Hz combined mode, low-pass mode 3 (~15 Hz). The housekeeping task polls approximately every 17 ms, subject to scheduling and I²C load. Sensor timestamps bracket the burst read; a batch that crosses an update is retried up to twice. Rejected batches are counted and never applied. A sample over 150 ms old stops driving the springs, allowing them to settle.

Both sensor vectors use the same proper rotation into model coordinates: `(chipY, chipX, -chipZ)` = screen right, screen up, toward the viewer. This corresponds to the vendor's portrait gravity example, whose pixel displacement is `(-accelY, +accelX)`. Physical direction/feel still needs a hands-on tilt check; automatic injection verifies the screen-frame implementation.

A complementary gravity estimator rotates its up vector with the gyro and slowly corrects toward the accelerometer when its magnitude and changes are consistent with gravity. Subtracting estimated gravity separates transient linear acceleration. This avoids treating a quick tilt as a large translation. Gyro bias is learned after 1.2 seconds of stable acceleration direction and stable low angular rate. The actual unit showed about 5 degrees/s of zero-rate offset, so calibration accommodates that offset rather than leaving a permanent lean. Low noise is dead-banded. No absolute velocity, position or heading is inferred by integrating the accelerometer.

Six-axis inertial sensing cannot perfectly distinguish prolonged linear acceleration from tilt, or slow constant rotation about gravity from gyro bias. The stationary heuristic and bounded modes suit a handheld character, not navigation or measurement. We have no magnetometer or external position reference.

## Sleep and diagnostics

Sleep eases to 10%, stops physics and retains the upright pose. Touch, pickup, translation and rotation wake both the character and screen. Motion restarts the same 60-second inactivity timer as touch. A separate noise-filtered detector also catches accumulated slow tilt; rendering jiggle is not counted as physical activity. See [POWER-AND-SLEEP.md](POWER-AND-SLEEP.md).

`system.status` exposes raw screen-frame specific force/gyro, estimated gravity and linear acceleration, bias, angular velocity/acceleration, deformation mode values, sample age, and IMU counters. `debug.motion` is a local USB-only diagnostic absent from the AI skill catalog. It accepts screen-frame `acceleration: [x,y,z]` in g and `gyro: [x,y,z]` in rad/s. Its 250 ms lease automatically restores real sensing when a test stops; `enabled: false` releases it immediately. Live sensor filtering continues during injection.

## Lower viscosity (0.7.4)

The motion simulation now uses about 45–51% less damping, softer mass/bend springs and more deformation room. Acceleration gain rises as stiffness falls so motion does not become sluggish. Surface-wave travel limits rise from 0.20/0.17/0.18 to 0.32/0.27/0.30 model units; body bending and depth compression also have more room. Boundary rebounds retain 45% of outward velocity instead of 20%, so reaching a bound does not kill most of the motion.

Gravity equilibrium and the selected acceleration/gyro directions are preserved. Sensor filtering, touch/drag/poke tuning, mesh, rendering resolution and sleep reset are unchanged. The wave modes remain near 3 Hz and the slow mass modes below 1 Hz. Lower damping intentionally extends the ring-down: hardware wave-settling checks allow 3 seconds and portable tests also check the eventual quiet state.

A direct comparison with 0.7.3 using a gentle 0.8 g, 4 Hz input found larger wave peaks on all axes and about 13.6 times the residual modal energy 0.5–1 seconds after stopping. This is a simulation comparison, not a measurement of physical fluid viscosity. Shape and normals are still evaluated by the same bounded reduced-order model. Evidence: `test-results/ditto-fluid-0.7.4/`.

## Fast shakes (introduced in 0.7.3)

The slow mass modes resonate around 1 Hz and attenuate rapid reversals. Three new surface modes resonate around 3–3.6 Hz. They receive a separate gravity-subtracted acceleration band-pass (70/s fast smoothing, 7/s baseline tracking, 0.045 g dead-band, 4 g vector cap). The existing linear-force direction from 0.7.2 is retained. Gravity and gyro filters, sensor registers and the original slow modes are unchanged.

Wave deformation moves different body layers against each other through three unit-determinant shears, with the matching analytic normal transforms. It uses the same mesh and rendering resolution. Fixed-step integration, bounded displacement, stale-input settling and sleep reset apply to these modes too. There is no random animation substituted for sensor direction. `system.status` exposes `motion_shake_g` and `motion_wave` for inspection.

`tests/blob_shake_check.cpp` checks 2–6 Hz reversals on all axes at 30 FPS with 60 Hz input, after-wobble, settling, rest-noise rejection, the selected positive direction, constant-acceleration rejection, bounded touch/shake combinations and sleep. The existing volume/normal checks include the new modes. `tools/render_shake.cpp` renders an inspectable native-resolution demonstration of the same firmware simulation.

`tests/shake_hardware_check.py` injects repeatable 4/6 Hz acceleration into the running firmware. This checks the software sensor path and live rendering, not physical shaker-table response or sensor bandwidth. Evidence is in `test-results/ditto-shake-0.7.3/`; final measurements are in `INTEGRATION-REPORT.md`.

## Acceleration-direction adjustment (0.7.2)

Seven linear-acceleration terms change sign: three position modes, three bend modes and depth compression. Raw sensor axes, gravity estimation, gyro torque, touch forces and sleep behavior are unchanged. The comparison harness verifies gravity-only and gyro-only responses are identical to 0.7.1, linear-only responses invert, and mixed input equals the old implementation with only linear acceleration negated. Portable motion/touch checks and focused device tests passed; evidence is in `test-results/ditto-accel-invert-0.7.2/`. The combined-load figures below belong to 0.7.1 and were not rerun for this sign-only adjustment.

## Evidence and reproducibility

- `tests/blob_motion_check.cpp`: axis handedness, known 90° gyro rotation, translation-versus-tilt separation, measured-scale gyro bias, all six responses, inertia, settling, 30/60 Hz consistency, unit deformation determinant, analytic normals, 30,000 combined motion/touch updates, sleep reset and invalid input.
- `tests/blob_physics_check.cpp`: retained narrow tap, springy drag, release, expressions and touch recovery.
- `tools/render_motion.cpp`: 480 frames of tilt, acceleration, rotation and settling; one/two-worker output is compared byte for byte. Motion preview and selected frames are visually inspected.
- `tests/motion_hardware_check.py`: live configuration/sample checks, injected gravity directions and all six impulses through the firmware, actual framebuffer captures, sleep/wake, invalid input and diagnostic-lease expiry.
- `tests/validate_motion.py`: targeted checks plus concurrent motion, touch, microphone input, silent speaker output, emotes and Wi-Fi scans. Results are under `test-results/ditto-motion-0.7.1/`; final measured performance and limits are in `INTEGRATION-REPORT.md`.

Hardware tests inject repeatable motion signals; they do not physically move the device or establish subjective feel. The live IMU is restored before handoff. Earlier 0.7.0 evidence is retained separately; it exposed the gyro bias issue and predates the corrected mouth. The full OS, cloud voice and battery-life qualification remain separate work.

## References

- [QST QMI8658C datasheet, revision 0.9](https://qstcorp.com/upload/pdf/202202/QMI8658C%20datasheet%20rev%200.9.pdf), sections 3.5–3.6 and 5.4–5.7. Saved under `../reference/QMI8658C.pdf`.
- [Waveshare board examples](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.06): local `../official/examples/esp-idf/04_Immersive_block/main/main.c` and `../official/examples/arduino/libraries/SensorLib/src/SensorQMI8658.hpp`.


## Extended jiggle (0.7.7)

Mass, bend, spin and depth stiffness and damping are reduced again; their displacement limits and linear-acceleration gains increase. The mass modes use stiffness 34–42 and damping 1.8–2.0; the bends use stiffness 18–21 and damping 1.35–1.45. Surface waves keep stiffness 280/340/300 but damping drops to 1.7/1.9/1.8, with travel caps 0.44/0.36/0.40. Boundary rebound retains 65% of velocity. These modes now receive tap impulses and drag-driven counter-waves too.

The gravity estimator, gyro-bias calibration, sensor configuration, selected acceleration direction and gravity-only equilibrium are unchanged. Longer coasting is deliberate: hardware shake checks allow 4.5 seconds to settle; portable checks include an 8-second wave tail and longer complete-body settling. A matched 0.7.6 comparison found 2.06× average model activity 0.5–1.5 seconds after a shake stopped. It does not measure material viscosity.

The same mesh, live renderer and bounded substeps remain. Native visual QA covers 1,240 frames across taps, drags and shaking, with no frame touching the viewport boundary. Evidence: `test-results/ditto-jiggle-0.7.7/`; current hardware results and retained platform limits are in [INTEGRATION-REPORT.md](INTEGRATION-REPORT.md).


## Fresh sampling and higher sensitivity (0.7.8)

The QMI8658 hardware timestamp must advance before a coherent sample enters fusion; identical counters do not refresh host sample age. Timestamp wrap is valid. Freshness expires at 150 ms. A 500 ms stall triggers soft reset and configuration/readback, rate limited to one attempt every 2 seconds. Learned gyro bias is retained, derivative history is reset, and live availability requires new samples after recovery. This fixes the old false-freshness path, which concealed unchanged real sensor data. It does not establish why the IMU originally stopped.

Gravity X/Y translation gains are 0.18/0.14; X/Y bend gains are 0.20/0.11. Linear translation gains are 0.75/0.57/0.30, bend gains 0.68/0.44/0.40 and depth gain 0.28. Gyro angular-speed gains are 0.12/0.12/0.18, with angular-acceleration gains 0.011/0.011/0.014. Wave gains are 0.44/0.36/0.44. Linear, gyro and shake dead bands are 0.012 g, 0.006 rad/s and 0.020 g respectively. Spring stiffness, damping and deformation bounds remain at the accepted 0.7.7 settings.

Status adds `imu_timestamp`, `imu_sample_age_ms`, `imu_stale_reads`, `imu_stalls`, `imu_recoveries`, `imu_fault_ctrl1` and `imu_fault_ctrl7`. Local-only `debug.motion.stall` disables the real IMU to exercise automatic recovery; it is absent from the AI action catalog. `tests/motion_recovery_check.py` verifies this fault, fresh counters and changing real raw axes before and after recovery. The regular motion test now also rejects frozen raw output. Full evidence and visual limitations are recorded in `INTEGRATION-REPORT.md`.


## Shared material loading (0.7.9 — superseded by 0.8.1)

Sensor sampling, the IMU stall watchdog, fusion, sensitivity and all motion-mode coefficients stay at 0.7.8. A new viscoelastic bulk response receives supported weight and transient inertial stress alongside the finger loads. It preserves bulk volume and relaxes internal shape memory under sustained force. See `TOUCH-PHYSICS.md` for its constitutive law, fixed on-screen support convention and limits; `INTEGRATION-REPORT.md` records hardware results.

## World-directed gravity (0.8.1 — current)

The fused accelerometer/gyro down vector now controls bulk compression and its support direction, not just the existing jiggle modes. A gyro measures angular rate, so it propagates orientation between accelerometer corrections; it does not supply absolute gravity by itself. The existing estimator, chip-to-screen mapping, bias learning, selected linear-acceleration sign, sensitivity and sensor recovery are unchanged.

`Material` integrates six symmetric log-strain components. Gravitational stress is `-1.5 * |g| * n nᵀ` with its trace removed. This compresses along arbitrary physical down, including diagonal and depth directions, and expands perpendicular to it. Existing strain and velocity are not rotated or reset when the input direction changes. The cached transform is `exp(strain)` with determinant one, and its inverse transpose handles lighting; a bounded 3×3 eigensolve runs once per physics frame. The face uses the same transform.

The support point follows physical down on an approximate rounded envelope, with 180 ms smoothing. The old fixed screen-bottom support is removed, including the small Y pivot in depth slosh. Camera orbit and avatar pose are inverted before applying gravity to the body mesh. Thus a character gesture does not decide physical down. Display-up orientation primarily compresses depth; sideways orientation compresses width. The normal material coefficients and accepted taps, drag, waves and gyro torque remain.

Status adds `material_gravity` (body frame), `material_support`, `material_shear` (XY/XZ/YZ), and `material_volume`. `material_scale` now contains the diagonal projections of the full symmetric transform; multiplying those three numbers is not its determinant when shear is present. Screen-frame gravity remains `motion_gravity_g`.

This is a supported cohesive-gel approximation, not free fall, particle fluid or wall collisions. Six-axis sensing still cannot uniquely distinguish sustained acceleration from tilt. Sleeping deliberately parks the upright character; movement wakes it and resumes gravity-driven physics.

Validation: `tests/world_gravity_check.cpp`, `tests/world_gravity_hardware_check.py`, existing motion/shake/tap checks and actual framebuffer captures. Results and measured frame rate are in the latest section of `INTEGRATION-REPORT.md`.
