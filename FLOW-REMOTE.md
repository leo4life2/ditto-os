# Ditto Remote — Flow controls and microphone

Firmware **0.11.56** and helper **0.3.0** implement USB/Bluetooth gestures, encrypted wireless microphone transport, and persistent battery diagnostics. Dictation and Enter do not require a readable focused text field; Flow may use its scratchpad. Password/secure input and focus-change release remain guarded. Shake Undo requires a readable, unchanged insertion in the same field.

**Mac setup pending:** BlackHole’s license agreement awaits approval; its installer requires admin authentication and declares a restart. Flow still selects Ditto USB. Bluetooth audio packets have been verified, but the virtual microphone-to-Flow path and physical unplugged dictation still need acceptance. See `WIRELESS-AUDIO-AND-BATTERY.md` and `test-results/wireless-battery-0.11.51/RESULTS.md`.

## Using the helper

Open `../../../outputs/scout-remote/Scout Remote.app`. Its display name is **Ditto Remote**; the existing bundle path and `local.scout.remote` identifier are preserved. It lives in the menu bar, with a softly shaded illustrated Ditto app icon and a matching traced monochrome silhouette. Its center shows a plug for USB, the Bluetooth symbol for wireless, or a dash when disconnected. The symbol follows accepted device input and the existing 350 ms stale-input timeout; enabling Bluetooth alone does not show a connection. Pausing keyboard control preserves the connection indicator. The compact menu contains remote enable/pause, Bluetooth, the test window, Battery Report, permissions and Quit. Closing the test window leaves the helper running; Quit stops it and any USB bridge it owns.

Grant Ditto Remote Accessibility for keyboard control, and Bluetooth for wireless gestures. The user refreshed the grant for 0.2.5 by re-adding the updated app; macOS may still list its bundle filename Scout Remote. Future ad-hoc rebuilds can require another grant refresh. No privacy database resets or permission bypasses are used. Keyboard control starts off on a fresh install. On later launches it restores the saved enabled/paused value only with Accessibility and fresh device input, after initializing gesture history. Bluetooth and target scope persist. No login item was added.

1. Leave **Scratchpad only** selected, connect Ditto, enable keyboard control and focus the blank test area.
2. Hold a stationary touch for 240 ms to hold Control–Option. Release to let Wispr Flow insert its transcription.
3. Double-tap for Return. This now works independently of whether Flow just inserted text, including after Undo or edits. The old post-insertion guard was mistakenly blocking Return even when the tap gesture was recognized.
4. A light back-and-forth shake sends Cmd-Z only when the last detected insertion is still unchanged in the same field. It never sends Select All/Delete. The editor determines its undo grouping.
5. Select **Other focused apps** and re-enable control when ready to use it outside the scratchpad.

Undo sensitivity in helper 0.2.5 uses gravity-removed acceleration peaks of 0.40 g (previously 0.85 g), two opposing direction reversals within 1.10 seconds, and up to 0.50 seconds between peaks. The two-second cooldown and unchanged-insertion/focused-field guard remain. Sampled-motion self-tests cover 144 cadence/axis/phase combinations plus five no-Undo motion scenarios; physical effort is still a subjective user check.

The helper uses the user's existing Control–Option Flow shortcut and does not call a private API. During microphone diagnosis, Flow's input preference was explicitly set to Ditto USB. Double taps accept contacts of 10–240 ms, up to 400 ms apart and 60 px apart. A drag over 22 px before the hold threshold cancels dictation. First contact after sleep is wake-only. Synthetic firmware input never sends real keyboard events. Stale telemetry (350 ms), reboot, connection changes and foreground-app changes release held modifiers. A force-killed process cannot guarantee cleanup; tap/release the physical modifiers if needed after a crash.

## Microphone

0.11.56 fixes USB sample corruption under the connected helper/Bluetooth workload by moving the radio controller and host off USB’s core. Two native transport captures match the clean baseline; the existing gain/filtering remain unchanged. See `test-results/usb-quality-0.11.55/RESULTS.md`.

USB exposes **Ditto Microphone**, mono 16 kHz, 16-bit, through the native macOS audio stack. Select it as the input in Flow or in the relevant app. Playback remains on the Mac's existing output. Both board microphones are initialized by the codec driver; this first USB input streams one microphone channel, not a stereo/beamformed mix. Device mute and USB host mute/volume attenuation are supported in source. Capture stays awake while a USB audio client records, even if the display is sleeping.

Firmware 0.11.15 adds about +18 dB input gain, an 85 Hz high-pass filter and peak limiting. A 200 ms ADC settling guard applies only after actual codec power-up, with a 10 ms fade. Audio parks five seconds after the last USB capture if the screen is also sleeping. USB uses a fixed, bus-synchronous stream with ADC-clock resampling, about 60 ms of priming, and 10 ms capture DMA blocks with 80 ms of DMA headroom. NVS settings persistence is deferred during recording; changes still take effect immediately.

Native Mac tests found the earlier stream could mix stale/current audio and smear mute transitions for ~2 seconds. Final 0.11.15 captures had clean ~41–43 ms mute tails, first callbacks at 88–144 ms, roughly 8× stronger acoustic test tones and no clipped samples. A 32-second concurrent speaker/mute/sleep/wake run had no new checked audio/USB/display faults. Cold-start initial samples remain silent while the ADC settles; first callback is not first usable speech. Flow explicitly prefers Ditto USB. The September 23 live user trial confirmed prompt onset and clearer audio through Flow. This is a subjective real-use check, not an instrumented end-to-end latency or noise-rejection benchmark. Evidence: `test-results/mic-response-0.11.8/RESULTS.md`.

The fresh 0.11.5 recovery test recorded three distinct tones from the Mac speaker through Ditto's physical microphone. Seven seconds of PCM were captured. During the 9.26-second status interval, presentation averaged 29.91 FPS, with zero reported USB audio drops, microphone overruns or display errors. This is a short acoustic test, not a long battery, speech-quality or combined-Bluetooth qualification. Older 0.11.0 measurements remain separate historical evidence.

The ESP32-S3 does not expose a standard Bluetooth headset microphone. Firmware 0.11.55 instead streams processed 16 kHz microphone audio over encrypted custom BLE notifications. Ditto Remote decodes it into the separate BlackHole 2ch virtual input. Select that input in Flow after its pending installation. The current USB selection remains usable. No generic Bluetooth-headset support is claimed.

## Listening, status and reactions (0.11.44)

USB capture selects Listening while held or stretched; a small green halo follows the head and shoulders. Its six native-resolution intensity stamps are prepared once at startup; geometry can still stretch behind the clock without a top cutoff. Device mute or sleep suppresses it. A confirmed helper release ends Listening promptly and starts a selected reaction even while macOS keeps the USB stream open. Recording state uses a bounded 600 ms helper lease, with six seconds of USB-tail suppression after release. Other apps using Ditto USB continue to activate the cue normally outside that tail.

The top connection icon starts at y=32. The bottom white-outlined battery runs y=468–490, below the character. It has no outer black backing. During active charging, an internal filled lightning symbol replaces the percentage and a mint sweep stays within the true green filled area. Full-on-external-power retains a steady internal bolt without digits. Discharging shows the percentage; charge-paused remains a gray percentage and unavailable state remains `--`. Low battery (20% or lower) is red; unknown/missing level is `--`. Small status patches update during parked sleep.

The AXP2101 gauge supplies percentage; REG00/01 independently supply battery presence, external power and charger phase. USB enumeration is not charging status. The actual board was full on power, so active charging previews remain fixtures rather than a tested physical charge cycle. Bluetooth startup, encrypted pairing, control acknowledgements and audio-packet delivery have since passed; Mac virtual-input setup remains pending.

Idle Hello there uses a random 25–45-second eligible interval, without resetting the sleep timer. Successful dictation release, Return or guarded Undo selects Yes, Celebrate, Giggle, Happy, Surprise, Wink or Thinking without adjacent repeats. Wink has eligible weight 3 versus 2 for each other reaction. Actions are acknowledged through the existing input poll; retries are deduplicated, stale queued reactions expire, and a new dictation cancels pending reactions. Device touch/recording/sleep takes priority. The helper does not turn every shake into an Undo emote: the existing unchanged-insertion guard must accept it.

See `test-results/green-upper-glow-0.11.43/RESULTS.md` for current measurements and limits: 29.52 FPS with USB capture active, with no added checked transport faults. This is approximately 30 FPS, not a guaranteed ≥30 or large-headroom claim. Bluetooth pairing/gesture qualification, a physical charge-cycle qualification and a new human Enter/Undo trial remain separate.

## Bluetooth transport (implemented; pairing still pending)

The device advertises one bonded BLE service. Input reads and control writes require encryption; notifications are withheld until encryption completes. Versioned input packets contain current touch/motion state, battery and four retained contact edges (USB retains sixteen). A binary input packet is 28–92 bytes, targeted every 32 ms. The 44-byte control write carries the dictation lease and deduplicated accepted action feedback. USB is preferred when fresh; transport changes release held keys and reset gesture history. The helper retries/discovers automatically. The menu's Bluetooth toggle can explicitly retry a firmware recovery stop while USB is attached.

Pair through **Ditto Remote → Bluetooth**. This is a custom BLE remote service, not a Bluetooth headset. The current helper already has Bluetooth enabled and is waiting for pairing to finish; unlock the Mac before inspecting any system prompt. macOS listing the device as connected does not establish an encrypted input stream. Actual wireless gestures, unplug/reconnect, bonded reboot, connected workload latency and battery impact remain unverified. Wireless dictation uses the Mac's configured microphone; Ditto audio remains USB-only.

## Packaging and startup

- Sources: `host/mac-remote/`.
- Build: `../../venv/bin/python tools/build_mac_remote.py`.
- Installed bundle: `../../../outputs/scout-remote/Scout Remote.app`.
- Previous bundle: `../../../outputs/scout-remote/.rollback/Scout Remote.app`.
- Identity: `local.scout.remote`; local ad-hoc signing, strict signature verification.
- Assets and generation notes: `host/mac-remote/Assets/`.
- Startup: manual launch; menu-bar app, no login service.
- Bridge: reuses loopback port 8781 if already running, otherwise starts its bundled Python bridge. Depends on the existing project Python/pyserial environment recorded in Info.plist; this is a local install, not a portable distribution.
- Mutable capture output: `~/Library/Application Support/Ditto Remote/`, outside the signed app. No transcript archive is written. Test scratchpad text is not persisted.
- Quit shuts down the bridge only if this helper started it. Quit the helper/other serial readers before flashing.

## Evidence and remaining verification

`test-results/usb-mic-0.11.0/` retains the acoustic recording/report, build/flash logs, actual native menu view captures, and host checks. Tests passed: production ESP binary encoder through the real Swift decoder; buffered 15 ms double taps recognized exactly once; wake-only hold suppression; signed motion and edge timestamps; truncation/version rejection; 256 USB input fragment sizes with sanitizer checks; 4,220 native UI text-layout cases; reconnect without replaying uncertain actions; strict signed-bundle validation after launch; bridge auto-start and clean Quit.

The previous helper's real Flow recording, insertion and Undo were confirmed by the user. The revised Return behavior still needs the permission-enabled real interaction test. The original automatic bootloader transition failed; the corrected PHY handoff was verified on 0.11.1–0.11.3. After the 0.11.4 reboot loop, manual BOOT/PWR recovery and 0.11.5 flash/readback succeeded. Ordinary reboot, both render workers, native USB mic, framebuffer and sleep/wake checks passed on 0.11.5. Bluetooth pairing, real gestures, unplug/reconnect and combined-radio workloads remain pending separate recovery of Bluetooth startup.

For emergency download-mode entry on this board: unplug USB; hold PWR for at least six seconds, then release; hold BOOT while reconnecting USB; still holding BOOT, briefly tap PWR, wait two seconds, then release BOOT. This board has PWR and BOOT, not RESET. After manual ROM entry, esptool watchdog reset successfully starts the application; ordinary USB RTS reset previously remained in download mode.
