# Building an app for Scout

Scout's first app platform is a small set of **compiled-in surfaces and callable actions**. An app supplies state, actions, and a visual response. The OS supplies the character, audio transport, device controls, clock, safe-area typography, rendering, and navigation. There is no app installation or sandbox in this prototype.

The working example is **Timer**. It runs in the background, survives navigation to Home, displays a countdown above the same character, and emits a completion event. It deliberately does not survive a reboot.

## Trace the example

| Part | Location | What Timer does |
|---|---|---|
| State | `main/scout.h`, `State::timer_end_us` / `timer_id` | Stores a monotonic deadline and run identity; zero deadline means inactive. |
| Actions | `main/services.cpp`, `timer.start` / `timer.cancel` | Validates arguments, changes the deadline under `state_mutex`, and calls `feedback()` to show the result. |
| Background work | `main/services.cpp`, `housekeeping()` | Clears the deadline once and updates an idle Home/Timer caption atomically, preserving other apps and active conversation. Plays a tone and emits `timer.finished` with its `timer_id`. |
| Visuals | `main/ui.cpp`, `header()` | Formats the remaining time and draws native typography; it never accesses display or audio drivers. |
| Agent contract | `host/skills.py` | Describes the two actions and their arguments. `execute()` sends them through the same device dispatcher used by the development console. |
| Verification | `tests/hardware_acceptance.py` | Starts a two-second timer, waits for completion, then checks cancellation. `tests/timer_navigation_check.py` also checks drafts, foreground controls, conversation, replacement and event identity. |

Try it with the persistent bridge running:

```sh
curl -s http://127.0.0.1:8781/skill \
  -H 'Content-Type: application/json' \
  -d '{"name":"timer_start","arguments":{"seconds":20}}'
```

Use `app_home` to return home. The timer continues. Opening `timer` again through `app_open` shows its remaining time. An API key is unnecessary for these actions; the eventual voice adapter calls the same interface. Compare `timer.finished.timer_id` with the ID returned by the start action before presenting it as the current timer: an older completion can still be in the host event queue after a replacement. IDs are scoped to a boot session.

## Add a small app

Use a simple counter as a first exercise. The current extension points are deliberately explicit:

1. In `main/scout.h`, add a `Screen::Counter` entry and `int counter = 0` to `State`.
2. In `main/services.cpp`, add `"counter"` at the matching position in `apps`, derive the app-navigation loop's bound from the array size, and add `"counter.set"` to `tool_names`.
3. Add a `counter.set` branch to `dispatch()` alongside the timer branches. Validate an integer named `value` from 0 to 9999, update `state.counter` while holding `state_mutex`, release the mutex, and call `feedback(Screen::Counter, "Count updated", Avatar::Yes, true)`. The common dispatcher tail returns observed state. Include `counter` in `status()` so callers can verify the result.
4. In `main/ui.cpp`, add its title at the matching position in `header()`'s labels array, and render the number with `center(font_64, ..., 323, GOLD)` in a `Screen::Counter` branch.
5. In `host/skills.py`, add `counter` to `app.open`'s name enum and a schema for `counter.set` with a required integer `value` from 0 to 9999. The skill name exposed to an agent is `counter_set`.

This uses existing services and the native compositor. It requires no changes to `board.cpp`, the serial protocol, the microphone pipeline, or the 3D renderer. The five edit points are the current compiled-in extension mechanism, not a claim that an independent plug-in SDK exists.

## Try the complete example patch

`examples/counter-app.patch` contains those five edits. Apply it to a separate copy of the source when experimenting:

```sh
git apply --check examples/counter-app.patch
git apply examples/counter-app.patch
bash build.sh
```

After flashing your experimental build, invoke `counter_set` with `{"value":42}` through the shared skill endpoint; open it with `app_open` and `{"name":"counter"}`. The example intentionally keeps its value only until reboot. The base firmware keeps Timer as its working example app; applying this patch is optional. The refreshed patch was applied and compiled in an isolated source copy, then flashed and exercised on hardware. Values 0, 42 and 9999, six invalid inputs, Home navigation, a background timer and updates during full-duplex silent PCM passed, with no new display/audio/TE errors. Captures and results are in `test-results/build-handoff-0.9.14/counter-check/`. That optional test binary reported an empty runtime version due to a build-variable scoping defect; the base firmware now reads its application descriptor directly. The Counter binary is identified by SHA256 `a399a29c42e3aa23185a77389393f6d50542ac28969914678428d113b5a906a8`. These are action/transport checks, not physical touch, cloud voice or sustained performance qualification. Restore the base source build after testing the example.

## Visual contract

- The display is 410 × 502. Essential text stays at least 50 pixels from every edge. Use the existing font and text helpers; they enforce that inset and tile clipping.
- Keep black as the background. The character is shared behind the app, with the app's text above it.
- The current shared composition supports a title near the top, a large result around rows 310–405, and a short caption/status below that. `header()` and `footer()` are called only for tiles intersecting those areas. For another composition, update those intersection bounds along with the drawing code.
- Show the result of an action and its current state. An app does not need a slider, button, or touch keyboard.
- Rendering must read a consistent state snapshot and remain free of blocking I/O, allocations, or changes to app state. The two renderer workers can draw different tiles concurrently.

## Action and lifecycle contract

- Validate every argument in firmware even if the host schema also validates it. Reject invalid input without changing state.
- Hold `state_mutex` only while reading or changing state. Do not hold it across a network call, an event emission, a display operation, or a wait.
- Use `feedback()` for visible action results and `notify_state()` when an interaction should wake the character. Shared voice state and microphone mute continue across navigation.
- Acknowledging asynchronous work means it started. Publish a completion or failure event and reflect the observed outcome in status. Wi-Fi connection is the existing example of this distinction.
- Background work belongs in the service task or a bounded worker, never the render loop. Timer's deadline uses the monotonic clock so clock synchronization cannot change its duration.
- Cancellation and replacement should be explicit. A replaced task must not later publish an outdated completion as though it were current.

## Keep diagnostics out of the audio path

`system.screenshot` starts one frozen-frame transfer. The command loop sends a two-row record at a time and continues reading other requests; its final success reply arrives after every row. `Device.screenshot()` already waits for that completion. A concurrent capture is rejected instead of overwriting the frame or request ID. A timed-out snapshot drains its late UI publication before another capture can start.

Do not put a whole image upload or other long loop inside `dispatch()`: it can prevent queued speaker commands from being handled and starve microphone capture. `emit()` serializes complete framed messages, and `emit_audio_input()` builds the bounded PCM event without allocating a JSON tree. Neither changes the existing JSON wire protocol. See `tests/capture_transfer_check.py`, `tests/wire_output_check.py`, and `tests/wire_hardware_check.py` for byte, lifetime, overlap, and failure checks. The hardware test reports audio timing separately; a framing pass does not mean its timing budget passed.

## Verify before sharing an app

Build with `bash build.sh`, flash the intended Espressif board, and invoke the new action through the console or `host/device.py`. Check valid input, invalid input, replacement/cancellation if relevant, Home/app navigation, and operation while microphone capture and playback are active. Capture the actual device output and check the 50-pixel inset and long text. Finally, repeat the combined performance test: an app must not silently lower resolution or animation quality to fit.

The prototype's acceptance criteria remain in `DEFINITION-OF-DONE.md`. Host previews and successful compilation are useful checks; neither substitutes for exercising the actual board.
