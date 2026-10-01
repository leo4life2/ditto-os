"""Exercise actual POSIX ownership/release using a PTY, never the ESP32 port."""
import json
import os
import pty
import select
import subprocess
import sys
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'host'))
from device import Device
import serial

# macOS PTYs do not implement the custom-baud IOSSIOSPEED ioctl. Keep the
# actual Device/parser/read thread and kernel locking, changing only this
# fixture's line speed to a standard termios rate. USB still uses 921600.
class PtySerial(serial.Serial):
    def __init__(self, *args, **kwargs):
        kwargs['baudrate'] = 9600
        super().__init__(*args, **kwargs)

serial.Serial = PtySerial

master, slave = pty.openpty()
port = os.ttyname(slave)
stop = threading.Event()
requests = []


def firmware():
    data = bytearray()
    while not stop.is_set():
        ready, _, _ = select.select([master], [], [], .1)
        if not ready:
            continue
        data.extend(os.read(master, 4096))
        while b'\n' in data:
            line, _, data = data.partition(b'\n')
            message = json.loads(line)
            requests.append(message['tool'])
            response = {'id': message['id'], 'ok': True, 'result': {'observed': message['tool']}}
            os.write(master, b'SCOUT '+json.dumps(response).encode()+b'\n')


worker = threading.Thread(target=firmware, daemon=True)
try:
    with Device(port) as first:
        worker.start()
        assert first.call('ownership.before') == {'observed': 'ownership.before'}
        try:
            second = Device(port)
        except serial.SerialException as error:
            assert 'exclusively lock' in str(error), str(error)
        else:
            second.close()
            raise AssertionError('Second Scout owner opened the same port')
        command = '''
import sys
sys.path.insert(0,sys.argv[1])
from device import Device
import serial
class PtySerial(serial.Serial):
    def __init__(self,*args,**kwargs):
        kwargs['baudrate']=9600
        super().__init__(*args,**kwargs)
serial.Serial=PtySerial
try:
    d=Device(sys.argv[2])
except serial.SerialException as e:
    assert 'exclusively lock' in str(e),str(e)
    print('second process rejected')
else:
    d.close()
    raise AssertionError('Second process acquired the occupied port')
'''
        result = subprocess.run([sys.executable, '-c', command, str(ROOT/'host'), port],
                                capture_output=True, text=True, timeout=5)
        assert result.returncode == 0 and 'second process rejected' in result.stdout, result.stderr
        assert first.call('ownership.after') == {'observed': 'ownership.after'}
        assert not first.failure and not first.pending
    with Device(port) as replacement:
        assert replacement.call('ownership.reopened') == {'observed': 'ownership.reopened'}
    assert requests == ['ownership.before', 'ownership.after', 'ownership.reopened'], requests
finally:
    stop.set()
    if worker.ident is not None:
        worker.join(1)
    os.close(master)
    os.close(slave)

print(json.dumps({'passed': True, 'same_process_competitor_rejected': True,
                  'second_process_competitor_rejected': True, 'original_connection_still_works': True,
                  'close_releases_ownership': True, 'scope': 'Real POSIX locks and production Device over a 9600-baud PTY; no ESP32, USB-rate or noncooperating-tool exclusion claim'}, indent=2))
