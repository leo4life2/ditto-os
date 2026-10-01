# Native-resolution renderer — September 19, 2026

This report records the test-robot checkpoint. The later Mecha Scout study was rejected; see `WORK-STATUS.md` for the installed development build and current visual direction.

**Historical yellow-robot checkpoint. These performance results do not describe the current Ditto asset.** Current evidence is indexed in `WORK-STATUS.md` and `LOCAL-ACCEPTANCE.md`. The old `ditto-slime-0.5.0` results and versioned `INTEGRATION-REPORT.md` sections also remain historical.

Native 30 FPS was achieved in this short combined test; the stricter headroom criterion remained unmet. The board renders live geometry at 410 × 502, with native text over it. It does not enlarge a smaller character image or play prerecorded animation frames.

The benchmarked RobotExpressive test asset has 1,040 triangles and 845 vertices. It is not the approved Mecha Scout model. Sharp pixels do not replace the final character's geometry, materials, or animation work.

## Measured on the board

Workload: simultaneous microphone capture, silent speaker playback, real Wi-Fi scans, changing animation clips, and repeated status commands. Statistics below cover the workload interval, before diagnostic screenshot collection.

| Measurement | Result |
|---|---:|
| Workload duration | 180.73 seconds |
| Delivered frames / average rate | 5,476 / 30.30 FPS |
| Frames within 33.333 ms | 5,473 / 99.95% |
| Frames within 25 ms | 3,437 / 62.76% |
| Required fraction within 25 ms | 95% — not passed |
| Median sampled frame processing time | 24.71 ms |
| Highest sampled rolling p95 frame processing time | 30.79 ms |
| Command round-trip p95 / maximum | 7.04 / 8.16 ms |
| Microphone / playback packets | 9,087 / 9,037 |
| Minimum sampled free internal RAM | 22,627 bytes |
| Minimum sampled free PSRAM | 5,767,656 bytes |
| Unexpected resets / command errors | 0 / 0 |
| Display / receive / transmit / overflow errors | 0 / 0 / 0 / 0 |

Frame rate uses frame and uptime deltas across the entire workload. Budget percentages use cumulative counters covering every frame. Sampled median and maximum rolling p95 are not a global latency percentile. Audio packet counts span producer/consumer startup boundaries and are not a synchronized packet-loss measurement. Playback was silent during the combined run; functional acceptance separately exercised a tone and microphone samples.

Diagnostic screenshots impose additional work and are outside the timed workload. The post-capture `final` status therefore includes extra slow frames; `baseline` through `measurement_end` is the benchmark interval. Do not quote its 30 FPS result as continuous screen-streaming performance.

## Retained changes

- Two workers dynamically claim 20-row tiles, with private color/depth buffers and native text composition. Only panel submission is serialized.
- Exact immutable material ramps replace repeated color multiplication. Row fades are applied during rasterization. The active shader is kept in instruction RAM; less-used shader paths remain separate.
- Renderer buffers are reserved before networking. Supported PSRAM instruction/constant caching and bulk allocations keep internal memory available for DMA and tasks.
- Full-frame capture is on demand. A requested frame is copied to stable screenshot storage after its display transfers finish; normal animation avoids that continuous PSRAM copy.
- Transfers obey even-coordinate panel windows. Per-row validity, old/new bounds, and dirty-region erasure handle movement and transitions between continuously animated and static content.
- The microphone task has an 8 KB stack after an earlier overflow was diagnosed. Task stack margins, per-core raster time, frame deadlines, resets, and audio/display errors are observable.

The earlier 26 FPS checkpoint is retained separately. A larger-tile trial that could not start the renderer, and another that starved Wi-Fi of internal RAM, were rejected. A per-frame lighting cache was slower and removed. No resolution, FPS, or visual-quality reduction was used to obtain these optimizations.

## Verification

- Hardware acceptance: **10/10 passed** — settings and validation; Wi-Fi scans and stale-result regressions; password edits; mic mute/resume; capture/playback; all 14 animation clips; navigation/conversation states; timer; sleep/wake.
- Raster equivalence: **23,051,840 pixels** across 112 frames / 14 clips, including poses spread across clip durations; reference serial, optimized serial, and dynamic two-worker results match.
- Row fade: all 65,536 RGB565 colors at 33 levels, **2,164,800 reference pixels**; in-raster and post-render fade results also match.
- Transfer: **100 frames / 2,311 aligned transfers**, including moving/clearing content, reverse tile order, static/continuous transitions, and delayed DMA. Requested captures and the modeled panel match exactly.
- Typography: **12 layouts** match reference blending and the 50-pixel text inset.
- Reboot: volume, brightness, mute, and time recovered; observed RTC error was −0.94 seconds.
- Idle/wake: **0.997 FPS** drawing when idle, five new frames during the wake check. This does not establish a 1 Hz physical panel refresh.
- HTTP bridge: **18 skills**, real setting changes and restoration, **410 × 502** capture, and cross-origin action rejection passed; reported audio/display errors remained zero.
- Digital captures of home, settings, and password correction were inspected. They cannot establish optical panel flicker, color accuracy, or physical readability.

## Remaining gates

The 25 ms headroom target, final approved character and visual polish, two-hour mixed reliability run, actual-panel review, secured Wi-Fi association/reconnection, and real voice integration remain. Scanning plus USB PCM does not demonstrate standalone encrypted cloud voice. Physical refresh switching and battery-life savings are unmeasured. Final-character performance must be measured again.

Firmware application SHA-256: `d9826facdab1861cb823ef04a672dde8b0ab9f0d7939fb3ac8632c583a160cc6`.

Evidence: `test-results/perf-pass-2/soak-final.json`, `final-metrics.json`, `acceptance-final.json`, and copied persistence/layout/bridge results in that directory. Earlier rejected experiments remain labeled separately. This is an intermediate hardware checkpoint, not completion of Scout OS.
