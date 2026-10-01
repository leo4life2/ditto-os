"""Capture real composited screens and sample steady active animation."""
import json, statistics, sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'host'))
from device import Device
OUT = Path(__file__).resolve().parents[1] / 'test-results'
with Device() as d:
    time.sleep(2)
    original = d.call('system.status')
    d.sync_clock()
    d.call('app.home')
    d.call('avatar.expression', {'name': 'neutral'})
    samples = []
    for i in range(14):
        time.sleep(.5)
        if i == 5:
            d.call('avatar.emote', {'name': 'Wave'})
        s = d.call('system.status')
        samples.append({k: s[k] for k in ('fps', 'frame_ms', 'frame_p95_ms',
            'bin_ms', 'draw_ms', 'anim_ms', 'skin_ms', 'display_errors',
            'internal_free', 'psram_free')})
    d.call('avatar.emote', {'name': 'Idle'})
    time.sleep(.4)
    d.screenshot(OUT / 'layered-home.png')
    for app, tool, value in [('volume', 'audio.volume.set', 40), ('brightness', 'display.brightness.set', 65)]:
        d.call(tool, {'percent': value})
        time.sleep(.6)
        d.screenshot(OUT / f'layered-{app}.png')
    d.call('wifi.draft.set', {'ssid': 'Studio', 'password': 'mangoriver7!'})
    d.call('wifi.password.edit', {'index': 5, 'delete_count': 1, 'insert': 'R'})
    time.sleep(.4)
    d.screenshot(OUT / 'layered-wifi.png')
    fresh = d.call('wifi.scan')
    assert fresh['draft_ssid'] == '' and fresh['password_length'] == 0
    d.wait_event('wifi.scan.result', 20)
    d.call('audio.volume.set', {'percent': original['volume']})
    d.call('display.brightness.set', {'percent': original['brightness']})
    d.call('app.home')
    report = {'samples': samples,
        'median_fps': statistics.median(s['fps'] for s in samples),
        'median_frame_ms': statistics.median(s['frame_ms'] for s in samples),
        'display_errors': d.call('system.status')['display_errors']}
    (OUT / 'layout-profile.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))
