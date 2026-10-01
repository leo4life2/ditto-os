"""Rounded impacts: local dent/ring, whole-body impulse, drag and sleep reset."""
import json,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'host'));sys.path.insert(0,str(ROOT/'tools'))
from device import Device
from validation_context import evidence_directory
OUT=evidence_directory(ROOT);report={}
with Device() as d:
    time.sleep(2);d.sync_clock();d.call('app.home');d.call('avatar.emote',{'name':'Idle'});time.sleep(.5)
    original=d.call('system.status');assert original['version']==__import__('os').environ.get('SCOUT_EXPECTED_VERSION','0.8.1')
    states=[]
    try:
        for name,x,y in [('left',105,282),('top',205,192),('middle',220,327),('right',300,307)]:
            expected=d.call('system.status')['touch_presses']+1
            d.call('debug.touch',{'down':True,'x':x,'y':y})
            # USB acknowledges input before the UI consumes it. Observe the
            # rendered hit, within the short-tap window, rather than racing it.
            deadline=time.monotonic()+.18
            while True:
                s=d.call('system.status')
                if s['tap_seen_press']==expected:break
                assert time.monotonic()<deadline,s
                time.sleep(.008)
            states.append(s)
            assert s['tap_hit'] and abs(s['tap_depth'])>.005,s
            assert abs(s['body_squash'])<.002 and abs(s['slime_press'])<.002,s
            assert sum(s[k]*s[k] for k in ('tap_bounce_x','tap_bounce_y','tap_bounce_z'))>.000064,s
            d.call('debug.touch',{'down':False,'x':x,'y':y})
            time.sleep(.09)
            ring=d.call('system.status');assert ring['tap_ripple']<-.005,ring
            s['delayed_ripple']=ring['tap_ripple']
            d.screenshot(OUT/(name+'-tap-device.png'))
            time.sleep(2)
        report['tap_sites']=[{k:s[k] for k in ('tap_local_x','tap_local_y','tap_depth','delayed_ripple','tap_bounce_x','tap_bounce_y','tap_bounce_z','body_squash','slime_press','tap_count')} for s in states]
        base=d.call('system.status')
        for i in range(16):
            start=time.monotonic()
            d.call('debug.touch',{'down':True,'x':205,'y':290});time.sleep(.06)
            d.call('debug.touch',{'down':False,'x':205,'y':290})
            time.sleep(max(0,.5-(time.monotonic()-start)))
        end=d.call('system.status')
        report['tap_load']={'start':base,'end':end,'fps':(end['frames']-base['frames'])/(end['uptime_s']-base['uptime_s'])}
        assert report['tap_load']['fps']>28 and end['tap_count']-base['tap_count']==16,report['tap_load']
        assert end['display_errors']==base['display_errors'],report['tap_load']
        d.call('debug.touch',{'down':True,'x':205,'y':210})
        for i in range(38):
            d.call('debug.touch',{'down':True,'x':205,'y':min(320,210+3*i)});time.sleep(.025)
        s=d.call('system.status');assert s['slime_stretch']>.4 and s['slime_press']>.2,s
        report['drag']={k:s[k] for k in ('slime_stretch','slime_press','face_x_eyes')}
        d.call('debug.touch',{'down':False,'x':205,'y':320});time.sleep(.15)
        assert d.call('system.status')['physics_activity']>.015
        d.call('display.sleep');time.sleep(2);s=d.call('system.status')
        assert s['sleeping'] and s['effective_brightness']==10 and abs(s['tap_depth'])<.0001 and abs(s['tap_ripple'])<.0001 and abs(s['tap_bounce_z'])<.0001
        d.call('debug.touch',{'down':True,'x':50,'y':60});time.sleep(.1);d.call('debug.touch',{'down':False,'x':50,'y':60});time.sleep(.25)
        s=d.call('system.status');assert not s['sleeping'] and s['effective_brightness']==original['brightness'] and not s['tap_hit']
        report['sleep_and_corner_wake']=True
        assert s['touch_available']
        report['touch']={k:s[k] for k in ('touch_available','touch_errors','touch_retries','touch_recoveries')}
    finally:
        d.call('debug.touch',{'down':False,'x':205,'y':290});d.call('app.home');d.call('display.wake')
        report['final']=d.call('system.status');report['device_logs']=list(d.logs)
        (OUT/'sharp-tap-check.json').write_text(json.dumps(report,indent=2)+'\n')
    time.sleep(1);d.screenshot(OUT/'home-device.png')
print(json.dumps(report,indent=2))
