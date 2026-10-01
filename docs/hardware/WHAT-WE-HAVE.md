# What we have: Waveshare ESP32-S3-Touch-AMOLED-2.06

A hardware-first reference for our own embedded OS. Updated September 18, 2026 after running our own diagnostic firmware on the connected board. **Use measured results as the baseline, documented specifications as capabilities to validate, and explicit unknowns as open work.** Full conditions, raw evidence, failures and reproduction are in [Hardware probe results](HARDWARE-PROBE-RESULTS.md).

## Measured baseline

- **32 MiB flash and 8 MiB PSRAM confirmed.** The final 64 KiB of flash passed program/readback. Three pattern passes across 7.75 MiB of allocated PSRAM found zero errors. Our diagnostic started with about 8 MiB free PSRAM; the factory app used much of that memory for its own code/assets.
- **Two 240 MHz cores verified concurrently.** Internal heap was about 251 KiB free at diagnostic startup; usable RAM depends on our build and enabled services.
- **Display timing signal: about 59.7 Hz.** Full-frame direct PSRAM transfers reached about 45.5/s but failed under combined audio/Wi-Fi/BLE load. Internal staging completed 300 frames under that load at about 31.1/s without display errors. These are transfer measurements, not final UI ceilings or verified tear-free animation.
- **Touch, six-axis motion, PMIC, RTC, both microphones, speaker digital path, Wi-Fi and BLE respond.** The RTC is ticking but invalid; SD mounting timed out. Stereo microphone capture had no read errors or DMA queue overflows during the 21.85-second reference test.
- **Timed deep-sleep wake and retained memory passed.** Battery runtime and actual current draw remain unmeasured.
- **Safe area established from your photos:** the 30 px guide is clipped; the 40 px guide is fully visible. Use **40 px as the tested safe inset** and **50 px for everyday UI spacing**. That gives a 330 × 422 safe rectangle and a 310 × 402 default content rectangle. BOOT still toggles the calibration pages. [Photo measurements and exact bounds](safe-area/SAFE-AREA.md).


## Hardware inventory

| Resource | Available capability | Practical meaning |
|---|---|---|
| CPU | Two Xtensa LX7 cores, up to 240 MHz; single-precision FPU and SIMD | Concurrent UI, audio, sensors, and communication, with explicit scheduling budgets. |
| Internal memory | 512 KB SRAM, 384 KB ROM, 16 KB RTC SRAM | Usable internal RAM is smaller after code, stacks, drivers, and networking. |
| Working memory | 8 MB octal PSRAM | Large working buffers and assets in use; different latency/cache constraints from internal SRAM. |
| Persistent storage | 32 MB flash | Firmware, settings, assets, and possible update slots. **Flash is not additional RAM.** |
| Display | 2.06-inch, 410 × 502 AMOLED, CO5300 over QSPI | Custom graphics and brightness; account for rounded corners and bezel. |
| Touch | FT3168 over I2C | Taps/drags; our software can interpret gestures. |
| Motion | QMI8658 accelerometer + gyroscope | Tilt, shake, movement, orientation estimates; no magnetometer for absolute compass heading. |
| Clock | PCF85063 RTC with battery-backed supply arrangement | Timekeeping between active sessions; synchronization/timezone policy is ours. |
| Audio | Two microphones through ES7210; ES8311 codec and speaker amplifier | Recording/playback, sound effects, visualization, voice interaction. |
| Removable storage | MicroSD/TF; examples use one-bit SDMMC and FAT | Stream media/assets and save logs. |
| Wireless | 2.4 GHz Wi-Fi b/g/n, Bluetooth LE 5 | Mac/network connections; no 5 GHz Wi-Fi or Bluetooth Classic audio. |
| USB | Native USB Serial/JTAG and USB-OTG capability | Console, flashing, or custom USB device functionality. |
| Power and controls | AXP2101, battery connector, PWR and BOOT buttons | Battery/charging telemetry, power policy, sleep/wake, button actions. |
| Expansion | Reserved I2C/UART/USB pads; motor circuitry in schematic | Limited expansion and possible haptics; fitted parts still need checking. |

Sources: [Waveshare specifications](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-2.06), [product table](https://www.waveshare.com/product/esp32-s3-touch-amoled-2.06.htm), [chip datasheet](https://documentation.espressif.com/esp32_s3_datasheet_en.pdf), [schematic](https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-2.06/ESP32-S3-Touch-AMOLED-2.06.pdf). Chip peripherals are not necessarily exposed on this board. The PMIC detects a battery at about 4.12 V; its capacity is unknown. No usable SD card responded. Motor population and PCB revision have not been physically inventoried.

## Performance budgets

- **Graphics RAM:** RGB565 costs `410 × 502 × 2 = 411,640 bytes` per full frame (402 KiB); double buffering costs about 804 KiB. A 32-bit compositing surface costs another 804 KiB. Fonts, images, snapshots, and other services need additional memory.
- **Display traffic:** the reviewed BSP uses 40 MHz QSPI. Four data lines imply a theoretical 20 MB/s pixel-payload ceiling: about **48.6 full RGB565 transfers/s before overhead**. This is a payload ceiling, not measured UI FPS. Our tests measured about 45.5 full-frame transfers/s via direct PSRAM DMA when that path worked, and 31.1/s via internal staging under combined load. Small changed-region updates cost less; a 64 × 64 transfer took about 1 ms. [Driver configuration](https://github.com/waveshareteam/Waveshare-ESP32-components/blob/9daffb7168b3c093a330da700988a22092294eef/display/lcd/esp_lcd_sh8601/include/esp_lcd_sh8601.h).
- **Audio:** the spectrum demo uses two 16-bit channels at 16 kHz: 64 KB/s. Its 1,024-sample FFT spans 64 ms with 15.625 Hz bin spacing. These are example settings, not codec maxima.
- **Memory placement:** PSRAM is cached external memory. DMA, interrupt code, stacks, and flash writes have specific placement/cache constraints. Track internal/external free heap and largest free block separately. The demos already enable PSRAM. Our diagnostic had 8,386,192 bytes free at boot and about 7.97 MB free with display/audio/radio stacks initialized. This does not imply the same free heap for an OS with more services. Internal staging required an additional 52,480-byte internal buffer. [Espressif memory guidance](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/external-ram.html).
- **Compute:** graphics, decoding, DSP, and radio work share the CPUs. There is no desktop-class GPU. Small local processing is plausible; a modern conversational language model would need a Mac/server in the design we can realistically target.
- **Power:** brightness/content, radios, audio, and rails determine whole-board battery life. Chip deep-sleep figures do not predict board runtime.
- **USB:** OTG and Serial/JTAG share the internal PHY; our USB interface affects simultaneous debugging. [USB documentation](https://docs.espressif.com/projects/esp-usb/en/latest/esp32s3/usb_device.html).

## What the examples teach us

| Example | Reference behavior |
|---|---|
| Arduino 01 HelloWorld | Panel startup, QSPI, drawing, controller coordinate offset |
| Arduino 02 AsciiTable | Text/glyph rendering |
| Arduino 03 RTC clock | RTC access and timed UI updates; hard-coded time written at every boot |
| Arduino 04 IMU UI | Accelerometer configuration, acquisition, charting |
| Arduino 05 PMIC UI | Battery/USB voltage, charging state, temperature telemetry |
| Arduino 06 LVGL | Draw buffers, flush callbacks, touch input, widgets |
| Arduino 07 SD test | One-bit SDMMC mounting and directory listing |
| Arduino 08 audio | I2S, embedded PCM playback, echo-loop experiment |
| ESP-IDF 01 PMIC | Power-chip communication and charging/telemetry setup |
| ESP-IDF 02 LVGL | Minimal application using the board-support package |
| ESP-IDF 03 Brookesia | App lifecycle, navigation, styling, resource cleanup, snapshots |
| ESP-IDF 04 motion blocks | Calibration, motion-driven animation, rounded boundaries |
| ESP-IDF 05 spectrum | Microphone acquisition, Hann window, FFT, visualizer |
| ESP-IDF 06 video | SD media, JPEG frames in AVI, audio output, buffer swapping |

[Official example source](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.06/tree/b099739ad0e33b34e5fbaae77f02bd84805d79a3/examples). Separate examples do not establish simultaneous performance.

## What we can own in our OS

Boot experience, compositor/UI, app lifecycle, input routing, event system, storage layout, clock synchronization, audio/sensor services, power policy, settings, Mac communication, updates, and recovery behavior.

The examples run on ESP-IDF's **FreeRTOS kernel**. Brookesia supplies a C++ app/UI framework; its apps are objects in shared firmware, not isolated desktop processes. We can design our own upper layers. Replacing the kernel itself is a separate project covering startup, interrupts, context switching, synchronization, allocation, both cores, and driver/radio runtime integration. That scope decision remains open. [FreeRTOS reference](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/system/freertos_idf.html).

The first-stage boot code is immutable ROM. Flash bootloader/application layers are programmable within the device's boot/security configuration. [Boot flow](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/startup.html).

## Edges we should design around

- Several published demos configure **16 MB flash**, but the installed factory firmware already used **32 MB**, as does our diagnostic. This concerns storage configuration, not additional RAM. Our future update/storage partition layout remains a design choice.
- Current manifests request LVGL 9.5/BSP 2.x, while some dependency locks describe older versions. Resolve and pin one coherent toolchain before building.
- The BSP reuses drivers named SH8601/FT5x06 for the advertised CO5300/FT3168 with board settings. Comments contain references to other boards. Follow actual initialization and pin definitions.
- RTC, IMU, PMIC, and haptics need dedicated services beyond the main BSP. Its buffer and anti-tearing options require validation, not assumptions from their names.
- Arduino LVGL examples repeatedly transfer the whole screen. The video demo uses a 320 × 200 canvas and logs polling-loop iterations as “FPS.” Measure actual presentation rates ourselves.

## Saved material and remaining unknowns

**Saved:** 409 source/config/reference files, about 3 MB, verified against GitHub blob hashes. All 14 first-party entrypoints are present, plus application code, selected drivers, BSP 2.0.0, and its QSPI driver. [Manifest](download-manifest.json) · [code-level notes and pin map](SOURCE-REVIEW.md).

**Coverage:** all 14 entrypoints, relevant configs, BSP implementation, key driver interfaces, and Brookesia's app-management path. This is a source-review subset, not a complete buildable clone. The whole vendored framework/LVGL tree and generated media have not been read line by line.

**Factory preservation waived:** the user explicitly chose to skip a backup and does not need the factory firmware. Downloading or analyzing the vendor recovery image is no longer a prerequisite. The listed 28 MiB Xiaozhi image was not downloaded or analyzed. [Optional factory image](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.06/blob/b099739ad0e33b34e5fbaae77f02bd84805d79a3/FirmWare/ESP32-S3-Touch-AMOLED-2.06-xiaozhi-251104.bin).

## Measure our own limits

**Treat demo settings as starting points, not ceilings.** Measure what this exact board can sustain before choosing our budgets; record both useful capacity and the headroom required for reliability.

| Probe | What it should establish |
|---|---|
| Identify flash/PSRAM and boot configuration | Actual chip capacity, detected/mapped memory, firmware partition usage, and usable space beyond the demos' defaults |
| Measure heaps and allocations | Internal versus PSRAM total/free/largest block at boot and under load; fragmentation and task stack high-water marks |
| Benchmark drawing | Full/partial-screen transfer time, actual presented FPS, tearing, input-to-visible latency, and safe-area geometry |
| Exercise audio + UI + Wi-Fi/BLE together | Audio dropouts, frame-time percentiles, CPU load per core, and memory peaks rather than isolated-demo performance |
| Benchmark SD and flash access | Sustained throughput, slowest stalls, and their effect on playback/UI; use dedicated scratch data for write tests |
| Measure power and wake | Current at useful brightness levels and in sleep, wake latency, RTC retention, and battery runtime with the actual battery |
| Establish development access | Identify the board, confirm ROM download access, and keep a way to reflash our own builds; factory backup is explicitly waived |

Start with identification and read-only observations, then dedicated benchmark firmware. Factory backup and factory restoration are not required. For each result record firmware/toolchain, clock rates, buffer placement, test duration, and workload; repeat sustained tests before calling a limit real. Explore higher supported settings incrementally where evidence warrants it, without assuming either the sample defaults or theoretical maxima are the practical limit.

Hardware access is now working. We compiled and flashed our own diagnostic, completed the measurements above, then left the screen-calibration firmware installed. The user explicitly waived factory backup/preservation. The [detailed measurement report](HARDWARE-PROBE-RESULTS.md) records tests that passed, the direct-DMA failure, and remaining limits. [Device status](device-status.json) and the raw logs preserve the evidence. The measurement plan above remains a backlog for workloads requiring more time, a known SD card, visual observation, network setup, or external instruments—not a claim that every possible limit was measured.
