"""Combined touch physics, audio and Wi-Fi hardware soak; synthetic gestures are labeled."""
import base64,json,sys,threading,time,statistics,signal,math,os
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from validation_context import evidence_directory
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'host'))
from device import Device
from voice_transport import VoiceTransport
ROOT=Path(__file__).resolve().parents[1];OUT=evidence_directory(ROOT);OUT.mkdir(exist_ok=True)
pattern=os.environ.get('SCOUT_TOUCH_PATTERN','drag')
CHECKPOINT=OUT/'soak-progress.json'
duration=int(sys.argv[1]) if len(sys.argv)>1 else 300
with Device() as d:
    time.sleep(2);d.sync_clock();d.call('app.home');voice=VoiceTransport(d);voice.start();voice.set_state('listening','Listening to you');time.sleep(1.1);baseline=d.call('system.status')
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
    def touch_worker():
        try:
            step=0;deadline=time.monotonic()
            while not stop.is_set():
                part=step%240
                down=part<190
                if part<60:x,y=110+part*3,280
                else:
                    a=(part-60)*.12
                    x,y=int(205+70*math.cos(a)),int(290-70*math.sin(a))
                if pattern=='taps':
                    cycle=step%18;down=cycle<3
                    x,y=((105,282),(205,192),(220,327),(300,307))[(step//18)%4]
                d.call('debug.touch',{'down':down,'x':x,'y':y})
                step+=1;deadline+=1/30;stop.wait(max(0,deadline-time.monotonic()))
        except Exception as e:errors.append(str(e))
    def motion_worker():
        try:
            start_motion=time.monotonic()
            while not stop.is_set():
                t=time.monotonic()-start_motion
                # Roll the physical frame about Z, with matched gyro and
                # acceleration, plus translation on all three screen axes.
                angle=.65*math.sin(t*1.7);speed=1.105*math.cos(t*1.7)
                acc=[math.sin(angle)+.65*math.sin(t*5),math.cos(angle)+.4*math.sin(t*4),.6*math.sin(t*3)]
                d.call('debug.motion',{'acceleration':acc,'gyro':[.6*math.sin(t*2),.7*math.cos(t*3),speed]})
                stop.wait(.025)
        except Exception as e:errors.append(str(e))
    workers=[input_worker,output_worker,touch_worker]
    if os.environ.get('SCOUT_MOTION_LOAD')=='1':workers.append(motion_worker)
    threads=[threading.Thread(target=f,daemon=True) for f in workers]
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
        sample={k:s[k] for k in ('raster0_ms','raster1_ms','anim_ms','skin_ms','bin_ms','draw_ms','diff_ms','copy_ms','dma_wait_ms','submit_ms','uptime_s','fps','frame_ms','frame_p95_ms','frames','frames_within25','missed_frames','internal_free','psram_free','display_errors','audio_rx_errors','audio_tx_errors','audio_overflows','compose_ms','text_ms','upload_ms','ui_stack_free','render_stack_free','control_stack_free','mic_stack_free','speaker_stack_free','reset_reason','physics_ms','physics_activity','annoyance','slime_stretch','slime_press','face_x_eyes','face_side_eye','touch_errors','touch_samples','touch_contacts','touch_available','touch_recoveries','tap_depth','body_squash','presented_frames','te_hz','present_ms','te_wait_ms','scan_margin_ms','scan_deadline_misses','te_timeouts')};sample.update({k:s[k] for k in ('imu_samples','imu_errors','imu_retries','motion_age_ms','motion_offset','motion_bend','motion_spin','motion_depth') if k in s});samples.append(sample)
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
    if os.environ.get('SCOUT_MOTION_LOAD')=='1':d.call('debug.motion',{'enabled':False})
    d.call('debug.touch',{'down':False,'x':205,'y':290});voice.close();d.call('voice.state.set',{'state':'idle','caption':'Ready for you'});d.call('app.home')
    for i,name in enumerate(('neutral','curious','sad','angry') if not errors else ()):
        d.call('avatar.expression',{'name':name});time.sleep(.6);d.screenshot(OUT/f'expression-{name}.png')
    d.call('avatar.expression',{'name':'neutral'})
    if not errors:d.screenshot(OUT/'home-final.png')
    report={'duration_s':round(time.monotonic()-start,1),'workload_duration_s':round(workload_duration,1),'baseline':baseline,'measurement_end':measurement_end,'errors':errors,'audio':audio,'command_latency_p95_ms':round(sorted(latencies)[int((len(latencies)-1)*.95)],2),'command_latency_max_ms':round(max(latencies),2),'samples':samples,'touch_input':'injected '+pattern+' through shared input path, not physical fingers','final':d.call('system.status'),'device_logs':list(d.logs)}
    report['unexpected_resets']=[{'previous_uptime_s':a['uptime_s'],'uptime_s':b['uptime_s']} for a,b in zip([baseline]+samples,samples+[measurement_end]) if b['uptime_s']<a['uptime_s']]
    report['requested_duration_s']=duration
    seconds=measurement_end['uptime_s']-baseline['uptime_s']
    report['render_fps']=(measurement_end['frames']-baseline['frames'])/seconds
    report['presented_fps']=(measurement_end['presented_frames']-baseline['presented_frames'])/seconds
    windows=[];anchor=baseline
    for q in samples+[measurement_end]:
        elapsed=q['uptime_s']-anchor['uptime_s']
        if elapsed>=5:
            windows.append((q['presented_frames']-anchor['presented_frames'])/elapsed);anchor=q
    report['five_second_window_fps']=windows
    report['minimum_five_second_fps']=min(windows) if windows else None
    report['completed_requested_duration']=workload_duration>=duration
    (OUT/'render-load.json').write_text(json.dumps(report,indent=2)+'\n')
    CHECKPOINT.write_text(json.dumps({'running':False,'elapsed_s':round(workload_duration,1),'target_s':duration,'errors':errors,'unexpected_resets':report['unexpected_resets']},indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='samples'},indent=2))
    assert report['final']['touch_available'], 'Touch controller unavailable at end'
    assert not report['unexpected_resets'],report['unexpected_resets']
    assert report['completed_requested_duration'],'Run stopped before its requested duration'
    assert not errors,errors
    assert all(measurement_end[k]==baseline[k] for k in ('display_errors','audio_rx_errors','audio_tx_errors','audio_overflows','te_timeouts','scan_deadline_misses'))
    if os.environ.get('SCOUT_REQUIRE_30_FPS')=='1':assert report['presented_fps']>=30,report['presented_fps']
