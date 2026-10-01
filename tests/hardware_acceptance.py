"""Exercise real firmware actions through the same transport the AI will use.
No credentials or cloud service required; run only with the intended board.
"""
import base64, json, math, statistics, sys, time, wave
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'host'))
from device import Device,DeviceError
from avatar_catalog import CLIPS
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'test-results';OUT.mkdir(exist_ok=True)
results=[]
def record(name,fn):
    start=time.monotonic()
    try:
        data=fn();results.append({'test':name,'ok':True,'seconds':round(time.monotonic()-start,3),'data':data});print('PASS',name,flush=True)
    except Exception as e:
        results.append({'test':name,'ok':False,'error':str(e)});print('FAIL',name,str(e),flush=True)
    (OUT/'acceptance.json').write_text(json.dumps(results,indent=2)+'\n')
def require(condition,message):
    if not condition:raise AssertionError(message)

with Device() as d:
    time.sleep(2)
    original=d.call('system.status')
    record('clock synchronized to Mac',lambda:require(abs(d.sync_clock()['epoch']-time.time())<2,'Clock drift'))
    def settings():
        for tool,field,values in [('audio.volume.set','volume',(0,40,70,30)),('display.brightness.set','brightness',(1,30,80,55))]:
            for n in values:require(d.call(tool,{'percent':n})[field]==n,'Setting mismatch')
        for n in (-1,101,2.5,'loud',None):
            for tool in ('audio.volume.set','display.brightness.set'):
                try:d.call(tool,{'percent':n});raise AssertionError('Invalid value accepted')
                except DeviceError:pass
        time.sleep(.5);d.screenshot(OUT/'brightness.png')
    record('settings boundaries and readback',settings)
    def network():
        d.call('wifi.scan');r=d.wait_event('wifi.scan.result',20);require(r['ok'],'Scan failed')
        networks=len(r['networks'])
        for invalid in (None,False,23,[]):
            try:d.call('wifi.draft.set',{'ssid':'Invalid draft','password':invalid});raise AssertionError('Invalid password type accepted')
            except DeviceError:pass
        s=d.call('wifi.draft.set',{'ssid':'Scout test network','password':'example12'})
        require(s['password_length']==9,'Draft length')
        s=d.call('wifi.password.edit',{'index':7,'delete_count':2,'insert':'345'})
        require(s['password_length']==10 and s['edit_index']==7,'Replacement range')
        for args in ({'index':99,'delete_count':0,'insert':'a'},{'index':-1,'delete_count':1,'insert':'a'}):
            try:d.call('wifi.password.edit',args);raise AssertionError('Bad edit accepted')
            except DeviceError:pass
        time.sleep(.4);d.screenshot(OUT/'wifi-edit.png')
        d.call('wifi.draft.set',{'ssid':'Scout test network','password':''})
        fresh=d.call('wifi.scan');require(fresh['draft_ssid']=='' and fresh['password_length']==0,'Rescan retained an old draft')
        require(d.wait_event('wifi.scan.result',20)['ok'],'Rescan failed')
        fresh=d.call('system.status')
        require(fresh['wifi_scan_done'] and not fresh['wifi_scanning'] and not fresh['wifi_scan_failed'],'Scan completion state')
        require(not fresh['wifi_error'],'Rescan retained old failure state')
        try:d.call('wifi.password.edit',{'index':0,'delete_count':0,'insert':'orphan'});raise AssertionError('Edit without network accepted')
        except DeviceError:pass
        # A real asynchronous scan must not erase a newer spoken selection.
        d.call('wifi.scan')
        chosen=d.call('wifi.draft.set',{'ssid':'Chosen while scanning','password':'example12'})
        d.wait_event('wifi.scan.result',20)
        fresh=d.call('system.status')
        require(fresh['draft_ssid']=='Chosen while scanning' and fresh['password_length']==9 and fresh['caption']==chosen['caption'],'Late scan overwrote newer selection')
        # Likewise, a late completion cannot overwrite another surface.
        d.call('wifi.scan');d.call('app.home');d.wait_event('wifi.scan.result',20)
        require(d.call('system.status')['caption']=='Ready for you','Late scan overwrote home')
        return {'networks_detected':networks,'join':'not tested: no credentials supplied'}
    record('Wi-Fi scan and spoken-password editing actions',network)
    def microphone():
        d.call('audio.microphone.set',{'muted':False});d.call('audio.capture.start');ev=d.wait_event('audio.input',3);require(len(base64.b64decode(ev['pcm']))==640,'Audio format')
        d.call('audio.microphone.set',{'muted':True});time.sleep(.15)
        while not d.events.empty():d.events.get()
        try:d.wait_event('audio.input',.4);raise AssertionError('Muted audio transmitted')
        except DeviceError:pass
        d.call('audio.microphone.set',{'muted':False});d.wait_event('audio.input',3)
        d.call('audio.capture.stop');return {'format':'16 kHz, mono, signed 16-bit PCM','mute_suppresses_stream':True}
    record('microphone capture, mute, and resume',microphone)
    def audio():
        while not d.events.empty():d.events.get()
        d.call('audio.capture.start');pcm=bytearray();deadline=time.monotonic()+1.5;d.call('audio.tone')
        while time.monotonic()<deadline:
            try:pcm.extend(base64.b64decode(d.wait_event('audio.input',.4)['pcm']))
            except DeviceError:break
        d.call('audio.capture.stop')
        with wave.open(str(OUT/'device-tone-capture.wav'),'wb') as f:f.setnchannels(1);f.setsampwidth(2);f.setframerate(16000);f.writeframes(pcm)
        # Push PCM through the real host -> device playback path as well.
        for j in range(10):
            import struct
            samples=[int(1800*math.sin(2*math.pi*440*(j*320+i)/16000)) for i in range(320)]
            d.call('audio.play',{'pcm':base64.b64encode(struct.pack('<320h',*samples)).decode()})
        d.call('audio.clear');s=d.call('system.status');require(s['audio_rx_errors']==0 and s['audio_tx_errors']==0,'Codec errors')
        return {'captured_samples':len(pcm)//2,'rx_errors':s['audio_rx_errors'],'tx_errors':s['audio_tx_errors'],'acoustic_quality':'requires waveform analysis'}
    record('speaker playback and microphone capture together',audio)
    def timer():
        d.call('timer.start',{'seconds':2});r=d.wait_event('timer.finished',5);require(r['event']=='timer.finished','Timer event')
        d.call('timer.start',{'seconds':60});require(d.call('timer.cancel')['timer_remaining']==0,'Timer not cancelled')
    record('timer completion and cancellation',timer)
    def apps():
        for name in ('volume','brightness','wifi','microphone','timer','system','home'):
            require(d.call('app.open',{'name':name})['app']==name,'Wrong app')
        for mode in ('listening','thinking','speaking','idle','offline'):
            s=d.call('voice.state.set',{'state':mode,'caption':'Ready for you'});require(s['conversation']==mode,'Wrong voice state')
        d.call('voice.state.set',{'state':'idle'});d.call('app.home');time.sleep(.4);d.screenshot(OUT/'home.png')
    record('app lifecycle and injected conversation states',apps)
    def animation():
        stats=[]
        for name in CLIPS:
            d.call('avatar.emote',{'name':name});time.sleep(.45);s=d.call('system.status');stats.append({k:s[k] for k in ('clip','fps','frame_ms','frame_p95_ms','anim_ms','skin_ms','bin_ms','draw_ms','display_errors')})
            require(s['clip']==name,'Wrong animation playing')
            require(s['display_errors']==0,'Display transfer error')
        d.call('avatar.emote',{'name':'Idle'});return stats
    record('all fourteen live animation clips',animation)
    def sleep():
        require(d.call('display.sleep')['sleeping'],'Sleep state');time.sleep(.2);require(not d.call('display.wake')['sleeping'],'Wake state')
    record('display sleep and wake',sleep)
    d.call('audio.capture.stop');d.call('audio.volume.set',{'percent':original['volume']});d.call('display.brightness.set',{'percent':original['brightness']});d.call('audio.microphone.set',{'muted':original['muted']});d.call('app.home');d.sync_clock()
    record('final health',lambda:d.call('system.status'))
print(json.dumps({'passed':sum(r['ok'] for r in results),'failed':sum(not r['ok'] for r in results)},indent=2))
sys.exit(any(not r['ok'] for r in results))
