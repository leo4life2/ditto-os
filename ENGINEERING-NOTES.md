# Scout OS prototype

**Current engineering checkpoint: 0.9.33.** An exact-pixel lighting-table layout change improves mixed throughput about 1.5% over surrounding controls. The flashed image measured **30.19 FPS home / 29.80 injected drag / 27.97 mixed full-rate silent PCM and Wi-Fi scan**, with about **31.7 KB free internal RAM**. Native pixels and body physics matched exactly across 480 test frames; the layout adds 16,448 bytes of external PSRAM. The continuous translucent droplet and subtle body translucency are retained. Sustained 30 FPS/headroom and synthetic PCM timing remain unmet. Physical touch, secured networking, actual AI voice, final reliability and measured battery life also remain unfinished. See [WORK-STATUS.md](WORK-STATUS.md) for evidence and limits.

A voice-first device shell for the Waveshare ESP32-S3-Touch-AMOLED-2.06, with local live 3D and shared device actions. It runs on ESP-IDF / FreeRTOS: a custom OS experience and service layer, not a replacement kernel.

**The OpenAI connection is deliberately deferred.** No API key is needed for the firmware, tests, audio transport, or local bridge. Speech recognition and AI conversation are not implemented yet.

## Included

- Black AMOLED interface, a large live character behind native-resolution Outfit typography, a prominent clock, coral setting results, and a 50-pixel text inset. The character can overlap the UI; soft shading keeps the readouts legible.
- Touch physics: poke, pull, flick and stir Ditto. After 60 seconds without touch or movement it rests upright, fades to 10%, stops physics/audio work, animates only small sleeping overlays and refreshes the clock/date each minute. Touch, pickup or rotation wakes it with a fast brightness rise. See `POWER-AND-SLEEP.md`.
- Live Ditto avatar: a continuous rounded purple body, tiny face, smooth lighting, and color dithering. Sixteen semantic motions use breathing, squash/stretch, blinking, leaning and bouncing, with interruptible 280 ms transitions and gentle tilt response.
- Wi-Fi scanning, network/password drafting, precise password corrections, connection/reconnection with a bounded failure state, disconnect, forgetting credentials. The screen displays the conversation's result; no sliders, touch buttons, or touch keyboard.
- Volume, brightness, microphone stream mute, display sleep/wake; settings persist in NVS.
- Mac clock synchronization, Wi-Fi SNTP, and RTC recovery after reboot.
- Background timer, app navigation, completion event and tone. Timers continue across navigation but do not survive reboot.
- Full-duplex 16 kHz signed 16-bit PCM transport. The codec records stereo; the prototype forwards one mono channel.
- Action catalog, conversation-state injection, framebuffer capture, error/heap/frame telemetry.

The current avatar is a procedural Ditto requested for local prototyping. Its 1,816 triangles render at native resolution. `AVATAR-PIPELINE.md` explains the replaceable asset contract, continuous surface normals, color smoothing and live animation. The old Labubu source is preserved but is not installed.

## Use

Install `host/requirements.txt` in Python 3.12+, then:

```sh
python host/bridge.py
```

Open <http://127.0.0.1:8781> for the development console. It shows health, invokes shared actions, drafts Wi-Fi credentials, and captures the actual framebuffer. It keeps USB open, reconnects after unplugging or software reboot, and syncs Mac time every minute. Uncertain actions and queued speech are never automatically replayed. Stop it before flashing or opening another direct serial client. Opening this native USB port can reset the board; the persistent bridge avoids doing so for each action.

Screenshot transfers preserve one complete frame while the firmware continues processing audio and control requests between row packets. Only one capture can be active; a concurrent request returns a busy error. The console automatically captures a 410 × 502 screenshot on opening. Use **Capture device display** to refresh it or **Open original pixels** for the unscaled PNG. This reads the actual firmware framebuffer, including the live 3D model and native text. It is not an optical camera feed: panel tearing, flicker during transmission, and physical color/brightness differences cannot be diagnosed from the PNG alone.

The panel transfer path follows Waveshare's paired-pixel window rule: both start coordinates and exclusive end coordinates are even, including each DMA strip. Odd dirty rectangles are expanded while preserving adjacent pixels. This corrected the visible avatar flicker reported during hands-on review; the user confirmed it appeared gone after flashing the correction.

Animated frames are fully composed before presentation. An asynchronous presenter reads an immutable framebuffer, waits for the panel’s TE edge, and writes paired DMA strips behind its scan. DMA completion timestamps track the margin before the panel’s next pass. QSPI stays at 40 MHz. Framebuffer captures cannot independently prove the absence of physical tearing; the hands-on confirmation is recorded with the test evidence.

The bridge binds to loopback, rejects cross-origin browser actions, and does not log passwords. `/skill` uses the future model's action catalog. `/action` also includes development actions.

```sh
curl -s http://127.0.0.1:8781/skill \
  -H 'Content-Type: application/json' \
  -d '{"name":"audio_volume_set","arguments":{"percent":35}}'
```

Direct USB, with the bridge stopped:

```sh
python host/device.py system.status
python host/device.py avatar.emote '{"name":"Wave"}'
python host/device.py --screenshot display.png
```

## Build

Builds now seal source/configuration and firmware content hashes before flashing; stale timestamps cannot silently retain old experimental objects. Runtime version comes from the application descriptor, using the root `VERSION` file. The external SDK is identified by its recorded path/version; this is build consistency, not complete SDK attestation.

The workspace has ESP-IDF and tools in sibling `esp-idf` and `idf-tools` folders. Elsewhere, install ESP-IDF 5.5.5 and set `IDF_PATH` / `IDF_TOOLS_PATH`.

```sh
bash build.sh
bash build.sh -p /dev/cu.usbmodemYOUR_DEVICE flash
```

Rediscover the port on another machine. Python auto-discovery selects only the Espressif USB Serial/JTAG device, never the WorkLouder keyboard. Generated assets are included, so firmware builds need no Blender. Regenerate the current avatar with ordinary Python using `tools/prepare_ditto.py`; see `assets/avatar-manifest.json` for mesh counts. Ditto is actual live 3D geometry, with no prerecorded animation images or video on the board.

Font regeneration is optional: `python tools/prepare_fonts.py` requires Pillow and uses the included font sources. If regenerating fonts, do it before building. `build.sh` uses the current user's bundled Python when present; otherwise ESP-IDF selects the installed Python. `SCOUT_PYTHON_BIN` can override that optional interpreter directory.

## Platform seams

| File | Responsibility |
|---|---|
| `main/board.cpp` | Display, I2C, codecs, RTC, motion samples |
| `main/services.cpp` | Validated actions, settings, Wi-Fi, timer, shared state |
| `main/ui.cpp` | Native text and live 3D composition |
| `main/main.cpp` | USB newline-delimited JSON requests/events |
| `host/skills.py` | Provider-independent callable action definitions |
| `host/voice_transport.py` | Actual audio/action boundary for a voice adapter |

Apps are currently compiled into the firmware. [APP-DEVELOPMENT.md](APP-DEVELOPMENT.md) traces the working Timer app and gives the exact extension points for another app. Apps share the character, audio, time, settings, and network state. Dynamic installation, sandboxing, and downloadable modules are outside this prototype.

The current renderer draws actual **410 × 502 native-resolution geometry**, using two CPU workers and small internal color/depth tiles. The character is not enlarged from a smaller image. Hidden rows are skipped without changing the projection. Native text is composed above the geometry, and aligned dirty regions stream through two internal DMA buffers. Row bounds include both old and new content so moving geometry is correctly erased. The capture shadow and a stable screenshot buffer live in PSRAM. A screenshot request records a complete rendered frame after its DMA transfers finish; continuous animation does not copy every frame into the capture shadow. No prerecorded frames or video are used.

The current Ditto remains at **native 410 × 502** resolution. `INTEGRATION-REPORT.md` records the measured combined workload and exact firmware identity. Current focused-tap evidence is isolated in `test-results/ditto-sharp-taps-0.6.1/`; the older `ditto-integration/` two-hour run applies to 0.3.2. The current report keeps new measurements separate from that earlier qualification.

The renderer uses 18-row tiles reserved before networking initializes, dynamically distributes tiles between the two cores, and composes text in each worker’s private tile. Immutable material ramps and in-raster fades preserve RGB565 output. Code/constant data, bulk PCM, and larger ordinary allocations use PSRAM; display/depth/DMA buffers stay internal.

USB command processing runs on core 1 so incoming audio/control traffic does not repeatedly preempt the display compositor on core 0. Audio tasks have priority over 3D work.

The final voice adapter should use `VoiceTransport.skill`, `read_audio`, `begin_speech`, `write_audio`, `interrupt`, and `set_state`. Each generated response owns a token from `begin_speech()`; pass the original token on every `write_audio(pcm, speech_id=token)`, `skill(name, arguments, speech_id=token)`, and response-driven `set_state(state, caption, speech_id=token)` callback. Interruption clears playback and invalidates that token. Audio returns `False` when canceled; actions and state callbacks raise `ResponseCancelled`. Suppress the canceled response rather than retrying it with a new token. Ownership is checked again inside the serial write lock, so callbacks waiting to send cannot slip through cancellation. Commands already sent may have taken effect: retain their observed result or report uncertain completion on disconnect; cancellation does not undo them. The ownership lock is released before waiting for a device reply. Token omission is reserved for synchronous local tests and host-managed state transitions, never asynchronous provider callbacks. Share one `ManagedDevice` with the console (the bridge accepts `serve(device=...)`), rather than opening USB twice. The transport maintains a two-second heartbeat; firmware stops microphone forwarding and clears queued playback if the voice lease expires after eight seconds. Reconnection requires an explicit new voice session. Simple controls and reasoning-model actions go through the same dispatcher. A successful `wifi.connect` reply only means the attempt began; wait for `wifi.connected` or observed status before claiming it joined. After two authentication failures or a 30-second unavailable-network period, `wifi.failed` and `wifi_error` give an explicit result. Background network completion does not navigate away from another app.

## Boundaries

- Active animation targets 30 FPS. After 60 seconds without touch or motion, it freezes the body and updates small sleep overlays at a nominal 12 Hz, with minute clock/date updates. Physical 1 Hz panel switching and battery-life savings are unmeasured.
- Text stays inside the measured 50-pixel inset. Framebuffer QA verifies digital layout; it is not a photograph of the panel.
- Short BOOT press returns home; holding for over 0.7 seconds toggles microphone stream mute. Mute suppresses outgoing samples, not microphone codec power.
- Wi-Fi is 2.4 GHz. Password correction currently uses ASCII byte positions. Draft text is visible for correction and cleared after submission. Saved credentials use SDK NVS. Secure Boot and flash encryption remain off, as on the probed device.
- No network credentials or API key were supplied. Secured Wi-Fi association and cloud conversation need those final inputs; actions and audio are exercised independently.
- The current audio/AI transport uses USB to the Mac bridge. A standalone cloud voice path over the board's Wi-Fi remains a separate integration step if the Mac is removed.
- No Bluetooth settings, app store, OTA updater, haptics, or battery percentage. Voltage is available; capacity and discharge remain unmeasured.

## Verification

Current focused host checks:

```sh
python tests/input_buffer_check.py
python tests/json_output_check.py
python tests/pcm_base64_check.py
python tests/bin_rounding_check.py
python tests/partial_transport_check.py
python tests/blit_transport_check.py
python tests/voice_interrupt_check.py
python tests/voice_session_check.py
python tests/voice_action_check.py
python tests/voice_boundary_runner_check.py
python tests/usb_ownership_check.py
python tests/recovery_host_check.py
python tests/frame_budget_check.py
python tests/profile_runner_check.py
python tests/package_release_check.py
```

Run actual-device checks **sequentially with the bridge stopped**, using a fresh evidence directory for each candidate. Touch, interface and sleep checks now read the expected firmware version from root `VERSION`; `SCOUT_EXPECTED_VERSION` can explicitly override it. `SCOUT_EVIDENCE_NAME` selects their output directory. They inject actions and do not establish actual fingertip latency, acoustic quality or secured Wi-Fi operation.

```sh
python tests/frame_profile.py test-results/current-profile.json
python tests/touch_response_hardware_check.py
python tests/ui_flow_hardware_check.py
python tests/sleep_overlay_hardware_check.py
SCOUT_EVIDENCE_NAME=current-audio python tests/audio_interrupt_hardware_check.py
python tests/voice_boundary_hardware_check.py test-results/current-voice-boundary.json
SCOUT_EVIDENCE_NAME=current-soak SCOUT_MOTION_LOAD=1 python tests/touch_soak.py 7200
```

The extended soak refuses to overwrite an evidence directory, checks the sealed source/version, and records sample journals plus a live process checkpoint. It requires continuous microphone delivery and full-rate silent playback, detects resets and presentation stalls, and compares quiet heap use after warm-up and cleanup. All touch errors stay visible; only explicitly counted quiet deferrals are distinguished from failed active samples. A failed or interrupted run stays failed. This synthetic workload does not qualify physical fingers, secured association, cloud conversation, battery life, or the performance target.

Both profile and soak also reject delivery pauses hidden by average PCM rate: a source start more than 20 ms late or an acknowledgement gap over 40 ms fails the synthetic timing budget. Bounded outliers record source lateness, call duration and acknowledgement gaps without PCM content. These are host/USB observations, not measured acoustic underruns. `tests/audio_cadence_check.py` covers deterministic pause/burst cases; the orchestration check injects a pause and verifies failure despite approximately 50 packets/s.

`Device` requests a POSIX exclusive advisory serial lock, so a second cooperating Scout reader cannot consume the same replies. The lock releases on close or process exit. This does not exclude unrelated applications or flash tools that do not honor the lock; keep sole USB ownership throughout testing/flashing. `usb_ownership_check.py` exercises real kernel locks and the production transport on a virtual serial port, without opening the ESP32. The hardware boundary check also tests competing Scout opens on the actual device.

The short profile verifies a full-rate silent 16 kHz PCM source, actual device playback and microphone delivery. It records firmware/host identities, refuses existing result paths and reports FPS without declaring the 30 FPS target passed. Cumulative frame counters separately report whether at least 95% of timed UI frames stayed within 25 ms and 99% within 33.333 ms. These timings include scheduling and presentation handoff waits; the mean of rolling p95 snapshots is not a whole-window percentile. A completed workload can still miss the headroom target. Native checks compare the production codec with SDK mbedTLS, complete rendered pixels with the old rounding path, and real transport code with a deferred-DMA panel. JSON results and captures are saved in `test-results/`. Historical wrappers and reports remain for their original versions; `tests/run_local_suite.py` is an old Labubu-era recipe, not the current release gate. A final two-hour run, real-network/provider review and current measurement remain separate requirements in WORK-STATUS.md.

See `WORK-STATUS.md` for current measurements and remaining work. `NATIVE-RENDER-REPORT.md`, `TEST-REPORT.md` and the versioned sections of `INTEGRATION-REPORT.md` retain historical measurements; they do not qualify a later build. See `THIRD-PARTY.md` for provenance.

`LOCAL-ACCEPTANCE.md` maps each local requirement to its evidence and remaining review. `tests/wifi_secure_check.py --ssid NAME` is the prepared secured-network test; it prompts locally for the password and must run with sole USB ownership. It is not counted as passed until supplied credentials establish a real connection.

The 0.5.0 slime update introduced local skin stretching, downward squish and delayed rebound. Later changes replaced its expression policy with coordinated dot eyes and the approved deadpan reactions; current behavior is described in `INTERACTION-DESIGN.md` and `POWER-AND-SLEEP.md`.

## Export a reviewable checkpoint

Use a new output directory outside this project. Select completed evidence explicitly; no evidence is attached by default, and the exporter rejects reports still marked running. For example, after reviewing these particular results:

```sh
python tools/package_release.py ../../../outputs/scout-os-checkpoint-0.9.17 \
  --evidence test-results/touch-response-0.9.17 \
  --evidence test-results/mixed-touch-preflight-0.9.17
```

The exporter checks the build seal, firmware hashes, every bundled source/evidence file and concurrent file changes before publishing the directory. It refuses to replace an existing package. `checkpoint.json`, `firmware/manifest.json`, `source-files.json` and `evidence-index.json` identify the exact snapshot and attachments. This is an engineering checkpoint: packaging never declares the product or its tests complete. Read `LOCAL-ACCEPTANCE.md` and `WORK-STATUS.md` before using it.

`tools/finalize_validation.py` and `tools/summarize_checkpoint.py` are historical wrappers with old report/layout assumptions. Do not use them to close the current qualification or update the preview; review the current runner's final report and use the explicit exporter above.
