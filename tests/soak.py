"""Combined audio, networking, animation and command-latency hardware soak."""
import base64,json,os,sys,threading,time,statistics,signal
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from validation_context import evidence_directory
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'host'))
from device import Device
from voice_transport import VoiceTransport
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'test-results';OUT.mkdir(exist_ok=True)
CHECKPOINT=evidence_directory(ROOT)/'soak-progress.json'
duration=int(sys.argv[1]) if len(sys.argv)>1 else 180
with Device() as d:
    time.sleep(2);d.sync_clock();d.call('app.home')
    if os.environ.get('SCOUT_SLEEP_WAKE_BEFORE_SOAK')=='1':
        for _ in range(3):
            d.call('display.sleep');time.sleep(2)
            s=d.call('system.status');assert s['sleeping'] and s['audio_sleeping'] and s['effective_brightness']==10,s
            d.call('display.wake');time.sleep(.4)
            s=d.call('system.status');assert not s['audio_sleeping'] and s['effective_brightness']==s['brightness'],s
    voice=VoiceTransport(d);voice.start();voice.set_state('listening','Listening to you');time.sleep(1.1);baseline=d.call('system.status')
    if baseline['frames'] == 0:
        report={'error':'Renderer did not start','status':baseline,'device_logs':list(d.logs),'events':list(d.events.queue)}
        (OUT/'soak-startup-failure.json').write_text(json.dumps(report,indent=2)+'\n')
        voice.close()
        raise AssertionError('Renderer did not start; see soak-startup-failure.json')
    stop=threading.Event();errors=[];latencies=[];audio={'input_packets':0,'output_packets':0};samples=[]
    for sig in (signal.SIGINT,signal.SIGTERM):signal.signal(sig,lambda *_:stop.set())
    def input_worker():
        try:
            while not stop.is_set():
                if voice.read_audio():audio['input_packets']+=1
        except Exception as e:errors.append(str(e))
    def output_worker():
        deadline=time.monotonic()
        try:
            while not stop.is_set():
                voice.write_audio(bytes(640));audio['output_packets']+=1;deadline+=.020;stop.wait(max(0,deadline-time.monotonic()))
        except Exception as e:errors.append(str(e))
    threads=[threading.Thread(target=f,daemon=True) for f in (input_worker,output_worker)]
    for t in threads:t.start()
    start=time.monotonic();next_scan=10;next_emote=5;names=['Wave','Yes','Happy','No','Celebrate','Idle'];ix=0
    while not stop.is_set() and time.monotonic()-start<duration:
        elapsed=time.monotonic()-start
        if elapsed>=next_scan:
            try:d.call('wifi.scan');d.wait_event('wifi.scan.result',15);d.call('app.home')
            except Exception as e:errors.append(str(e))
            next_scan+=45
        if elapsed>=next_emote:d.call('avatar.emote',{'name':names[ix%len(names)]});ix+=1;next_emote+=12
        t=time.monotonic();s=d.call('system.status');latencies.append((time.monotonic()-t)*1000)
        sample={k:s[k] for k in ('raster0_ms','raster1_ms','anim_ms','skin_ms','bin_ms','draw_ms','diff_ms','copy_ms','dma_wait_ms','submit_ms','uptime_s','fps','frame_ms','frame_p95_ms','frames','frames_within25','missed_frames','internal_free','psram_free','display_errors','audio_rx_errors','audio_tx_errors','audio_overflows','compose_ms','text_ms','upload_ms','ui_stack_free','render_stack_free','control_stack_free','mic_stack_free','speaker_stack_free','reset_reason','presented_frames','te_hz','present_ms','te_wait_ms','scan_margin_ms','scan_deadline_misses','te_timeouts')};samples.append(sample)
        if len(samples)>1 and samples[-1]['frames']==samples[-2]['frames']:
            errors.append('Renderer stopped producing frames');break
        if len(samples)%15==0:
            progress={'running':True,'elapsed_s':round(elapsed,1),'target_s':duration,'last':sample,'errors':errors}
            CHECKPOINT.write_text(json.dumps(progress,indent=2)+'\n')
            print(json.dumps(sample),flush=True)
        time.sleep(1)
    workload_duration=time.monotonic()-start;measurement_end=d.call('system.status')
    stop.set()
    for t in threads:t.join(timeout=2)
    voice.close();d.call('voice.state.set',{'state':'idle','caption':'Ready for you'});d.call('app.home')
    for i,name in enumerate(('neutral','curious','sad','angry') if not errors else ()):
        d.call('avatar.expression',{'name':name});time.sleep(.6);d.screenshot(OUT/f'expression-{name}.png')
    d.call('avatar.expression',{'name':'neutral'})
    if not errors:d.screenshot(OUT/'home-final.png')
    report={'duration_s':round(time.monotonic()-start,1),'workload_duration_s':round(workload_duration,1),'baseline':baseline,'measurement_end':measurement_end,'errors':errors,'audio':audio,'command_latency_p95_ms':round(sorted(latencies)[int((len(latencies)-1)*.95)],2),'command_latency_max_ms':round(max(latencies),2),'samples':samples,'final':d.call('system.status'),'device_logs':list(d.logs)}
    report['unexpected_resets']=[{'previous_uptime_s':a['uptime_s'],'uptime_s':b['uptime_s']} for a,b in zip([baseline]+samples,samples+[measurement_end]) if b['uptime_s']<a['uptime_s']]
    report['requested_duration_s']=duration
    report['completed_requested_duration']=workload_duration>=duration
    (OUT/'soak.json').write_text(json.dumps(report,indent=2)+'\n')
    CHECKPOINT.write_text(json.dumps({'running':False,'elapsed_s':round(workload_duration,1),'target_s':duration,'errors':errors,'unexpected_resets':report['unexpected_resets']},indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='samples'},indent=2))
    assert not report['unexpected_resets'],report['unexpected_resets']
    assert report['completed_requested_duration'],'Run stopped before its requested duration'
    assert not errors,errors
    assert all(samples[-1][k]==0 for k in ('display_errors','audio_rx_errors','audio_tx_errors','audio_overflows'))

    assert measurement_end['te_timeouts']==baseline['te_timeouts'],'Display synchronization timed out'
    assert measurement_end['scan_deadline_misses']==baseline['scan_deadline_misses'],'Display missed its next-scan deadline'
