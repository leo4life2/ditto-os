# Responsive standby — 0.9.8

The device sleeps after **60 seconds without touch or qualified physical motion**, provided no voice session or audio stream is active. Handling it restarts that inactivity timer. A screen touch, pickup, translation or rotation wakes both Ditto and the display. This is responsive standby, not a CPU reset or deep sleep.

The approved sleep face and three drifting Zs now settle for about **1.9 seconds** before parking. The eyes reach 24% height and 165% width, with a tiny oval mouth and ordinary supported body height. The latest approved lightweight approach freezes the glossy body and its physics after settling, but continues the three drifting Zs and a continuous, translucent water droplet attached to the mouth, gently bobbing and stretching on the same 5.2-second cycle as the first Z at a nominal 12 Hz. Only two small rectangles are recomposed and transferred. The clock/date still refresh each minute. This supersedes the earlier decision to freeze the Zs too. See [LIGHTWEIGHT-SLEEP.md](LIGHTWEIGHT-SLEEP.md). See [INTERACTION-DESIGN.md](INTERACTION-DESIGN.md).

## Brightness

Sleep fades from the brightness currently displayed to **10% over 1.6 seconds**, using quintic smootherstep (`6t⁵ − 15t⁴ + 10t³`). It starts and finishes gently. Wake restores the saved setting over **180 ms**, using cubic ease-out, which provides most of the brightness early. Reversing a transition starts from the current level. Ordinary brightness changes remain immediate. Durations follow elapsed wall time rather than a frame count.

The panel accepts discrete brightness values, so this curve is quantized to integer percentages. These are brightness commands, not a measurement of the panel's optical response. No claim is made that this is Apple's algorithm.

## What stops, and what stays available

| Subsystem | While resting |
| --- | --- |
| Blob physics | Stops stepping on entry, eases existing deformation to the supported rest pose and resets once after settling. Touch/IMU wake restarts it. |
| Animation, skinning and rasterizer workers | Finish the sleep transition, then hold the finished framebuffer. No recurring 3D animation job. A separate small 2D sleep-overlay pass remains active on Home. |
| Clock and date | Redraw at each minute boundary, including midnight. The clock continues using wall time. |
| Necessary display changes | A status change or screenshot request can redraw. A visible running timer updates once a second and its deadline can wake the device. |
| Brightness control | Updates only when the curved transition changes the brightness command; stops writing after reaching the target. |
| Microphones and speaker | Codec suspend powers down the converters/amp and disables both shared I2S channels. Audio workers block on an event. Codecs resume at the existing format on wake. An active voice/audio session prevents automatic sleep. |
| CPU | Releases the avatar's 240 MHz lock. ESP-IDF scales to 80 MHz between work, returning to 240 MHz for interrupts/tasks. The compositor does not hold a continuous high-clock lock. The TE presenter retains its brief scan-timing waits to prevent tearing; RTOS work can still boost the clock. |
| Touch and six-axis IMU | Remain powered for wake. Touch has a separate GPIO38 data-ready task with bounded fallback polling (10 ms during contact, 40 ms awake/100 ms sleeping without contact, 500 ms during quiet monitor-bus deferral). IMU housekeeping retains its 16/32 ms cadence and freshness/recovery checks. Physical IRQ timing still needs a real swipe measurement. |
| Wi-Fi | Uses minimum modem sleep to preserve an existing connection. Automatic reconnect retries pause during sleep; an already-running connection attempt may finish. No periodic scan is introduced. |
| Bluetooth | No Bluetooth stack is initialized in this firmware. |
| USB control | Blocks waiting for commands; remains available for local tools and wake. |
| Timers | Keep monotonic deadlines and wake for an alarm. No work is suspended that would lose an alarm or sensor recovery. |
| RAM, PMIC, RTC and panel | Retain state and the displayed image. The panel stays on at 10%; its physical refresh rate is not changed. |

The IMU wake detector is separate from the visual jelly response. It accepts a measured force magnitude more than 0.055 g away from resting gravity, corrected angular speed above 0.12 rad/s, or a 0.026 g change from the resting acceleration vector (roughly 1.5° of slow tilt). It uses the measured accelerometer vector rather than the animation filter's gravity estimate, so startup bias correction does not masquerade as a pickup. A 60 ms low-pass removes accelerometer jitter, and the first 120 ms after startup or a stale gap establishes an averaged reference. The detector then requires 45 ms of consecutive evidence and retains activity for 250 ms. This rejects resting noise while detecting slow tilt as well as pickup. A stale sensor is not treated as proof of stillness; automatic sleep waits for fresh motion data. Separate real/simulated histories prevent diagnostic source changes from faking a pickup.

## Measurement and limits

`tests/sleep_power_check.py` records active and quiet task runtimes, CPU clock residency, stopped physics/audio counters, actual sensor progress, sleep/wake curves, six-axis injected wakes, slow tilt, interrupted dimming, codec resume, timer wake and a complete quiet timeout using the real IMU. It temporarily sets the clock to just before midnight to check a natural date/clock rollover while asleep, then restores Mac time. Captures show actual framebuffer pixels, not optical brightness.

`debug.power` is local USB instrumentation, excluded from the AI skill catalog. `system.status` exposes sleep, activity, transition output, worker counters and fresh sensor state. Asking for status wakes a CPU task, so its instantaneous `cpu_mhz` is **not** a measure of idle clock residency; use two power profiles. Runtime accounting uses the microsecond timer and remains valid across clock changes. Busy percentages are averages across both cores. Diagnostic floods must be excluded from an idle-power comparison.

Power-management bookkeeping uses internal RAM; render tiles remain 18 rows; the separate internal DMA staging halves are 12 rows to retain working memory. The screen and mesh still render at 410 × 502 native pixels.

These checks measure work and clock residency, **not current draw or battery runtime**. The attached USB device and live gyro/touch polling remain power costs. Deep/light sleep and interrupt-only sensor wake would require a separate hardware wake path; this release keeps USB and prompt pickup wake working. Existing intermittent digitizer faults and combined-load frame headroom are tracked in the integration report.

The bundled AXP2101 driver provides battery/VBUS/system voltage readings and a getter for the configured constant charging current. That charging-current setting is not a measurement of the board's load. No usable load-current readout has been established through this driver or the firmware's battery-voltage telemetry. Actual consumption therefore still needs an appropriate meter: measure the same brightness, connection and workload with frozen versus animated sleep overlays, keep battery charging state consistent, and report the measurement point. USB input power while charging is not directly battery runtime. Battery capacity and available meter information have been requested; neither is assumed.

Implementation references: [Espressif power management](https://docs.espressif.com/projects/esp-idf/en/v5.0/esp32s3/api-reference/system/power_management.html), [Wi-Fi modem sleep](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/wifi-driver/wifi-performance-and-power-save.html), and the bundled ESP-IDF 5.5.5 / `esp_codec_dev` implementations. Measured results and firmware hashes are recorded in [INTEGRATION-REPORT.md](INTEGRATION-REPORT.md).
