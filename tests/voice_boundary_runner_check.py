"""Offline orchestration checks for the hardware runner; no board is opened."""
import json
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

import voice_boundary_hardware_check as runner
from device import Device


class Wire:
    def __init__(self, device, lose_volume=False):
        self.device = device
        self.lose_volume = lose_volume
        self.state = dict(version='fixture', volume=30, conversation='idle',
                          audio_stream=False, app='home', display_errors=0,
                          audio_rx_errors=0, audio_tx_errors=0, audio_overflows=0,
                          audio_power_errors=0, te_timeouts=0, scan_deadline_misses=0)

    def write(self, packet):
        request = json.loads(packet)
        tool, arguments = request['tool'], request['arguments']
        if tool == 'audio.volume.set' and not self.lose_volume:
            self.state['volume'] = arguments['percent']
            self.state['app'] = 'volume'
        elif tool == 'voice.state.set':
            self.state['conversation'] = arguments['state']
        elif tool in ('audio.capture.start', 'audio.capture.stop'):
            self.state['audio_stream'] = tool.endswith('.start')
        elif tool == 'app.home':
            self.state['app'] = 'home'
        result = dict(self.state) if tool == 'system.status' else {}
        self.device.pending[request['id']].put({'ok': True, 'result': result})
        return len(packet)


class UnguardedDevice(Device):
    def call(self, *args, **kwargs):
        kwargs.pop('dispatch_guard', None)
        return super().call(*args, **kwargs)


def board(unguarded=False, lose_volume=False):
    cls = UnguardedDevice if unguarded else Device
    d = cls.__new__(cls)
    d.closed = False
    d.failure = None
    d.boot_count = 1
    d.pending = {}
    d.logs = []
    d.subscribers = []
    d.lock = threading.Lock()
    d.serial = Wire(d, lose_volume)
    return d


checks = []
for repetition in range(3):
    d = board()
    report = {}
    saved = []
    runner.exercise(d, report, lambda: saved.append(json.loads(json.dumps(report))))
    assert report['passed'] and report['restored'] and len(report['checks']) == 4
    assert not d.pending and not d.subscribers and isinstance(d.serial, Wire)
    assert saved[-1] == report
checks.append('Successful callback races, observed effects and cleanup; three repetitions')

for failure, options in [('unguarded dispatch', {'unguarded': True}),
                         ('acknowledged but absent action', {'lose_volume': True})]:
    d = board(**options)
    report = {}
    saved = []
    try:
        runner.exercise(d, report, lambda: saved.append(json.loads(json.dumps(report))))
    except AssertionError:
        pass
    else:
        raise AssertionError('Missed deliberately injected failure: ' + failure)
    assert not report['passed'] and report['errors'] and report['restored']
    assert not d.pending and not d.subscribers and isinstance(d.serial, Wire)
    assert saved[-1] == report
    checks.append(f'{failure} detected; failure retained and device state restored')

with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp) / 'existing.json'
    path.write_text('preserve me')
    result = subprocess.run([sys.executable, str(Path(runner.__file__)), str(path)],
                            capture_output=True, text=True)
    assert result.returncode != 0 and 'fresh output' in result.stderr
    assert path.read_text() == 'preserve me'
checks.append('Existing evidence refused before build verification or USB open')

print(json.dumps({'passed': True, 'checks': checks,
                  'scope': 'Runner with production host transport and simulated firmware replies; no hardware qualification'}, indent=2))
