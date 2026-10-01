"""Qualify tap duration and real automatic rest; exclusive Scout USB ownership."""
import json
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'host'))
from device import Device

OUT = Path(sys.argv[1]).resolve()
OUT.mkdir(parents=True, exist_ok=False)
report = {'passed': False, 'scope': 'USB-injected tap; real IMU for automatic rest. Not optical latency or measured current.'}
faults = ['display_errors', 'audio_rx_errors', 'audio_tx_errors', 'audio_overflows',
          'audio_power_errors', 'scan_deadline_misses', 'te_timeouts']

def save():
    (OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')

with Device() as d:
    try:
        time.sleep(3)
        d.sync_clock()
        d.call('app.home')
        time.sleep(2)
        baseline = d.call('system.status')
        report['baseline'] = baseline
        boot = d.boot_count
        assert baseline['version'] == (ROOT / 'VERSION').read_text().strip()

        def tap():
            start = time.monotonic()
            d.call('debug.touch', {'down': True, 'x': 202, 'y': 306})
            time.sleep(.10)
            d.call('debug.touch', {'down': False, 'x': 202, 'y': 306})
            return start

        at = tap()
        samples = []
        while time.monotonic() - at < 4.6:
            s = d.call('system.status')
            s['test_time'] = time.monotonic() - at
            samples.append(s)
            time.sleep(.025)
        report['tap_samples'] = samples
        extended = [s for s in samples if 1.3 < s['test_time'] < 2.05]
        opened = [s for s in samples if 2.6 < s['test_time'] < 2.9]
        assert len(extended) > 8 and all(s['face_oval'] > .98 and s['face_melon'] < .01 for s in extended)
        assert len(opened) > 3 and all(s['face_melon'] > .98 for s in opened)
        assert samples[-1]['behavior'] == 'idle' and samples[-1]['face_melon'] < .01
        assert samples[-1]['tap_count'] == baseline['tap_count'] + 1
        print('Extended tap mouth and return to idle passed.', flush=True)

        # Capture separately so USB image transfer does not alter the timing test.
        at = tap()
        time.sleep(max(0, 1.35 - (time.monotonic() - at)))
        report['oval_capture_status'] = d.call('system.status')
        d.screenshot(OUT / 'extended-oval.png')
        time.sleep(max(0, 2.65 - (time.monotonic() - at)))
        report['open_capture_status'] = d.call('system.status')
        d.screenshot(OUT / 'open-mouth.png')
        time.sleep(max(0, 4.6 - (time.monotonic() - at)))
        d.screenshot(OUT / 'idle.png')

        # Do not force display.sleep. Keep real sensors enabled and cross the
        # normal deadline, including a clock synchronization while inactive.
        rest = []
        start = time.monotonic()
        parked = None
        synced = False
        while time.monotonic() - start < 180:
            s = d.call('system.status')
            rest.append(s)
            report['rest_samples'] = rest
            assert not s['motion_simulated'] and s['imu_available']
            assert d.boot_count == boot
            if not synced and s['idle_for_s'] >= 30:
                report['clock_sync_during_rest'] = d.sync_clock()
                synced = True
            if s['sleeping'] and s['effective_brightness'] == 10 and s['power_saving']:
                if parked is None:
                    parked = s
                elif s['uptime_s'] - parked['uptime_s'] > 3:
                    assert s['frames'] == parked['frames']
                    assert s['physics_steps'] == parked['physics_steps']
                    assert s['audio_input_blocks'] == parked['audio_input_blocks']
                    assert s['imu_samples'] > parked['imu_samples']
                    assert s['sleep_overlay_frames'] > parked['sleep_overlay_frames']
                    report['automatic_sleep'] = {'start': parked, 'end': s}
                    break
            else:
                parked = None
            if len(rest) % 10 == 0:
                print('Quiet observation:', round(s['idle_for_s'], 1), 'seconds; sleeping:', s['sleeping'], flush=True)
                save()
            time.sleep(.5)
        assert 'automatic_sleep' in report, 'No uninterrupted quiet interval; inspect real touch/motion samples'
        d.screenshot(OUT / 'automatic-sleep.png')
        # Wake through the normal touch path; the next quiet rest is left to
        # happen naturally after reconnecting the host bridge.
        d.call('debug.touch', {'down': True, 'x': 50, 'y': 60})
        time.sleep(.07)
        d.call('debug.touch', {'down': False, 'x': 50, 'y': 60})
        time.sleep(.4)
        final = d.call('system.status')
        report['final'] = final
        assert not final['sleeping'] and final['effective_brightness'] == baseline['brightness']
        report['fault_deltas'] = {k: final[k] - baseline[k] for k in faults}
        assert not any(report['fault_deltas'].values())
        report['passed'] = True
        print('Automatic sleep, clock sync during rest, parked work, and injected tap wake passed.', flush=True)
    except Exception as e:
        report['error'] = repr(e)
        raise
    finally:
        report['logs'] = list(d.logs)
        save()
