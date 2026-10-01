# Current acceptance map — September 21, firmware 0.9.20

This map and [WORK-STATUS.md](WORK-STATUS.md) supersede the historical sections below. Production source/image is sealed and flashed as 0.9.20 (face shader in cached flash, app SHA-256 `5ee3727b2af8886f3936190f13cbb40fb215690d61afb433c2475dcb4af9781e`). This recovers 2,816 bytes of internal reservation with a small measured throughput cost. Production mixed performance is 27.42 FPS and still fails headroom. A report applies to its recorded source, binary and workload; older qualification does not automatically cover this image.

| Requirement | Evidence and current scope | Still required |
| --- | --- | --- |
| Fast, continuous touch paths | `touch-response-0.9.17/hardware-check.json`; native history/follower checks in TOUCH-LATENCY.md | Physical finger/IRQ cadence and optical response |
| Reliable digitizer | `mixed-touch-preflight-0.9.17/soak.json`: 90.29 s, zero non-deferred failed samples/reset/overflow; all 62 failed samples remain visible as quiet deferrals | Two-hour synthetic run passed; real fingers and pickup/wake still required |
| Native glossy image and smooth drool | `face-code-placement-0.9.20/`: identical shader calculations and tested candidate/production machine bytes, 22 candidate expression captures and 13 production sleep/wake captures | Physical appearance remains a user/optical check |
| Nominal 30 FPS with margin | `face-code-placement-0.9.20/production-profile.json`: home 30.20 / drag 29.87 / mixed 27.42 FPS; frame-time margin fails | Sustained target with the final real workload; average FPS alone is insufficient |
| Natural-language-oriented local UI | `ui-review-0.9.12/hardware-review.json`: 26 checks and 27 captures, including password edits and extreme drags | Real conversation, spoken corrections and failure/interruption review |
| Sleep/wake and low work | `face-code-placement-0.9.20/sleep-capture-check/`: three injected cycles/13 captures, parked body/physics, moving overlays, clock/date and brightness verified; earlier 0.9.16 CPU measurement was 4.98% busy | Physical wakes, current/runtime and final broader workload |
| Secured Wi-Fi | Prepared `tests/wifi_secure_check.py`; scan/edit actions are tested | Supplied test network, association/authentication failure/reboot/outage recovery |
| Live AI conversation | `voice-action-cancellation-0.9.17/` plus `post-soak-host-0.9.17/`: offline races and actual USB cancellation/audio/port-exclusion checks passed | API-key/provider integration and acoustic tests |
| Synthetic PCM delivery cadence | `audio-cadence-0.9.20/hardware-profile.json`: revised 90 s mixed phase, zero missed source deadlines / gaps over 40 ms, max acknowledgement gap 37.78 ms; deliberate pause fails the runner despite passing average rate | Earlier isolated 170 ms gap's cause, longer final workload, real speech and acoustic timing |
| Final reliability | 0.9.17 two-hour synthetic mixed run passed; `extended-mixed-soak-0.9.17/soak.json` and `completed-audit.json` | Final chosen firmware with real conversation, reconnect/persistence and power qualification |
| Reproducible handoff | `release-handoff-0.9.17/`: 10 package checks and 14 build-provenance guards; Counter example compiled/tested separately | Final chosen binary plus explicitly reviewed evidence; packaging does not imply acceptance |

Evidence paths in this table are under `test-results/`. A checkpoint package contains only the explicitly selected subset listed in `evidence-index.json`; other historical references may be absent. No physical latency, cloud conversation or battery-life claim follows from synthetic input or silent PCM.


## Historical acceptance records

The following records remain unchanged for their named versions. Phrases such as “current build” within them refer to those historical checkpoints.

# Eye fix — September 21, firmware 0.8.2

Current scope: remove Side/Happy eye silhouettes, keep dot-eye gaze, and arbitrate blink/wink/expressions without stacking. Portable geometry/pose checks, 16-emote native visual review, drag/release comparisons, device captures and a 60-second combined load passed. Body physics/assets are unchanged and 480 old/new interaction frames match exactly for body geometry/transforms. Evidence: `test-results/ditto-dot-eyes-0.8.2/validation-summary.json`. C/deadpan personality refinement and older full-product acceptance items remain separate.

---

# Gravity update — September 21, firmware 0.8.1

Current scope: fused physical gravity drives the body’s bulk compression/support in any orientation, while preserving the accepted jelly/touch behavior. Controlled on-device orientations, rotation, taps/drags, fast shaking, sleep/motion wake, live IMU restoration, large USB replies and a 60-second combined load passed. Evidence: `test-results/ditto-world-gravity-0.8.1/validation-summary.json`. This focused validation does not close the earlier full-product, battery or physical feel acceptance items. All sections below are historical.

---

# Standby update — September 21, firmware 0.8.0

Current scope: movement/touch inactivity, curved brightness, paused rendering/physics/audio, minute clock/date refresh, CPU frequency scaling and responsive wake. The controlled checks, separate real quiet interval and 60-second combined load are recorded in `test-results/ditto-motion-wake-0.8.0/validation-summary.json` and `INTEGRATION-REPORT.md`. Battery runtime, the prior digitizer issue and full-product acceptance remain open. Everything below is historical evidence for earlier builds.

---

# Focused tap update — September 20

Current build: **0.6.1**. This patch has portable physics, native render comparison, four device tap sites, retained drag/release, sleep/wake and two minutes of repeated taps with audio/Wi-Fi validation. See `INTEGRATION-REPORT.md` and `test-results/ditto-sharp-taps-0.6.1/`. Earlier full-control qualification below is for 0.6.0; it was not repeated for unrelated services in this narrow patch.

# Local acceptance audit — September 20

The current build is firmware **0.6.0**, with surface-picked taps, springier release, corrected face seating and 16 live emotes. Current execution and measurements are recorded in `test-results/ditto-emotes-0.6.0/validation-progress.json` and `final-metrics.json`. The earlier 0.3.2 audit and two-hour measurements remain in `test-results/ditto-integration/LOCAL-ACCEPTANCE-0.3.2.md`; they do not establish performance for this new build.

| Requirement | Evidence | Remaining check |
|---|---|---|
| Poke, local pull, downward squish, release waves, face reactions | Shared portable simulation checks local deformation, directional response, settling, stress bounds, normals and expression recovery. Firmware checks and actual captures verify x_x during squish and recovery into side-eye. | Physical fingertip feel and coordinate alignment. |
| Digitizer recovery | Eight-failure threshold and two-second cooldown tested in the portable check; local reset diagnostic restarts the actual controller and verifies valid samples resume. Failure counters remain visible. | Electrical root cause of the earlier controller stall is not established. |
| Upright sleep, 10% brightness, tap wake | Explicit and real 60-second inactivity sleep checks; actual upright sleep capture; restored awake brightness; approximately one draw/second asleep. | Physical fingertip wake latency and perceived brightness. |
| Native rendering with headroom | New five-minute mixed test with synthetic gestures, audio input/output and Wi-Fi scans; see current `final-metrics.json`. | This feature has not had its own two-hour run or real voice workload. |
| Settings and app foundation | Existing ten-group hardware acceptance and timer navigation checks are repeated by the current validation runner. | Cloud voice adapter at the API-key stage. |
| Delivery | The current source, firmware, captures and results are packaged separately in `outputs/scout-os-ditto-emotes`. Previous release remains available. | Overall product acceptance remains open. |

These are engineering checks. Synthetic touch is reported separately from actual physical contacts, and silent PCM traffic is not a conversational audio evaluation.

## Secured network test

`tests/wifi_secure_check.py` is prepared and syntax-checked; it has **not been run against a secured network**. It prompts for the passphrase without echoing it or placing it in the shell command. It refuses to replace an existing saved network or active password draft/timer. On success it leaves the supplied working test network saved; if the initial credential fails it removes the configuration it created. Its screenshots occur after firmware clears the visible password draft.

Run only when the validation runner has finished and the USB bridge has been stopped, so there is one serial owner:

```sh
python tests/wifi_secure_check.py --ssid 'Your owned test network'
```

The plan checks observed secured-network discovery, a real password correction followed by association, explicit disconnect, bounded wrong-password failure, recovery with the correct password, and saved-network association after reboot. It keeps a newer screen visible during asynchronous outcomes. Router-outage recovery still needs a controlled access point; reboot reconnection is not equivalent to testing an access-point outage.

## Conversation boundary

The cloud adapter remains deferred until the API key is supplied. The transport already provides response-owned speech chunks and cancellation. The final adapter must also reject late tool calls/state updates from an interrupted model turn, and associate asynchronous results with the request that produced them. Those are integration requirements, not capabilities established by the current silent-PCM soak.

The final goal remains `DEFINITION-OF-DONE.md`. Neither milestone is marked complete by this audit.
