"""Repeatable fast shaking through the real firmware; restore real sensors."""
import json, math, os, sys, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'host'),str(ROOT/'tools')]
from device import Device
from validation_context import evidence_directory
OUT=evidence_directory(ROOT)
report={'runs':[]}
with Device() as d:
    time.sleep(2);d.sync_clock();d.call('app.home');d.call('avatar.emote',{'name':'Idle'})
    original=d.call('system.status')
    assert original['version']==os.environ.get('SCOUT_EXPECTED_VERSION','0.8.1')
    def drive(axis,hz,seconds,amplitude=2):
        start=time.monotonic();deadline=start;states=[];count=0
        while time.monotonic()-start<seconds:
            t=time.monotonic()-start;acc=[0,0,1]
            acc[axis]+=amplitude*math.sin(2*math.pi*hz*t)
            d.call('debug.motion',{'acceleration':acc,'gyro':[0,0,0]})
            count+=1
            if count%2==0:states.append(d.call('system.status'))
            deadline+=1/60
            time.sleep(max(0,deadline-time.monotonic()))
        return states,count/(time.monotonic()-start)
    try:
        drive(0,0,1)
        for axis in range(3):
            for hz in (4,6):
                base=d.call('system.status');states,rate=drive(axis,hz,3)
                end=d.call('system.status');values=[s['motion_wave'][axis] for s in states]
                fps=(end['frames']-base['frames'])/(end['uptime_s']-base['uptime_s'])
                run={'axis':axis,'hz':hz,'injection_hz':rate,'fps':fps,'minimum':min(values),'maximum':max(values),'start':base,'end':end,'samples':states}
                report['runs'].append(run)
                assert rate>35 and len(states)>30,run
                assert min(values)<-.02 and max(values)>.02,run
                assert fps>28,run
                assert all(end[k]==base[k] for k in ('display_errors','audio_rx_errors','audio_tx_errors','audio_overflows')),run
                # No driving after this; the stored velocity must carry a tail.
                tail,_=drive(axis,0,4.5,0)
                run['after_wobble_peak']=max(abs(s['motion_wave'][axis]) for s in tail[:10])
                run['settled']=tail[-1]['motion_wave']
                assert max(abs(v) for v in tail[-1]['motion_wave'])<.012,run
        d.screenshot(OUT/'shake-settled-device.png')
        d.call('display.sleep');drive(0,5,.4)
        s=d.call('system.status');assert not s['sleeping'],s
        drive(0,0,1);d.call('display.sleep');drive(0,0,2)
        s=d.call('system.status');assert s['sleeping'] and s['effective_brightness']==10 and max(abs(v) for v in s['motion_wave'])==0
        d.call('debug.motion',{'enabled':False});d.call('debug.touch',{'down':True,'x':50,'y':60});d.call('debug.touch',{'down':False,'x':50,'y':60})
        # Brightness is applied by the UI task, after the touch command reply.
        deadline=time.monotonic()+1
        while True:
            s=d.call('system.status')
            if not s['sleeping'] and s['effective_brightness']==original['brightness']:break
            assert time.monotonic()<deadline,s
            time.sleep(.025)
        report['sleep_wake']=True
    finally:
        d.call('debug.motion',{'enabled':False});d.call('app.home');d.call('display.wake');time.sleep(.5)
        report['final']=d.call('system.status');report['logs']=d.logs
        (OUT/'shake-hardware-check.json').write_text(json.dumps(report,indent=2)+'\n')
        d.screenshot(OUT/'shake-home-device.png')
    assert not report['final']['motion_simulated'] and report['final']['imu_available'] and report['final']['touch_available']
print('4/6 Hz shaking on all three axes, live rendering, settling, sleep/wake and real-sensor restoration passed.')
