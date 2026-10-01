"""Summarize a completed short hardware check without declaring OS completion."""
import hashlib,json,shutil,statistics
from pathlib import Path
from validation_context import evidence_directory
ROOT=Path(__file__).resolve().parents[1];OUT=evidence_directory(ROOT)
s=json.loads((OUT/'soak.json').read_text());identity=json.loads((OUT/'build-identity.json').read_text())
assert s['completed_requested_duration'] and not s['errors'] and not s['unexpected_resets']
assert hashlib.sha256((ROOT/'build/scout_os.bin').read_bytes()).hexdigest()==identity['files']['build/scout_os.bin']['sha256']
assert hashlib.sha256((ROOT/'assets/avatar.a3d').read_bytes()).hexdigest()==identity['files']['assets/avatar.a3d']['sha256']
b,e=s['baseline'],s['measurement_end'];frames=e['frames']-b['frames'];seconds=e['uptime_s']-b['uptime_s']
under25=e['frames_within25']-b['frames_within25'];missed=e['missed_frames']-b['missed_frames']
checks=json.loads((OUT/'acceptance.json').read_text());samples=s['samples'];memory={}
for name in ('internal_free','psram_free'):
 values=[q[name] for q in samples]
 memory[name]={'minimum':min(values),'early_median':statistics.median(values[30:60]),'late_median':statistics.median(values[-30:])}
 memory[name]['median_change']=memory[name]['late_median']-memory[name]['early_median']
errors={k:e[k] for k in ('display_errors','audio_rx_errors','audio_tx_errors','audio_overflows')}
assert all(v==0 for v in errors.values()) and all(t['ok'] for t in checks)
m={'version':identity['version'],'avatar':identity['avatar']['id'],'duration_s':seconds,'frames':frames,'average_fps':frames/seconds,
   'frames_within25_percent':100*under25/frames,'frames_over33_333ms':missed,'frames_within33_333_percent':100*(1-missed/frames),
   'max_sampled_rolling_p95_ms':max(q['frame_p95_ms'] for q in samples),'command_p95_ms':s['command_latency_p95_ms'],
   'audio':s['audio'],'memory':memory,'errors':errors,'unexpected_resets':s['unexpected_resets'],
   'acceptance_passed':sum(t['ok'] for t in checks),'acceptance_total':len(checks),'requested_duration_completed':True,
   'two_hours_completed':seconds>=7200,'firmware_sha256':identity['files']['build/scout_os.bin']['sha256'],
   'gates':{'30_fps_with_1_percent_tolerance':frames/seconds>=29.7,'95_percent_within25ms':under25/frames>=.95,'99_percent_within33_333ms':missed/frames<=.01,'secured_wifi_verified':False,'cloud_conversation_verified':False}}
(OUT/'final-metrics.json').write_text(json.dumps(m,indent=2)+'\n')
report=f'''# Ditto integration — hardware checkpoint

Firmware **{m['version']}**, procedural Ditto, native **410 × 502**. This is a completed short check of the new avatar and rendering path, not a declaration that the full OS is finished.

| Check | Measured result |
| --- | --- |
| Combined audio, Wi-Fi scans, animation and USB status workload | {seconds:.1f} seconds; {frames:,} frames |
| Average animation rate | **{m['average_fps']:.2f} FPS** |
| Frames within 25 ms | **{m['frames_within25_percent']:.2f}%**; target 95% |
| Frames above 33.333 ms | **{missed:,}** |
| Highest sampled rolling 120-frame p95 | {m['max_sampled_rolling_p95_ms']:.2f} ms |
| USB command acknowledgement p95 | {m['command_p95_ms']:.2f} ms |
| Functional acceptance | {m['acceptance_passed']}/{m['acceptance_total']} groups passed |
| Display/audio errors; unexpected resets | Zero |
| Minimum internal heap / PSRAM | {memory['internal_free']['minimum']:,} / {memory['psram_free']['minimum']:,} bytes |
| Early-to-late median free heap change | Internal {memory['internal_free']['median_change']:+,.0f}; PSRAM {memory['psram_free']['median_change']:+,.0f} bytes |

The measured interval excludes the final diagnostic screenshots. Audio traffic includes {s['audio']['input_packets']:,} captured microphone packets and {s['audio']['output_packets']:,} silent speaker packets. Silent PCM exercises transport and concurrent rendering; it does not establish speech recognition, echo cancellation or conversational quality.

The blob is an adaptively tessellated smooth implicit surface: **1,590 triangles, 845 vertices, four meshes**. Its continuous normals, soft lighting, fixed RGB565 dithering and pre-quantization UI fade avoid the faceted printing geometry and horizontal fade bands. Fourteen shared animation actions run locally with interruptible transitions. There are no prerecorded character frames in firmware.

The body is closed with no degenerate or inverted faces. A clean temporary rebuild reproduced all four generated asset files exactly. Across 112 native host frames, reference/optimized raster outputs match; the fixed-versus-float depth comparison also has zero differing pixels. One/two-worker smooth-color output is identical, and an isolated test checks dither phase across odd/even tile origins.

Source and detailed evidence: `{OUT.relative_to(ROOT)}/`. Exact app binary SHA-256: `{m['firmware_sha256']}`. Current firmware/asset/source identity is recorded in `build-identity.json`.

## Remaining acceptance work

This run is **{seconds/60:.1f} minutes**, not the required two-hour acceptance test. Earlier Labubu and Ditto runs were deliberately interrupted for the avatar replacement and fade correction and are not completed acceptance results. Secured Wi-Fi association/reconnection, the later API-key voice integration, a sustained two-hour run and hands-on panel/battery review remain separate items. Framebuffer captures cannot prove optical panel behavior.

The local preview is http://127.0.0.1:8782/ditto/. Its stills are real device captures; its motion video was rendered on the Mac with the embedded software renderer. The console is http://127.0.0.1:8781 when the USB test has released the device.
'''
(ROOT/'INTEGRATION-REPORT.md').write_text(report)
status=ROOT/'WORK-STATUS.md';text=status.read_text();a=text.index('## Running now');z=text.index('## Still open',a)
text=text[:a]+f'''## Latest sustained validation

The final 10-minute check completed: **{m['average_fps']:.2f} FPS**, **{m['frames_within25_percent']:.2f}% of frames within 25 ms**, **{missed} frames above 33.333 ms**, and **zero display/audio errors or unexpected resets** over {seconds:.1f} seconds. The exact record and remaining gates are in `INTEGRATION-REPORT.md` and `test-results/ditto-integration/final-metrics.json`.

The preview is at http://127.0.0.1:8782/ditto/. The persistent console resumes at http://127.0.0.1:8781 after exclusive tests finish.

'''+text[z:];status.write_text(text)
progress={'running':False,'elapsed_s':s['workload_duration_s'],'target_s':s['requested_duration_s'],'completed_requested_duration':True,'last':samples[-1],'errors':s['errors'],'summary':m}
(OUT/'soak-progress.json').write_text(json.dumps(progress,indent=2)+'\n')
gallery=ROOT.parents[2]/'outputs/esp32-amoled-analysis/native-resolution/ditto'
shutil.copy2(OUT/'home-final.png',gallery/'home.png')
print(json.dumps(m,indent=2))
