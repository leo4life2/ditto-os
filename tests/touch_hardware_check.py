"""Real firmware, injected touch samples, actual captures and idle/wake checks.

Injected samples exercise the shared input path, not the physical digitizer.
The report retains physical contact/read counters separately.
"""
import json,math,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'host'))
from device import Device,DeviceError
OUT=ROOT/'test-results/ditto-emotes-0.6.0';OUT.mkdir(exist_ok=True)
report={'touch_diagnostics':[]}
def wait(d,predicate,seconds=4):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        s=d.call('system.status')
        if predicate(s):return s
        time.sleep(.05)
    raise AssertionError(s)
def touch(d,down,x=205,y=290):
    return d.call('debug.touch',{'down':down,'x':x,'y':y})

with Device() as d:
    d.sync_clock();original=d.call('system.status');assert original['version']=='0.6.0'
    d.call('app.home');d.call('display.brightness.set',{'percent':61});d.call('app.home')
    try:
        d.call('display.sleep');s=wait(d,lambda s:s['sleeping'] and s['effective_brightness']==10 and s['clip']=='Sleep')
        time.sleep(.7);d.screenshot(OUT/'sleep-device.png')
        report['explicit_sleep']={'brightness':s['effective_brightness'],'clip':s['clip']}
        first=d.call('system.status');start=time.monotonic();touch(d,True)
        s=wait(d,lambda s:not s['sleeping'] and s['effective_brightness']==61 and s['clip']!='Sleep')
        report['wake']={'latency_ms':round((time.monotonic()-start)*1000,1),'restored_brightness':s['effective_brightness']}
        touch(d,False)
        d.screenshot(OUT/'wake-device.png')
        # A material patch is pulled downward while the rest of the body lags.
        touch(d,True,205,210)
        for i in range(45):
            touch(d,True,205,min(325,210+i*3));time.sleep(.025)
        s=d.call('system.status')
        assert s['slime_stretch']>.45 and s['slime_press']>.2 and s['face_x_eyes']>.85,s
        report['local_squish']={k:s[k] for k in ('slime_stretch','slime_press','face_x_eyes')}
        d.screenshot(OUT/'squish-device.png');touch(d,False,205,325)
        s=wait(d,lambda s:s['face_x_eyes']<.1 and s['face_side_eye']>.5,seconds=1.4)
        assert s['face_x_eyes']<.1 and s['face_side_eye']>.5,s
        report['expressive_recovery']={k:s[k] for k in ('face_x_eyes','face_side_eye')}
        d.screenshot(OUT/'side-eye-device.png')
        time.sleep(1.5);s=d.call('system.status')
        assert s['face_side_eye']<.1 and s['face_x_eyes']<.01,s
        d.screenshot(OUT/'open-eyes-device.png')
        peaks=[];tap_states=[]
        for side in (105,300):
            touch(d,True,side,285);time.sleep(.045);touch(d,False,side,285)
            time.sleep(.07);tap=d.call('system.status');tap_states.append(tap);peaks.append(tap['physics_activity']);time.sleep(1.2)
        assert min(peaks)>.03,peaks
        assert tap_states[0]['tap_hit'] and tap_states[1]['tap_hit'],tap_states
        assert tap_states[0]['tap_local_x']<tap_states[1]['tap_local_x'],tap_states
        assert tap_states[0]['tap_bounce_x']>0 and tap_states[1]['tap_bounce_x']<0,tap_states
        assert tap_states[1]['tap_count']>tap_states[0]['tap_count'],tap_states
        report['pokes']={'activity_peaks':peaks,'states':[{k:s[k] for k in ('tap_count','tap_hit','tap_local_x','tap_local_y','tap_bounce_x','tap_bounce_y')} for s in tap_states]}
        d.call('avatar.expression',{'name':'neutral'})
        for emote in ('Wave','Point','Wink','Giggle','Celebrate'):
            d.call('avatar.emote',{'name':emote});time.sleep(1.6);d.screenshot(OUT/(emote.lower()+'-device.png'))
        d.call('avatar.emote',{'name':'Idle'})
        touch(d,True)
        for i in range(30):touch(d,True,205+3*i,290-i);time.sleep(.016)
        d.screenshot(OUT/'drag-device.png');touch(d,False,292,261)
        time.sleep(.13);s=d.call('system.status');assert s['physics_activity']>.015
        report['release']={'continued_activity':s['physics_activity']}
        for direction in (-1,1):
            touch(d,True,270,290)
            for i in range(80):
                a=direction*i*.14
                touch(d,True,int(205+65*math.cos(a)),int(290-65*math.sin(a)));time.sleep(.016)
            touch(d,False);time.sleep(.1)
        s=d.call('system.status');report['circles']={'activity':s['physics_activity'],'annoyance':s['annoyance']}
        assert s['annoyance']>.3
        d.screenshot(OUT/'annoyed-device.png')
        # A dropped host contact expires, never leaving the blob held forever.
        touch(d,True);s=wait(d,lambda s:not s['touch_down'],seconds=2)
        report['stale_contact_released']=True
        for bad in ({'down':True,'x':410,'y':200},{'down':True,'x':100,'y':-1}):
            try:d.call('debug.touch',bad)
            except DeviceError:pass
            else:raise AssertionError('Invalid touch accepted')
        print('Interaction checks passed; waiting for real 60-second idle timeout',flush=True)
        d.call('app.home');d.call('display.wake');time.sleep(62)
        s=d.call('system.status');assert s['sleeping'] and s['effective_brightness']==10 and s['clip']=='Sleep',s
        before=s;time.sleep(3);s=d.call('system.status')
        rate=(s['frames']-before['frames'])/(s['uptime_s']-before['uptime_s'])
        assert .5<rate<1.5,rate
        report['automatic_sleep']={'brightness':s['effective_brightness'],'clip':s['clip'],'draw_fps':rate}
        d.screenshot(OUT/'automatic-sleep-device.png')
        touch(d,True,50,60);s=wait(d,lambda s:not s['sleeping'] and s['effective_brightness']==61);touch(d,False,50,60)
        report['corner_tap_wake']=True
        report['touch_controller']={k:s[k] for k in ('touch_available','touch_samples','touch_errors','touch_contacts','touch_retries','touch_last_invalid_report')}
        assert s['touch_available'] and s['touch_errors']==0
        before=s;d.call('debug.touch.reset')
        s=wait(d,lambda s:s['touch_recoveries']>before['touch_recoveries'] and s['touch_available'] and s['touch_samples']>before['touch_samples'])
        report['digitizer_restart']={'before':before['touch_recoveries'],'after':s['touch_recoveries'],'valid_samples_after':s['touch_samples']-before['touch_samples'],'errors':s['touch_errors']}
        assert s['touch_errors']==0
    finally:
        report['last_observed_state']=d.call('system.status');report['logs']=list(d.logs)
        (OUT/'touch-hardware.json').write_text(json.dumps(report,indent=2)+'\n')
        touch(d,False);d.call('display.brightness.set',{'percent':original['brightness']});d.call('app.home');d.call('display.wake')
    report['final']=d.call('system.status')
    d.screenshot(OUT/'home-device.png')
(OUT/'touch-hardware.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
