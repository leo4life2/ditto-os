# Hardware probe results — September 18, 2026

Measured on our **Waveshare ESP32-S3-Touch-AMOLED-2.06** over native USB. This is the measured baseline for our OS, with explicit test conditions and unknowns. A short bench run establishes working configurations, not every component’s maximum or long-term reliability.

**State left on the board:** our calibration firmware, showing inset guides. **BOOT** toggles the numbered coordinate grid. Factory firmware was replaced, as authorized; no factory backup was required. No eFuses, security settings, charger limits, or rail voltages were changed.

## Directly established

| Resource | Result | Meaning for our OS |
|---|---|---|
| CPU | ESP32-S3 rev 0.2, two cores running at 240 MHz | Both cores ran matching 5-million-iteration integer workloads concurrently: about 126 ms per core and 126 ms total. This is a synthetic workload, not an application benchmark. |
| Flash | 33,554,432 bytes; JEDEC `C8 40 19` | The final 64 KiB, at `0x1FF0000`, passed erase/program/readback with zero errors. This establishes access beyond 16 MiB; it is not an exhaustive test of every flash cell. |
| PSRAM | 8,388,608 bytes detected at 80 MHz; 8,386,192 free at diagnostic startup | Three address-dependent pattern passes over 8,126,464 allocated bytes found zero errors. Startup SDK memory testing also passed. |
| Internal heap | 256,703 bytes free; largest block 131,072 | A 65,536-byte allocated region passed three pattern passes. Firmware, driver, and stack memory was not overwritten for testing. |
| Display | 410 × 502 RGB565 transfers; timing signal about 59.72 Hz | TE is wired to GPIO 13. This measures the panel timing signal, not the visible correctness or tear-free presentation of every frame. |
| Touch | Controller at `0x38`; 500/500 reads succeeded, averaging 457 µs | Reset GPIO 9 before discovery. It did not acknowledge before explicit reset in the first run. No physical touch coordinates were validated. |
| Motion | QMI8658 at `0x6B`, identity `0x05`; 883 samples in 2.00 s, zero read errors | Six-axis acquisition works. Joint accel/gyro mode used ±8g, ±512°/s and nominal 448.4 Hz. Stationary calibration remains necessary to establish bias. |
| RTC | PCF85063 at `0x51` ticked forward | Its oscillator-stop flag was set and calendar read January 1, 2000. We must treat time as invalid until synchronized; no clock setting was changed. |
| Power | AXP2101 at `0x34`, identity `0x4A` | PMIC reported a battery at about 4.12 V, USB around 5.21 V, system around 4.36 V. Battery capacity/runtime was not measured. |
| Audio | ES8311 `0x18`, ES7210 `0x40`; stereo 16-bit capture at configured 16 kHz | 349,696 samples/channel over 21.85 s, zero read errors and zero RX queue overflows. Both channels contained varying nonzero signals. |
| Speaker path | Codec open and 16,384-byte quiet-tone write succeeded | Digital playback path tested; audible output, distortion, and maximum volume were not independently confirmed. |
| Radios | Passive Wi-Fi and BLE scans returned results | No network was joined. Nearby identities and microphone audio were not retained. Range and network throughput remain unmeasured. |
| SD | Mount attempt timed out; no formatting | No usable card responded. This cannot distinguish an absent card from wiring/card issues; no SD capacity or throughput claim. |
| Security | Secure Boot and flash encryption off; ROM loader works | We can flash our own firmware. No security fuses were modified. |

## Measured performance and the important failure

Later prototype testing exposed a separate display constraint: every CO5300 update must use paired pixel coordinates, following Waveshare's BSP rounder callback. Start X/Y and exclusive end X/Y must be even, including each DMA strip. Arbitrary dirty rectangles caused visible avatar flicker despite successful transfer return codes and clean framebuffer captures. Expanding the windows while preserving border pixels corrected it; the user reported the flicker appeared gone after flashing that fix. A logical framebuffer capture cannot establish optical timing or absence of tearing.

The working OS prototype and its current animation measurements are documented in [Scout OS test report](../../work/esp32-amoled/scout-os/TEST-REPORT.md). Rendering rates depend on the model, compositing, active services and transfer strategy; the probe rates below remain measurements of their stated workloads.

| Workload | Result |
|---|---|
| Internal RAM memcpy, 32 KiB buffers | 379 MB/s |
| PSRAM memcpy, 1 MiB buffers | 22.34 MB/s |
| PSRAM scalar sequential read / write, 1 MiB buffers | 60.60 / 34.89 MB/s |
| Flash read, 2 MiB in 32 KiB blocks | 12.62 MB/s |
| Final 64 KiB flash block erase / program | 96 / 116 ms in the reference run; erase timing varied between runs |
| Full-screen direct PSRAM DMA, radios off | 45.46 transfers/s, about 22.0 ms each |
| Full-screen direct PSRAM DMA plus microphone capture and Wi-Fi scan | 45.51 transfers/s in the short test |
| Full-screen direct PSRAM DMA plus audio, Wi-Fi and BLE | **Failed with DMA TX underflow in the first run** |
| Full-screen staging through 52,480-byte internal buffer, radios off | 31.25 transfers/s |
| Same staging with audio, Wi-Fi and BLE active | **300 frames, 0 errors; 31.10 transfers/s**, 95th percentile 33.0 ms; maximum 34.0 ms |
| 64 × 64 partial update from internal DMA memory | About 1.00 ms per completed transfer |

Rates use decimal MB/s. Memory timings exclude deliberate scheduler yields; cached 4 KiB PSRAM tests ran much faster and **do not represent external-memory bandwidth**. Copy rates count payload bytes, not both memory reads and writes. Graphics timings include our unoptimized synchronous API/scheduler overhead, with no rendering work or TE synchronization; they are not animation FPS or final throughput ceilings.

The direct-DMA failure also exposed an SDK cleanup concern: the LCD I/O path can retain an in-flight count after consuming a failed SPI transaction, potentially blocking later display calls. We reset before the next run and tested internal staging. We did not patch the SDK or declare direct PSRAM DMA reliable under contention. For our first compositor, internal DMA staging and changed-region updates are the proven starting point.

With display, audio and both radio stacks initialized, the sampled free heaps were **130,559 internal bytes** and **7,971,580 PSRAM bytes**. That snapshot precedes allocation of the additional 52,480-byte staging buffer. It is not the worst-case free heap. The allocator’s `internal_total` field includes reservation/accounting effects; do not interpret it as physical SRAM capacity. Heap integrity passed after the combined workload.

All display tests used 40 MHz QSPI. The [CO5300 datasheet, page 107](https://files.waveshare.com/wiki/common/Co5300_Datasheet.pdf) specifies a 20 ns minimum write clock period. The standard 80 MHz APB SPI divider gives 40 MHz as its highest setting within that timing. We did not blindly try 80 MHz. The panel’s roughly 60 Hz scan and full-frame transfer speed are separate budgets.

## Sleep and restart

- Light sleep returned `ESP_OK`: requested 250 ms, measured 241.60 ms. Its wake cause was not retained, so this does **not** establish precise timer accuracy or prove that only the timer woke it.
- Deep sleep: one-second timer wake verified; **64 bytes of retained test data survived with zero errors**. Time from entry request to application entry was 1.568 s, including normal boot and PSRAM startup. That is not bare hardware wake latency.
- macOS serial reattachment could reset the board during wake boot. The follow-up preserved results in RTC no-init memory and delayed reopening USB. This is a development-connection behavior to account for in our power service.
- These are functional sleep tests. No current meter was attached; sleep current and battery life remain unknown. SoC temperature rose from roughly 33°C to 43°C during the reference workload; this is not a thermal stress qualification.

## Screen geometry calibration

The firmware now shows a 10 px grid with 50 px major lines and colored rectangular inset guides at **0, 10, 20, 30, 40, 50, 60 and 80 px**. Press **BOOT** to switch between guides and the numbered grid; white numbers are X, cyan numbers are Y. Coordinates start at the logical top-left.

The smallest rectangle that remains fully visible at all four corners gives a conservative rectangular content boundary. An inset `d` means `x = d … 409-d`, `y = d … 501-d`. We should also inspect each edge/corner independently; bezel loss may be asymmetric. Add typographic breathing room after establishing the physical boundary.

**Photo calibration completed:** your two supplied photos show the 30 px guide clipped at the corners and the 40 px cyan guide fully visible. Adopt **x 40–369, y 40–461** as the tested conservative safe rectangle (330 × 422), and **x 50–359, y 50–451** as the default UI rectangle (310 × 402). The 10 px difference is breathing room, not measured clipping. The minimum uniform inset is bracketed above 30 and at or below 40 px; exact corner radius and a per-pixel visibility mask remain unmeasured. [Photos, interpretation and reusable geometry](safe-area/SAFE-AREA.md).

## Remaining capabilities and boundaries

Documented but not electrically exercised: USB-OTG modes, expansion pads, motor/haptic output, external wake sources, RTC backup retention across complete power loss, and the low-power coprocessor. The schematic includes a motor driver and additional audio/reference circuitry; this does not prove every optional part is populated or that echo cancellation is implemented.

Also unmeasured: touch accuracy/gesture latency, tearing and perceived display quality, SD performance with a known card, radio transfer throughput/range, audio rates beyond this 16 kHz test, acoustic quality, sustained stress/fragmentation, whole-board power, and battery capacity. These are explicitly open, not artificial ceilings.

## Evidence and reproduction

- [Reference workload measurements](diagnostic-v1.1.json), [full serial log](diagnostic-20260918-194750.log).
- [First run showing the DMA failure](diagnostic-20260918-194324.json), [failure log](diagnostic-20260918-194324.log). Its CPU launch timing was sequential; use the corrected second run for concurrency. Its later console output was incomplete; the second run installed buffered USB logging.
- [Sleep/grid measurements](diagnostic-20260918-195240.json), [sleep/grid log](diagnostic-20260918-195240.log).
- [Buildable diagnostic source](../../work/esp32-amoled/diagnostic/), ESP-IDF **v5.5.5** commit `b774170ff46c393eeb5e495ea37936038d3f4f4f`; codec component **1.3.6**; board panel source pinned in [source notes](SOURCE-REVIEW.md).
- [Archived workload firmware and source](diagnostic-v1.1/). Build configuration: CPU 240 MHz, flash QIO 80 MHz / 32 MiB, octal PSRAM 80 MHz, code/rodata in flash, 64 KiB internal DMA reserve. Whole-frame RGB565 buffer: 411,640 bytes in PSRAM.
- [Target-checked runner](run_diagnostic.py) restricts flashing to the identified board’s USB serial; [original ROM identification](probe-results-latest.json).

No factory image download or backup was a prerequisite. These probes use ESP-IDF/FreeRTOS as a measurement harness; they do not commit our future OS to its application framework or settle whether we replace the kernel.
