"""Check response-owned audio/actions/state over USB; no cloud or acoustic claim."""
import argparse
import hashlib
import json
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'host'), str(ROOT / 'tools')]
from build_provenance import verify
from device import Device
from voice_transport import VoiceTransport, ResponseCancelled
import serial


class ObservedLock:
    def __init__(self, lock):
        self.lock = lock
        self.waiting = threading.Event()

    def __enter__(self):
        if threading.current_thread().name == 'voice-boundary-callback':
            self.waiting.set()
        self.lock.acquire()
        return self

    def __exit__(self, *_):
        self.lock.release()


class ObservedSerial:
    """Observe outgoing command names only; never retain PCM or credentials."""
    def __init__(self, serial):
        self.serial = serial
        self.tools = []

    def __getattr__(self, name):
        return getattr(self.serial, name)

    def write(self, packet):
        self.tools.append(json.loads(packet)['tool'])
        return self.serial.write(packet)


def spawn(function, name):
    result, errors = [], []

    def run():
        try:
            result.append(function())
        except Exception as error:
            errors.append(error)

    thread = threading.Thread(target=run, name=name, daemon=True)
    thread.start()
    return thread, result, errors


def join(worker):
    worker[0].join(5)
    assert not worker[0].is_alive(), 'Callback or interruption did not finish'


def exercise(device, report, save=lambda: None):
    original_lock, original_serial = device.lock, device.serial
    observed_lock = ObservedLock(original_lock)
    observed_serial = ObservedSerial(original_serial)
    voice = VoiceTransport(device)
    baseline = device.call('system.status')
    saved_volume = baseline['volume']
    new_volume = saved_volume + 1 if saved_volume < 100 else saved_volume - 1
    boot = device.boot_count
    fields = ('display_errors', 'audio_rx_errors', 'audio_tx_errors',
              'audio_overflows', 'audio_power_errors', 'te_timeouts', 'scan_deadline_misses')
    report.update(version=baseline['version'], checks=[], errors=[], passed=False)
    device.lock, device.serial = observed_lock, observed_serial
    try:
        voice.start()
        for kind, tool in [('action', 'audio.volume.set'),
                           ('state', 'voice.state.set'), ('audio', 'audio.play')]:
            voice.set_state('listening', 'Boundary check')
            token = voice.begin_speech()

            def callback():
                if kind == 'action':
                    return voice.skill('audio_volume_set', {'percent': new_volume}, speech_id=token)
                if kind == 'state':
                    return voice.set_state('thinking', 'Obsolete response', speech_id=token)
                return voice.write_audio(bytes(640), speech_id=token)

            # The real Device.call is now waiting for its serial lock. The
            # provider's callback passed the earlier ownership check already.
            original_lock.acquire()
            observed_lock.waiting.clear()
            start = len(observed_serial.tools)
            worker = spawn(callback, 'voice-boundary-callback')
            cancel = None
            try:
                assert observed_lock.waiting.wait(2), 'Callback never reached serial dispatch'
                cancel = spawn(voice.interrupt, 'voice-boundary-interrupt')
                deadline = time.monotonic() + 2
                while voice._speech_id == token and time.monotonic() < deadline:
                    time.sleep(.001)
                assert voice._speech_id != token, 'Cancellation waited for the queued callback'
            finally:
                original_lock.release()
                join(worker)
                if cancel is not None:
                    join(cancel)
            assert cancel is not None and not cancel[2], 'Interruption failed'
            assert tool not in observed_serial.tools[start:], 'Canceled callback reached USB'
            if kind == 'audio':
                assert worker[1] == [False] and not worker[2]
            else:
                assert len(worker[2]) == 1 and isinstance(worker[2][0], ResponseCancelled)
            state = device.call('system.status')
            assert state['volume'] == saved_volume and state['conversation'] == 'listening'
            report['checks'].append({'kind': kind, 'queued_callback_rejected': True,
                                     'canceled_writes': 0, 'observed_state_unchanged': True})
            save()

        # Late callbacks and genuinely new callbacks share the same dispatcher.
        old_token = token
        current = voice.begin_speech()
        start = len(observed_serial.tools)
        for function in [
            lambda: voice.skill('audio_volume_set', {'percent': new_volume}, speech_id=old_token),
            lambda: voice.set_state('thinking', speech_id=old_token),
        ]:
            try:
                function()
            except ResponseCancelled:
                pass
            else:
                raise AssertionError('Late action/state accepted')
        assert voice.write_audio(bytes(640), speech_id=old_token) is False
        assert len(observed_serial.tools) == start, 'Late callback sent a command'
        voice.skill('audio_volume_set', {'percent': new_volume}, speech_id=current)
        voice.set_state('thinking', 'New response', speech_id=current)
        assert voice.write_audio(bytes(640), speech_id=current) is True
        state = device.call('system.status')
        assert state['volume'] == new_volume and state['conversation'] == 'thinking'
        voice.interrupt()
        assert device.call('system.status')['volume'] == new_volume, 'Cancellation rolled back an accepted action'
        report['checks'].append({'late_callbacks_rejected': True, 'new_callbacks_accepted': True,
                                 'already_applied_action_preserved': True})
        save()
    except BaseException as error:
        report['errors'].append(type(error).__name__ + ': ' + str(error))
        save()
        raise
    finally:
        for label, operation in [
            ('close voice', voice.close),
            ('restore volume', lambda: device.call('audio.volume.set', {'percent': saved_volume})),
            ('restore state', lambda: device.call('voice.state.set', {'state': 'idle'})),
            ('return home', lambda: device.call('app.home')),
        ]:
            try:
                operation()
            except Exception as error:
                report['errors'].append(label + ': ' + repr(error))
        device.lock, device.serial = original_lock, original_serial
        try:
            final = device.call('system.status')
            report['counter_increases'] = {key: final[key] - baseline[key] for key in fields}
            report['version_unchanged'] = final['version'] == baseline['version']
            report['reboot_count_delta'] = device.boot_count - boot
            report['restored'] = (final['volume'] == saved_volume and final['app'] == 'home'
                                  and final['conversation'] == 'idle' and not final['audio_stream'])
            if any(report['counter_increases'].values()) or report['reboot_count_delta']:
                report['errors'].append('Hardware counters changed or the device rebooted')
            if not report['restored'] or not report['version_unchanged']:
                report['errors'].append('State restoration or version check failed')
        except Exception as error:
            report['errors'].append('final status: ' + repr(error))
        report['passed'] = not report['errors'] and len(report['checks']) == 4
        save()
    assert report['passed'], report['errors']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Use a fresh output path; previous evidence will not be overwritten')
    proof = verify(ROOT)
    report = {'passed': False, 'expected_version': proof['version'],
              'expected_local_app_sha256': proof['artifacts']['build/scout_os.bin'],
              'scope': 'Real USB with silent PCM and injected callback scheduling; no provider, acoustic, physical-finger or performance claim',
              'identity_scope': 'Local sealed image plus reported firmware version; no runtime image-hash endpoint',
              'source_sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                                for p in [Path(__file__), ROOT/'host/device.py',
                                          ROOT/'host/voice_transport.py', ROOT/'host/skills.py']}}
    args.output.parent.mkdir(parents=True, exist_ok=True)

    def save():
        temporary = args.output.with_suffix(args.output.suffix + '.tmp')
        temporary.write_text(json.dumps(report, indent=2) + '\n')
        temporary.replace(args.output)

    save()
    try:
        with Device() as device:
            time.sleep(2)
            assert device.call('system.status')['version'] == proof['version'], 'Firmware version mismatch'
            try:
                competitor = Device(device.serial.port)
            except serial.SerialException as error:
                assert 'exclusively lock' in str(error), str(error)
            else:
                competitor.close()
                raise AssertionError('Second Scout reader acquired the active USB port')
            report['second_scout_usb_owner_rejected'] = True
            exercise(device, report, save)
    except BaseException as error:
        report['passed'] = False
        report.setdefault('errors', []).append(type(error).__name__ + ': ' + str(error))
        save()
        raise
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
