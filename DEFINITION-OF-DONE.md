# Definition of done — Scout OS prototype

September 19, 2026. This is the acceptance standard for the small, polished prototype requested in this conversation. Completion is demonstrated on the actual device. A compiled build, a successful flash, or a short benchmark alone does not meet this standard.

## Product finish line

Pick up the device, talk to a recognizable, expressive character, and reliably operate Wi-Fi, volume, brightness, and microphone mute. The display makes the conversation and resulting state clear. Return home or open a small app while retaining the same assistant. The experience looks intentional, animates smoothly, and recovers gracefully when something fails.

The initial architecture includes the Mac USB bridge for voice/AI transport and provisioning. Standalone cloud voice over the board's Wi-Fi is not a completion claim for this version. First-time Wi-Fi setup must work through that bridge even before the board has joined a network.

## Milestone A — local experience complete, before the API key

All of the following must pass:

1. **Finished visual direction on the device.** An approved character or approved hardware-appropriate adaptation replaces the temporary performance-test robot. Black background, legible clock/readouts, the 50-pixel text inset, and character/UI overlap work together. Home, settings, password correction, and conversation states share the same visual language. No clipped essential text, placeholder surfaces, or visible flicker. Review actual device output, not only generated concept art. Digital framebuffer inspection does not establish optical panel behavior.

2. **Sharp animation with a measured budget.** Retain native 410 × 502 rendering and target sustained 30 FPS during simultaneous animation, microphone capture, playback, and networking. For headroom, target p95 frame-processing time at or below 25 ms, with at least 99% of frames inside the 33.3 ms processing budget. Record delivered frame pacing as well as processing time. Idle drawing slows without breaking wake-up, timers, or clock updates. A lower resolution, lower frame-rate target, or materially simpler appearance must be recorded as a design decision; it is not an invisible performance workaround.

3. **Every essential action works through the shared interface.** Volume, brightness, microphone stream mute, clock synchronization, sleep/wake, home/app navigation, and a timer work on hardware. Wi-Fi supports scanning, selecting, drafting a password, precise corrections, connecting, disconnecting, and reconnecting. The screen shows observed results and clear pending/failure states. Reboot preserves settings and recovers time. Asynchronous results do not overwrite a newer user action. Secured connection tests require supplied test-network credentials and cannot be counted as passed without them.

4. **A small, usable app foundation.** One working example app and a documented way to add another. Apps share the character, device services, and callable actions. Adding an app does not require rediscovering the display/audio drivers. Compiled-in apps are sufficient for this prototype.

5. **Repeatable verification and handoff.** Firmware builds from the supplied source, flashing is reproducible, and the console can invoke the same actions the language model will invoke. Actual framebuffer captures and test reports are available. At least a two-hour mixed-workload run has no unexpected resets, display/audio transport errors, or continuing memory loss. Reboot, idle/wake, action interruption, and disconnection/recovery checks pass. This establishes prototype acceptance, not production qualification or battery-life claims.

The local milestone must not wait on an OpenAI key. Visual work, renderer work, action tests, audio transport, and developer documentation can proceed independently. A missing Wi-Fi test credential remains a specifically unverified item.

## Milestone B — natural-language experience complete

After the API key is supplied, demonstrate the complete loop on hardware:

- Speak naturally to select a network, dictate a password, correct a character, and connect. Confirm the actual connection result before announcing success.
- Ask for volume or brightness changes, mute the outgoing microphone stream, start a timer, open an app, and return home. These operations use the shared actions already tested locally.
- Keep the same assistant available across surfaces. Its face and motion accurately reflect listening, thinking, speaking, interruption, and unavailable states. UI readouts communicate outcomes; sliders, touch buttons, and a touch keyboard are not the primary interaction.
- Interrupt a spoken response and make a new request without stale speech or stale actions continuing. Check microphone/speaker behavior on the actual device, including whether playback confuses the conversation.
- Recover clearly from unavailable AI service, an unplugged bridge, lost connectivity, a failed action, and an incorrect password. Show truthful state rather than false success or an indefinite spinner. The existing local recovery controls remain usable when voice is unavailable.
- Repeat the relevant performance and reliability checks with real conversation traffic. A silent PCM soak alone is not evidence of a finished voice experience.
- Complete a hands-on review of the whole experience: appearance, readability, animation, conversational timing, and recovery behavior meet the agreed prototype quality.

## Outside this finish line

A new kernel, an app store, downloadable/sandboxed apps, broad Bluetooth accessory support, OTA updates, standalone cloud networking, full localization, production security certification, and exhaustive battery optimization are not required. Do not expand into those while the essential experience remains unfinished.

## Current position

The snapshots below are historical. [WORK-STATUS.md](WORK-STATUS.md) records the current flashed build and requirement-by-requirement gaps; none of these older passages establish current acceptance.

The latest September 19 steering replaces the rejected Labubu with a procedural Ditto. It remains genuine local 3D at native resolution, with a simple face and smooth shading. The source/asset pipeline is documented in `AVATAR-PIPELINE.md`; current evidence belongs in `test-results/ditto-slime-0.5.0/`.

The prior Labubu two-hour run was deliberately stopped when the user requested this replacement. It is not a completed acceptance result. The short 0.3.1 combined run is preserved in `test-results/ditto-integration/short-checkpoint/`. The completed 0.3.2 two-hour run remains historical. `INTEGRATION-REPORT.md` tracks the current touch-physics revision and its five-minute mixed check; no two-hour result is claimed for 0.4.0. Secured Wi-Fi association, hands-on panel review and natural-language conversation must be evaluated separately. The latter connection tests require the test-network credential and later API key. No milestone is marked complete by this avatar replacement.

The subsequent touch request adds directional poke/drag/circular forces, release momentum, upright closed-eye rest, 10% sleep brightness, and touch wake with restoration of the saved awake brightness. Synthetic input and physical fingertip verification must remain distinguishable in the evidence.

The next 0.5.0 steering requests a slime-like material response and cute reactions. Local skin stretch, downward flattening, delayed rebound, x_x and side-eye are implemented; `test-results/ditto-slime-0.5.0/` records this version separately. Existing brightness/sleep behavior is retained.
