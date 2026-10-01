> Historical report for the earlier enlarged-character checkpoint. Current Ditto status and acceptance are in [WORK-STATUS.md](WORK-STATUS.md) and [LOCAL-ACCEPTANCE.md](LOCAL-ACCEPTANCE.md); this page retains earlier robot evidence.

# Scout OS hardware test report

Tested on the connected Waveshare ESP32-S3-Touch-AMOLED-2.06 on September 18, 2026. Final firmware uses the large background character and native text overlays. OpenAI voice remains disconnected by request.

## Result

The prototype builds, flashes through automatic USB reset, boots, renders live 3D, executes shared device actions, and exposes local audio/action and screenshot interfaces. The downloaded RobotExpressive is a test character, not a finished recreation of the approved Mecha Scout concept art.

- **10/10 acceptance groups passed** on the final firmware: Mac clock synchronization; settings and invalid-input boundaries; Wi-Fi scan/password edits; microphone capture/mute/resume; speaker playback plus microphone recording; timer completion/cancellation; app navigation/conversation states; 14 live animation clips; display sleep/wake; final health.
- Final combined workload ran **60.5 seconds** after warm-up, with **3,075 microphone packets** and **3,024 silent speaker packets**, recurring Wi-Fi scans, gestures, and status calls. The microphone count includes queued warm-up audio. There were **zero transport exceptions, display errors, codec read/write errors, or microphone overflow events**.
- Median reported animation rate was **30.13 FPS**; the lowest sampled one-second rate was **29.61 FPS**. The device completed **1,821 frames**, of which **92 (5.1%) exceeded 33.33 ms** during the measured workload. This is approximately 30 FPS with modest headroom, not a locked frame rate or a generous performance reserve.
- Once-per-second frame-work samples had a median of **32.41 ms**, 95th percentile **33.14 ms**, and maximum **33.55 ms**. These samples do not capture every frame or the true worst frame. The highest sampled rolling 120-frame p95 was **34.72 ms**.
- USB command acknowledgement latency: p95 **5.52 ms**, maximum **6.78 ms**. This is not optical response latency.
- Minimum sampled internal heap: **32,783 bytes**; PSRAM free at measurement end: **7,785,016 bytes**. A direct TLS/cloud connection on the device needs separate memory and performance validation.
- The final layout check measured a median **30.30 FPS** and checked home, volume, brightness, and password correction captures. Settings were restored after testing. Starting a fresh Wi-Fi scan clears an old draft so the results can appear.

## Display correction and visual verification

The panel requires even start coordinates and even exclusive end coordinates, including every DMA strip. The transfer code now expands unaligned rectangles while preserving their surrounding pixels. The user confirmed that the visible avatar flicker appeared gone after the correction. The panel driver also propagates transfer errors rather than silently discarding them.

Actual 410 × 502 firmware framebuffer captures were inspected for the large clock, readable settings, the 50-pixel text inset, password correction highlighting, character overlap, and expressions. The robot fills much of the screen behind the text instead of occupying a separate reserved viewport. Native Outfit typography stays sharper than the lower-resolution character rendering.

A framebuffer capture records the pixels composed for transmission. It does **not** measure optical flicker, tearing, bezel clipping, physical brightness, or panel color. The physical flicker confirmation came from the user; no optical camera measurement or TE synchronization claim is made.

## Rendering configuration

ESP32-S3 at 240 MHz, 32 KB instruction cache, 64 KB data cache, and 80 MHz octal PSRAM. Panel QSPI remains at the validated 40 MHz. Core 1 renders **164 × 200** live scenes and passes completed buffers to core 0. Core 0 performs bilinear reconstruction, adds native-resolution 410 × 502 text, and streams aligned changed regions using two internal DMA tiles. Buffer ownership prevents the renderer or compositor from overwriting data still in use. Audio tasks take priority; USB command handling runs on core 1.

The model has **1,040 triangles, 845 vertices, 14 body clips, and three facial morph targets**. Animation blends between poses locally; there are no prerecorded videos or animation-image sequences. The small render resolution and simple model visibly limit fidelity compared with the concept images.

## Additional checks

- Reboot persistence recovered volume 42, brightness 61, and microphone mute; original settings were restored. RTC time after reboot was **0.3 seconds behind the Mac**. This check ran on the same rendering build before the final, unrelated Wi-Fi rescan-draft correction.
- After 60 seconds without interaction, idle drawing measured **0.996 FPS**. Waking produced six frames in roughly 0.2 seconds. Physical panel refresh rate and battery consumption were not measured.
- The speaker emitted a 660 Hz test tone that was recorded by the board microphone. Spectral analysis found a **656.25 Hz** peak at the analysis resolution, with **98.77%** of window energy in the tone band. This demonstrates acoustic output/input, not speaker fidelity or far-field voice performance.
- Host compositor regression: **6,174,600 output pixels** matched the direct four-sample bilinear reference exactly, including black spans and edges.
- Host display-window regression: **10,000 rectangles**, including **9,351 unaligned inputs**, produced correctly aligned transfers with preserved border pixels.
- The actual tile-transfer code, tested against a delayed-DMA panel mock, passed **100 complete frames and 1,682 aligned transfers**, including no in-flight buffer reuse and exact capture-shadow/panel output equality.
- Optimized native glyph rendering matched the original per-pixel reference across **12 tiled layouts**.
- The lighting cache previously matched its uncached baseline for **112 host-rendered frames across 14 clips**.
- The local bridge exposes **18 skills**, passes setting/readback/restore, returns the actual framebuffer PNG, and rejects cross-origin browser action requests. It remains a local development process; no automatic launch service is installed.

## Not established

- Successful secured Wi-Fi association, credential persistence across a real AP reconnect, internet SNTP synchronization, and recovery from an AP outage: no network credentials supplied. Nearby scanning and draft/edit/action interfaces were tested.
- Speech recognition, model tool selection, conversational latency, cloud interruption behavior, or API billing: no API key supplied. Conversation states and skills were invoked directly.
- Standalone cloud audio over the board's Wi-Fi. The current audio/AI integration boundary is the USB Mac bridge.
- Battery life, physical 1 Hz panel switching, thermal endurance, long-duration soak reliability, optical display measurements, speaker fidelity, or microphone far-field performance. The final combined test lasted one minute, not hours.
- Physical BOOT-button interaction and manual tilting were not mechanically actuated by the scripts. Their handlers are present; motion samples were established in the earlier hardware probe.

## Evidence

- `test-results/acceptance.json`, `soak.json`, `layout-profile.json`
- `test-results/persistence-idle.json`, `bridge-check.json`
- `test-results/acoustic-check.json`, `acoustic-retest.wav`
- `test-results/layered-home.png`, `layered-volume.png`, `layered-brightness.png`, `layered-wifi.png`, and `expression-*.png`
- `build-latest.log`, `flash-latest.log`
- Regression implementations in `tests/display_window.cpp`, `compositor_check.py`, `transport_check.py`, and `text_check.py`

Firmware SHA-256: `e18b75863489e7ec07ced3c99d159fe1ea6b249100cacb2d807a884e2415458c`.
