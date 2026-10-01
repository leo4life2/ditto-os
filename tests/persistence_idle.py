"""Verify reboot persistence, RTC time, and measured idle drawing on hardware."""
import json,sys,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'host'))
from device import Device
OUT=Path(__file__).resolve().parents[1]/'test-results'
report={}
with Device() as d:
    time.sleep(2);original=d.call('system.status');d.sync_clock()
    d.call('audio.volume.set',{'percent':42});d.call('display.brightness.set',{'percent':61});d.call('audio.microphone.set',{'muted':True});d.call('system.reboot');time.sleep(2)
    s=d.call('system.status');assert(s['volume'],s['brightness'],s['muted'])==(42,61,True)
    assert abs(s['epoch']-time.time())<3,(s['epoch'],time.time())
    report['persistence']={'volume':s['volume'],'brightness':s['brightness'],'muted':s['muted'],'rtc_error_s':round(s['epoch']-time.time(),2)}
    d.call('audio.volume.set',{'percent':original['volume']});d.call('display.brightness.set',{'percent':original['brightness']});d.call('audio.microphone.set',{'muted':original['muted']});d.call('app.home');d.call('voice.state.set',{'state':'idle','caption':'Ready for you'})
    time.sleep(62);before=d.call('system.status');time.sleep(4);after=d.call('system.status')
    rate=(after['frames']-before['frames'])/(after['uptime_s']-before['uptime_s']);assert 0<=rate<.5,rate
    assert before['sleeping'] and after['sleeping']
    assert after['physics_steps']==before['physics_steps']
    report['idle']={'draw_fps':rate,'physical_panel_refresh':'not measured'}
    t=time.monotonic();d.call('display.wake');time.sleep(.2);s=d.call('system.status');report['wake']={'awake':not s['sleeping'],'frames_since_idle':s['frames']-after['frames']};assert report['wake']['frames_since_idle']>=3
    d.sync_clock();d.screenshot(OUT/'home-restored.png')
(OUT/'persistence-idle.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
