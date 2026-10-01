"""A background timer must preserve newer foreground state and identify its run."""
import json,queue,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'host'),str(ROOT/'tools')]
from device import Device
from validation_context import evidence_directory
report={}
with Device() as d:
    time.sleep(2);initial=d.call('system.status')
    assert initial['password_length']==0,'A user password draft is active; leave it untouched'
    events=d.subscribe('timer.finished')
    def finished(timer_id,timeout=6):
        event=events.get(timeout=timeout);assert event['timer_id']==timer_id,event
        return event
    def no_event(seconds):
        try:event=events.get(timeout=seconds)
        except queue.Empty:return
        raise AssertionError(f'Canceled or replaced timer emitted a completion: {event}')
    try:
        d.call('voice.state.set',{'state':'idle'})
        old=d.call('timer.start',{'seconds':1})['timer_id']
        draft=d.call('wifi.draft.set',{'ssid':'Scout draft test','password':'draft-only-123'})
        finished(old);s=d.call('system.status')
        assert s['app']=='wifi' and s['caption']==draft['caption'] and s['password_length']==14,s
        report['wifi_draft_preserved']=True
        old=d.call('timer.start',{'seconds':1})['timer_id']
        volume=d.call('audio.volume.set',{'percent':initial['volume']})
        finished(old);s=d.call('system.status')
        assert s['app']=='volume' and s['caption']==volume['caption'],s
        report['newer_setting_preserved']=True
        old=d.call('timer.start',{'seconds':1})['timer_id'];d.call('app.home')
        finished(old);s=d.call('system.status')
        assert s['app']=='home' and s['caption']=="Time's up!",s
        report['home_stays_home']=True
        old=d.call('timer.start',{'seconds':1})['timer_id']
        d.call('voice.state.set',{'state':'listening','caption':'Listening to your correction'})
        finished(old);s=d.call('system.status')
        assert s['conversation']=='listening' and s['caption']=='Listening to your correction',s
        d.call('voice.state.set',{'state':'idle'})
        report['conversation_preserved']=True
        old=d.call('timer.start',{'seconds':1})['timer_id']
        new=d.call('timer.start',{'seconds':3})['timer_id'];assert new!=old
        no_event(1.4);assert d.call('system.status')['timer_remaining']>0
        finished(new);report['replacement_identified']=True
        old=d.call('timer.start',{'seconds':1})['timer_id']
        s=d.call('timer.cancel');assert s['timer_id']!=old and s['timer_remaining']==0
        no_event(1.4);report['cancellation_suppresses_completion']=True
    finally:
        d.call('timer.cancel');d.call('voice.state.set',{'state':'idle'})
        d.call('wifi.scan');d.wait_event('wifi.scan.result',20);d.call('app.home');d.unsubscribe(events)
(evidence_directory(ROOT)/'timer-navigation-check.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
