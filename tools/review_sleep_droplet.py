"""Capture the actual sleeping overlay through the running local bridge."""
import argparse
import json
import time
import urllib.request
from pathlib import Path
from PIL import Image, ImageChops

p = argparse.ArgumentParser()
p.add_argument('output', type=Path)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
report = {'scope': 'Actual framebuffer; not optical timing, refraction or current measurement'}

def request(path, data=None):
    body = None if data is None else json.dumps(data).encode()
    req = urllib.request.Request('http://127.0.0.1:8781/' + path, data=body,
                                 headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=20) as response:
        return json.load(response)

def action(tool):
    return request('action', {'tool': tool, 'arguments': {}})

def capture(name):
    assert request('capture', {})['captured']
    with urllib.request.urlopen('http://127.0.0.1:8781/frame.png', timeout=20) as r:
        (a.output / name).write_bytes(r.read())

faults = ['display_errors', 'audio_rx_errors', 'audio_tx_errors',
          'audio_overflows', 'scan_deadline_misses', 'te_timeouts']
try:
    action('app.home')
    time.sleep(2)
    before = request('status')
    report['before'] = before
    assert before['version'] == (Path(__file__).resolve().parents[1] / 'VERSION').read_text().strip()
    capture('home.png')
    action('display.sleep')
    time.sleep(3)
    start = request('status')
    report['sleep_start'] = start
    assert start['sleeping'] and start['render_parked']
    assert start['effective_brightness'] == 10
    capture('sleep-a.png')
    time.sleep(1.2)
    capture('sleep-b.png')
    end = request('status')
    report['sleep_end'] = end
    assert end['sleeping'] and end['render_parked']
    for key in ['frames', 'physics_steps', 'audio_input_blocks']:
        assert end[key] == start[key], key
    report['overlay_updates'] = end['sleep_overlay_frames'] - start['sleep_overlay_frames']
    assert report['overlay_updates'] > 10
    ia, ib = [Image.open(a.output / f'sleep-{x}.png').convert('RGB') for x in ['a', 'b']]
    drool_box = (180, 276, 220, 332)
    diff = ImageChops.difference(ia.crop(drool_box), ib.crop(drool_box))
    report['changed_droplet_pixels'] = sum(px != (0, 0, 0) for px in diff.getdata())
    assert report['changed_droplet_pixels'] > 30
    ia.crop(drool_box).resize((240, 336), Image.Resampling.NEAREST).save(a.output / 'droplet-600pct.png')
    report['fault_deltas'] = {k: end[k] - before[k] for k in faults}
    assert not any(report['fault_deltas'].values())
    report['passed'] = True
finally:
    action('app.home')
    time.sleep(1)
    report['awake_a'] = request('status')
    time.sleep(1)
    report['awake_b'] = request('status')
    assert report['awake_b']['frames'] > report['awake_a']['frames']
    (a.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({k: report[k] for k in ['passed', 'overlay_updates', 'changed_droplet_pixels', 'fault_deltas']}))
