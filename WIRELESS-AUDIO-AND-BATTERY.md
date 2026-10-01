# Wireless dictation and battery diagnostics

## Audio path

Ditto's processed 16 kHz mono microphone → independent 20 ms IMA ADPCM blocks → encrypted Bluetooth LE notifications → Ditto Remote → BlackHole 2ch → Wispr Flow.

This is a custom BLE stream, not a Bluetooth Classic headset. The ESP32-S3 does not become an ordinary system Bluetooth microphone by pairing. The Mac helper supplies the audio input through the separate, unmodified BlackHole driver. The helper only opens BlackHole's persistent device UID; it never routes this stream to the Mac's speakers or changes the system output device.

- Notification subscriptions require encryption. The Mac completes input subscription, protected input receipt, then microphone subscription before sending controls. Setup and stale-input timeouts recover stalled connections.
- Six persistent CCCD slots cover Service Changed, input, and audio for both permitted bonds. A compile-time assertion prevents the old two-slot capacity regression.
- Audio characteristic: `D1D70003-A76B-47F1-A982-F194A1334701`.
- 172-byte packet: version 1, mute flag, rate code 16, reserved zero; LE sequence; signed LE initial sample; IMA index; reserved zero; 319 four-bit differences, low nibble first. 320 decoded samples per packet. About 8.6 kB/s before BLE overhead.
- Each block is independently decodable after packet loss. Sequence gaps are counted and briefly concealed with silence; malformed/duplicate/old packets are rejected.
- The existing 44-byte encrypted control frame accepts version 2: byte 1 bit 0 is dictation, bit 1 requests audio. Version 1 stays compatible. Requests expire after 600 ms without renewal, and disconnect cancels the lease.
- The helper requests audio on physical touch before hold recognition, retains a one-second tail, and renews every 100–120 ms. Simulated touch never triggers system shortcuts or microphone requests in normal operation.
- BlackHole output remains warm with silence. A short jitter buffer absorbs Bluetooth scheduling variation. The on-device microphone keeps its existing processing; USB behavior is preserved.
- USB remains preferred for controls when present. The virtual microphone can receive BLE audio with the cable attached too, so the selected Flow input can remain the same when unplugging.
- No voice or transcription is stored by normal battery diagnostics. The explicit bounded audio probe can save PCM only when invoked with its optional `--pcm` argument.

### Mac dependency

Official package: [BlackHole 2ch](https://existential.audio/downloads/BlackHole2ch-0.7.1.pkg), version 0.7.1, SHA-256 `57b540f27a3e29c37e310e01bee0fdfab76733087e47f997ef9dccf851400dcf`.
Verified Developer ID Installer: Existential Audio Inc. (Q5C99V536K), notarized. The package installs the HAL driver under `/Library/Audio/Plug-Ins/HAL` and declares a required restart. Its installer requires administrator authentication. This is a separate GPL-licensed dependency, not a modified driver embedded in Ditto Remote.

Select **BlackHole 2ch** as Wispr Flow's microphone after installation. Do not select it as the Mac's speaker/output. The helper's Test Window reports whether its wireless output is ready. The existing Ditto USB microphone remains available for rollback.

## Battery evidence

The board owns only the first 1 MiB of the existing unused `storage` partition. It provisions an ownership/CRC header only when that entire region is blank. Unknown contents are never erased. A circular journal uses 256-byte CRC-protected records and has 4,080 slots: about 68 hours at one record per minute, slightly less with additional power-source transitions.

Records survive unplugging/reboot. Flash writes pause during dictation/audio, with a 16-record RAM queue; overflow is counted. Sudden power-off can lose the unfinished minute plus deferred records. CRC-invalid records are skipped on recovery. Only owned old journal sectors are recycled. Firmware, NVS, Bluetooth bonds and unrelated data are outside this region.

The logger records:

- PMIC estimated battery percentage, voltage validity, source and charging state.
- Observed awake/sleep, battery, radio, microphone, streaming and dictation time.
- Average display brightness and a sampled CPU-clock indicator.
- Task runtime deltas, including both idle cores, rendering, audio, radio and services.
- Frame/physics/contact counts, minimum internal free RAM, reset reason and error counts.
- Firmware version, monotonic uptime, boot ID, sequence and valid wall-clock timestamps.

Task counters use unsigned deltas so their ~71-minute wrap does not break a full battery-cycle report. CPU busy percentages use two-core elapsed capacity; per-category time is diagnostic attribution, not electrical power measurement.

The bridge automatically archives records while USB is connected and audio is idle. Location:
`~/Library/Application Support/Ditto Remote/Battery/`

Each device has append-only JSONL evidence, metadata, and `Battery Report.md`. `Battery Report…` in the helper opens the report. The bridge exposes its cached report at `http://127.0.0.1:8781/battery`. Archive catch-up is paginated, bounded and deduplicated; interrupted final appends are repaired before resuming.

Use the device normally after a full charge, then reconnect USB. No manual stopwatch or special logging mode is needed. Reconnect within roughly 68 hours to preserve the whole local history. A partial discharge reports observed runtime and gauge decline; only actual complete usage can establish cycle life. Charging measurements do not establish unplugged battery drain. There is no direct discharge-current meter exposed by the implemented PMIC interface, so these reports do not claim mA, watts or mWh.

## Validation status

Native tests cover ADPCM block recovery/mute/malformed packets (36.9 dB SNR on a 440 Hz test tone), encrypted-control frame versions, journal ownership/torn writes/wrap/reboot, archive dedup/truncated tails, and existing gesture/focus/feedback behavior. Real hardware and Mac routing results are recorded separately in `test-results/wireless-battery-0.11.51/RESULTS.md` once measured. Do not treat source-level tests as acoustic or full-cycle validation.
