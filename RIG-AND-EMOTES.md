# Ditto's soft skeleton and emotes

Current skeleton/face implementation: 0.10.11; automatic trigger integration, visible release priority and optimized presentation: 0.11.44. The five-joint hierarchy is root → body → head, left hand and right hand. It is defined in `main/ditto_rig.h`; the A3D file remains the same four-mesh base surface and semantic action catalog. The live skeleton is a runtime layer, not a new Blender asset or prerecorded playback.

Each joint has a bind-space pivot, parent and live rotation/translation. Smooth body/head/hand weights make one continuous skin. The 866 body vertices cache three weights and their x/y derivatives: **31,176 bytes of PSRAM**, allocated once. Face vertices sample the same weight field after mouth/eye morphing. Weighted transforms include the weight derivatives in their Jacobian; inverse-transpose normals keep glossy highlights attached to the curved skin. Idle bypasses the rig exactly once its controls return to zero.

Order: authored joint pose → weighted skin and matching normals → existing liquid deformation → projection/shading → display. The physics, material law, fused sensor inputs, 2.3-second oval reactions and rotating dizzy face retain their previous code. Hit testing maps a contact through the current rig before supplying its impact to the liquid. Ordinary touch and motion reactions take priority over manually invoked emotes; the rig blends away instead of forcing a gesture through an interaction. Confirmed remote-action emotes take over the authored face and rig after release, so physical recovery cannot consume the entire clip. A fresh touch still interrupts immediately. Speech owns its mouth. Sleep cancels the rig, settles and parks it with the existing small sleep overlays.

## Automatic triggers

Firmware 0.11.27 greets with Wave (Hello there) after 25–45 seconds of eligible quiet home time. Movement, touch, active conversation, a current gesture or another screen delays the greeting. It never resets the sleep timer.

Helper 0.2.7 acknowledges accepted dictation release, Return and guarded Undo. The device randomly chooses Yes (Yep), Celebrate (We did it), Giggle (Heh), Happy (Pleased), Surprise, Wink or Thinking, without immediately repeating the previous choice. Wink has eligible weight 3 versus 2 for each other choice (1.5×); the 10,000-pick deterministic check produced 1,850 Winks with zero immediate repeats. New touch/recording and sleep interrupt automatic emotes. No, Sad, Curious and Point remain explicitly invokable for other contexts. The legacy physics-driven smiles, oval mouth and dizzy reactions remain.

Scheduling is isolated in `main/ditto_autoplay.h`; queue/acknowledgement is in `host/mac-remote/Feedback.swift`. In 0.11.29, confirmed reactions also carry explicit presentation ownership into `BlobRenderer`: the authored face and joints play while the previous physical response continues underneath. This leaves the liquid state unchanged and suppresses only its competing recovery expression. Idle Wave does not receive this override. Live status now reports reaction priority, rig activity, elapsed clip time and actual joint displacement; a selected clip alone is insufficient evidence. Evidence: `test-results/emote-recovery-0.11.28/RESULTS.md`; original scheduler/helper work: `test-results/status-spacing-0.11.27/RESULTS.md`.

## Full-screen rendering and listening — 0.11.44

The avatar now renders behind the entire screen, including the clock/status; the prior fixed top reservation is gone. Listening retains the live head/hand rig under held liquid deformation. A small green halo follows the upper body; six cached native-resolution levels provide its fade. The broad aurora has been removed. The glow participates in damage tracking only when it moves; both old and new footprints are repaired. Geometry, smooth shading, physics and authored pose math remain unchanged. Zero-weight bone/Jacobian work is skipped exactly. Proven current/previous projected bounds and framebuffer history let unchanged screen strips remain untouched, with complete redraws for UI or glow-level changes. All 16 clips match the uncropped full-render reference pixel-for-pixel over 3,360 frames. The latest microphone run measures 29.52 FPS; headroom remains limited. Evidence and interaction checks: `test-results/green-upper-glow-0.11.43/RESULTS.md`.

## Gestures

| Shared action | Movement and expression |
| --- | --- |
| Wave | Right-hand wave, small head tilt and counterbalance |
| Yes | Head nods, with a grounded body |
| No | Head shake, sideways gaze and a closed squiggly mouth |
| Curious | Head tilt, lifted hand and curious gaze |
| Point | Right-hand extension with following head/gaze; inward body shift keeps screen margin |
| Celebrate | Both hands lift with a restrained bounce |
| Sad | Head and hands droop; closed squiggly mouth |
| Giggle | Small head/body tremors and a soft open smile |
| Happy | Gentle pleased sway and raised hands |
| Surprise | Head rises, hands spread, mouth opens |
| Wink | One eye quickly closes into a fixed `>` and reopens; a warm rotating sparkle appears beside it |
| Listening / Thinking / Speaking | Gentle head-and-hand poses for their conversation state |
| Idle / Sleep | Existing quiet behavior and power policy |

Discrete actions anticipate, move and return within the existing four-second action duration. Controller transitions smooth retargeting. The `>` shares the eye controller's exclusive ownership with blinking, dizziness and sleep. The current wink has a separate 515 ms envelope (40 ms closing, 400 ms hold, 75 ms reopening), bypassing slow body easing. A partially closing dot switches to a fixed closed-eyelid contour enlarged in 0.10.4, then about 24% larger again with only 8% extra stroke weight in 0.10.5; there is no dot-to-arrow interpolation or growth. The 20-triangle fan remains valid. Curved happy and side-eye eyelids remain absent.

Invoke through the existing shared action, for example `avatar.emote` with `{"name":"Wave"}`. The bridge console exposes those same actions. The language model can call them later; no API key is needed for local gestures.

## Review and checks

Gallery: http://127.0.0.1:8782/ditto/rig/. Movies use native-resolution production-renderer output encoded for review. They are not camera footage and are not embedded in the firmware. The Speaking preview demonstrates body language, not live speech.

`tests/ditto_rig_check.cpp` verifies interior normal derivatives, positive Jacobians, weight normalization, parent inheritance and neutral/canceled rest. `tools/render_rig_emote.cpp` checks every actual body vertex over each rendered clip. `tests/ditto_eyes_check.cpp` checks single-owner expression priority and positive winding, closed-eyelid bounds, the timed rig-to-eye closure and immediate interruption. `tests/world_gravity_check.cpp` verifies that legacy asset rotations no longer affect the gravity mapping and that the actual scene transform preserves world down.

Build identities, native captures, exact-pixel ordinary-interaction comparison, targeted hardware checks and measured limitations are in `test-results/ditto-rig-0.10.0/RESULTS.md`. The directory includes .0/.1 candidates; only the release seal identifies the final .2 build. This is an emote/rig delivery, not completion of the overall OS acceptance requirements.

The focused 0.10.3 wink correction is recorded separately in `test-results/ditto-wink-0.10.3/RESULTS.md`; the longer hold and heavier fixed eyelid are recorded in `test-results/ditto-wink-0.10.4/RESULTS.md`.

The 0.10.5 size-only refinement is recorded in `test-results/ditto-wink-0.10.5/RESULTS.md`; the 355 ms timing is unchanged.

In 0.10.8, `main/ditto_wink_sparkle.h` adds a head-following four-point star with soft champagne/cream shading, approximately 126 degrees of rotation, slight foreshortening and a short fade. A small analytic shader draws its antialiased silhouette, soft glow and highlight through a depth-tested patch. It needs no scratch allocation or animation assets; rejected stroke-based candidates are recorded in the evidence directory. The effect runs only during the wink; contact, motion, sleep and other clips suppress it. Idle/sleep do not animate it. Evidence: `test-results/ditto-wink-0.10.6/RESULTS.md`.

In 0.10.10, the four-point sparkle is replaced by a filled five-point star with rounded tips, a broad creamy center and a soft golden halo, matching the supplied reference. Its anchor, rotation, timing, cancellation and body physics are unchanged. A symmetric signed-distance shader retains the native candidate pixels while reducing computation. Evidence: `test-results/ditto-star-0.10.9/RESULTS.md`.

In 0.10.11, every open mouth shares the pink interior and dark antialiased outline previously used for dizziness. This includes Happy, Giggle, Celebrate, Surprise, speech, physical reactions and the sleeping oval. Closed smiles and the No/Sad squiggles retain their dark line shapes. No changes to pose timing, eye ownership, body physics, rig or persistent allocation. Evidence: `test-results/pink-mouth-0.10.11/RESULTS.md`.
