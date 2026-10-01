"""Bounded failure states and real USB reboot recovery; no cloud credentials."""
import json,sys,time
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from validation_context import evidence_directory
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'host'))
from device import Device,DeviceError
from connection import ManagedDevice
OUT=evidence_directory(ROOT);report={}
def wait_state(d,predicate,timeout):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        try:
            s=d.call('system.status',timeout=3)
            if predicate(s):return s
        except DeviceError:pass
        time.sleep(.25)
    raise AssertionError('Expected state did not arrive')
with Device() as d:
    time.sleep(2);d.sync_clock();original=d.call('system.status')
    d.call('app.home');d.call('audio.capture.start')
    d.call('voice.state.set',{'state':'speaking','caption':'Voice lease test'})
    s=wait_state(d,lambda s:s['conversation']=='offline' and not s['audio_stream'],11)
    assert not s['voice_active'] and s['caption']=='Voice disconnected'
    report['abandoned_voice_stops_within_11_seconds']=True
    print('PASS abandoned voice lease',flush=True)
    # Do not replace a real user's saved network in an automated failure test.
    if not original['ssid']:
        d.call('wifi.draft.set',{'ssid':'Scout-unavailable-test-6a9e','password':'not-a-real-password'})
        start=time.monotonic();d.call('wifi.connect');d.call('app.open',{'name':'volume'})
        s=wait_state(d,lambda s:bool(s['wifi_error']) and not s['wifi_connecting'],36)
        assert not s['wifi_connected'] and s['app']=='volume'
        report['unavailable_network']={'failed_after_s':round(time.monotonic()-start,2),'message':s['wifi_error'],'newer_surface_preserved':True}
        print('PASS bounded network failure and newer screen preserved',flush=True)
        d.call('app.open',{'name':'wifi'});d.screenshot(OUT/'wifi-failed.png')
        s=d.call('wifi.draft.set',{'ssid':'New draft','password':''})
        assert not s['wifi_error'],'New draft retained the failed network error'
        d.call('wifi.forget')
    else:report['unavailable_network']={'skipped':'Existing saved network preserved'}
    d.call('app.home')
time.sleep(1)
d=ManagedDevice()
try:
    before=wait_state(d,lambda s:True,15);generation=d.generation
    d.call('timer.start',{'seconds':60})
    try:d.call('system.reboot',timeout=5)
    except DeviceError:pass
    s=wait_state(d,lambda s:d.generation>generation and abs(s['epoch']-time.time())<3,25)
    assert s['timer_remaining']==0,'Uncertain pre-reboot action was replayed'
    assert not s['audio_stream'] and not s['voice_active']
    report['real_reboot_reconnect']={'generation_before':generation,'generation_after':d.generation,'clock_error_s':round(s['epoch']-time.time(),2),'no_timer_or_audio_replay':True}
    d.call('app.home');d.screenshot(OUT/'recovered-home.png')
finally:d.close()
(OUT/'recovery-hardware.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
