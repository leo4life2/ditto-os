"""Device soak: injected gestures, real silent PCM transport, Wi-Fi scans.

Not physical touch, secured Wi-Fi, cloud voice or current qualification.
Use a fresh SCOUT_EVIDENCE_NAME; runs never overwrite existing evidence.
"""
import datetime, hashlib, json, math, os, signal, statistics, sys, threading, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'host'),str(ROOT/'tools')]
from validation_context import evidence_directory
from build_provenance import verify
from device import Device
from voice_transport import VoiceTransport
from audio_cadence import AudioCadence
OUT=evidence_directory(ROOT);CHECKPOINT=OUT/'soak-progress.json'
if CHECKPOINT.exists() or (OUT/'soak.json').exists():
    raise SystemExit('Choose a fresh SCOUT_EVIDENCE_NAME; evidence already exists')
duration=int(sys.argv[1]) if len(sys.argv)>1 else 300
if duration<30:raise SystemExit('Use at least 30 seconds; extended run is 7200')
pattern=os.environ.get('SCOUT_TOUCH_PATTERN','drag')
if pattern not in ('drag','taps'):raise SystemExit('Unknown touch pattern')
motion=os.environ.get('SCOUT_MOTION_LOAD')=='1';seal=verify()
errors=[];samples=[];touch_samples=[];power_samples=[];latencies=[]
cadence=AudioCadence()
audio={'input_packets':0,'output_packets':0}
stop=threading.Event()
for sig in (signal.SIGINT,signal.SIGTERM):signal.signal(sig,lambda *_:stop.set())
report={'passed':False,'requested_duration_s':duration,'version':seal['version'],
 'started_at':datetime.datetime.now().astimezone().isoformat(),'pid':os.getpid(),
 'runner_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
 'audio_cadence_sha256':hashlib.sha256((ROOT/'tools/audio_cadence.py').read_bytes()).hexdigest(),
 'acceptance_scope':'Synthetic mixed-load reliability only; FPS metrics do not imply the performance target passed',
 'expected_local_app_sha256':seal['artifacts']['build/scout_os.bin'],
 'identity_scope':'Local sealed artifact and reported version; no runtime image-hash endpoint',
 'physical_touch':False,'secured_wifi':False,'cloud_voice':False,'current_measurement':False,
 'touch_input':'injected '+pattern,'injected_motion':motion,
 'audio_scope':'16 kHz silent playback; microphone packets counted and discarded, not saved',
 'errors':errors,'audio':audio,'samples':samples,'touch_samples':touch_samples,'power_samples':power_samples}
def save(path,value):
    tmp=path.with_suffix('.tmp');tmp.write_text(json.dumps(value,indent=2)+'\n');tmp.replace(path)
def journal(name,value):
    with (OUT/(name+'.jsonl')).open('a') as f:f.write(json.dumps(value,separators=(',',':'))+'\n')
def fail(message):errors.append(message);stop.set()
def touch_delta(a,b):
    keys=('errors','bus_errors','decode_errors','idle_bus_deferrals','recoveries','overflow')
    delta={k:b[k]-a[k] for k in keys}
    # Errors count failed samples; retries can cause two raw bus errors per sample.
    # Both sample errors and quiet deferrals increment in the same locked section.
    delta['non_deferred_failed_samples']=delta['errors']-delta['idle_bus_deferrals']
    return delta
FAULTS=('display_errors','audio_rx_errors','audio_tx_errors','audio_overflows','audio_power_errors',
        'scan_deadline_misses','te_timeouts','imu_recoveries','imu_stalls')
with Device() as d:
    voice=None;threads=[];start=None;baseline=None;boot=d.boot_count
    try:
        time.sleep(2);d.sync_clock();d.call('app.home');initial=d.call('system.status')
        assert initial['version']==seal['version'],'Device version differs from sealed source'
        assert not initial['muted'],'Mic is muted; unmute before qualification (preference preserved)'
        # Warm lazy allocations before measuring comparable quiet heaps.
        voice=VoiceTransport(d);voice.start()
        for _ in range(50):voice.write_audio(bytes(640));voice.read_audio(.001);time.sleep(.020)
        d.call('wifi.scan');d.wait_event('wifi.scan.result',15);voice.close();voice=None
        d.call('app.home');time.sleep(3);report['quiet_baseline']=d.call('system.status')
        voice=VoiceTransport(d);voice.start();time.sleep(.5)
        baseline=d.call('system.status');report['baseline']=baseline
        assert baseline['frames']>10,'Renderer did not start'
        touch_base=d.call('debug.touch.status');report['touch_baseline']=touch_base;boot=d.boot_count
        def mic_worker():
            try:
                while not stop.is_set():
                    if voice.read_audio():audio['input_packets']+=1
            except Exception as e:fail('microphone: '+repr(e))
        def output_worker():
            begin=deadline=time.monotonic()
            try:
                while not stop.is_set():
                    started=time.monotonic()
                    assert voice.write_audio(bytes(640)),'Playback cancelled'
                    now=time.monotonic();audio['output_packets']+=1
                    cadence.observe(deadline,started,now)
                    deadline+=.020;stop.wait(max(0,deadline-time.monotonic()))
            except Exception as e:fail('speaker: '+repr(e))
            finally:
                audio.update(cadence.summary())
                audio['elapsed_s']=time.monotonic()-begin
                audio['host_packets_per_second']=audio['output_packets']/audio['elapsed_s']
        def touch_worker():
            step=0;deadline=time.monotonic()
            try:
                while not stop.is_set():
                    part=step%240;down=part<190
                    if part<60:x,y=110+part*3,280
                    else:
                        a=(part-60)*.12;x,y=int(205+70*math.cos(a)),int(290-70*math.sin(a))
                    if pattern=='taps':
                        down=step%18<3;x,y=((105,282),(205,192),(220,327),(300,307))[(step//18)%4]
                    d.call('debug.touch',{'down':down,'x':x,'y':y})
                    step+=1;deadline+=1/30;stop.wait(max(0,deadline-time.monotonic()))
            except Exception as e:fail('touch injection: '+repr(e))
        def motion_worker():
            begin=time.monotonic()
            try:
                while not stop.is_set():
                    t=time.monotonic()-begin;angle=.65*math.sin(t*1.7)
                    acc=[math.sin(angle)+.65*math.sin(t*5),math.cos(angle)+.4*math.sin(t*4),.6*math.sin(t*3)]
                    d.call('debug.motion',{'acceleration':acc,'gyro':[.6*math.sin(t*2),.7*math.cos(t*3),1.105*math.cos(t*1.7)]})
                    stop.wait(.025)
            except Exception as e:fail('motion injection: '+repr(e))
        save(OUT/'soak.json',report)
        start=time.monotonic()
        threads=[threading.Thread(target=f,daemon=True) for f in [mic_worker,output_worker,touch_worker]+([motion_worker] if motion else [])]
        for t in threads:t.start()
        next_scan,next_emote,next_touch,next_power,next_save=10,5,0,0,0
        names=['Wave','Yes','Happy','No','Celebrate','Idle'];ix=0
        while not stop.is_set() and time.monotonic()-start<duration:
            elapsed=time.monotonic()-start
            if elapsed>=next_scan:
                d.call('wifi.scan');d.wait_event('wifi.scan.result',15);d.call('app.home');next_scan+=45
            if elapsed>=next_emote:
                d.call('avatar.emote',{'name':names[ix%len(names)]});ix+=1;next_emote+=12
            then=time.monotonic();s=d.call('system.status');latencies.append((time.monotonic()-then)*1000)
            sample={k:v for k,v in s.items() if k not in ('ssid','caption','wifi_error')}
            sample['elapsed_s']=time.monotonic()-start;samples.append(sample);journal('samples',sample)
            previous=samples[-2] if len(samples)>1 else baseline
            assert s['version']==seal['version'] and d.boot_count==boot,'Unexpected reboot/version change'
            assert s['uptime_s']>=previous['uptime_s'],'Uptime restarted'
            assert len(samples)<2 or s['presented_frames']>previous['presented_frames'],'Presentation stalled'
            assert not s['sleeping'] and s['touch_available'],'Unexpected sleep or unavailable controller'
            for key in FAULTS:assert s[key]==baseline[key],f'{key}: {baseline[key]} -> {s[key]}'
            if elapsed>=next_touch:
                q=d.call('debug.touch.status');touch_samples.append(q);journal('touch-samples',q);delta=touch_delta(touch_base,q)
                for key in ('non_deferred_failed_samples','decode_errors','recoveries','overflow'):
                    assert delta[key]==0,f'touch {key}: {delta}'
                next_touch+=10
            if elapsed>=next_power:
                power_samples.append(d.call('debug.power'));journal('power-samples',power_samples[-1]);next_power+=60
            if elapsed>=next_save:
                dt=s['uptime_s']-baseline['uptime_s']
                progress={'running':True,'pid':os.getpid(),'elapsed_s':round(sample['elapsed_s'],1),
                  'target_s':duration,'version':s['version'],'presented_fps':(s['presented_frames']-baseline['presented_frames'])/dt if dt>=1 else None,
                  'internal_free':s['internal_free'],'touch_delta':delta,'errors':errors}
                save(CHECKPOINT,progress);print(json.dumps(progress),flush=True);next_save+=15
            stop.wait(1)
        report['workload_duration_s']=time.monotonic()-start
        report['measurement_end']=end=d.call('system.status');report['touch_end']=d.call('debug.touch.status')
        assert report['workload_duration_s']>=duration,'Run stopped before requested duration'
        dt=end['uptime_s']-baseline['uptime_s']
        report['presented_fps']=(end['presented_frames']-baseline['presented_frames'])/dt
        report['speaker_blocks_per_second']=(end['audio_output_blocks']-baseline['audio_output_blocks'])/dt
        report['new_errors']={k:end[k]-baseline[k] for k in FAULTS}
        report['touch_delta']=touch_delta(touch_base,report['touch_end'])
        assert not any(report['new_errors'].values()),report['new_errors']
        for key in ('non_deferred_failed_samples','decode_errors','recoveries','overflow'):
            assert report['touch_delta'][key]==0,report['touch_delta']
        assert end['uptime_s']>=baseline['uptime_s'] and d.boot_count==boot,'Unexpected reboot'
        assert report['speaker_blocks_per_second']>=49,'Device playback underfed'
    except Exception as e:fail(repr(e))
    finally:
        if start is not None:report.setdefault('workload_duration_s',time.monotonic()-start)
        stop.set()
        for t in threads:t.join(timeout=3)
        if any(t.is_alive() for t in threads):errors.append('Worker still running after cleanup deadline')
        cleanup=[('debug.touch',{'down':False,'x':205,'y':290})]
        if motion:cleanup.append(('debug.motion',{'enabled':False}))
        for tool,args in cleanup:
            try:d.call(tool,args)
            except Exception as e:errors.append('cleanup '+tool+': '+repr(e))
        try:
            if voice:voice.close()
            d.call('app.home');time.sleep(3);d.sync_clock();report['final']=d.call('system.status')
            if not errors:d.screenshot(OUT/'home-final.png')
        except Exception as e:errors.append('cleanup: '+repr(e))
        report['boot_count_start']=boot;report['boot_count_end']=d.boot_count;report['device_logs']=list(d.logs)
if start is not None:report['elapsed_including_cleanup_s']=time.monotonic()-start
if samples:
    report['internal_memory']={'minimum_sampled_bytes':min(s['internal_free'] for s in samples),
      'first_60_sample_median_bytes':statistics.median(s['internal_free'] for s in samples[:60]),
      'last_60_sample_median_bytes':statistics.median(s['internal_free'] for s in samples[-60:])}
    report['command_latency_p95_ms']=sorted(latencies)[int((len(latencies)-1)*.95)]
    report['command_latency_max_ms']=max(latencies)
if 'quiet_baseline' in report and 'final' in report:
    report['quiet_heap_change_bytes']={k:report['final'][k]-report['quiet_baseline'][k] for k in ('internal_free','psram_free')}
    if any(v < -1024 for v in report['quiet_heap_change_bytes'].values()):
        errors.append('Retained heap loss exceeds 1024 bytes; inspect comparable quiet baselines')
if not audio.get('delivery_timing_met',False):errors.append('PCM delivery timing budget exceeded; inspect audio outliers')
if audio.get('host_packets_per_second',0)<49:errors.append('Host playback source underfed')
audio['input_packets_per_second']=audio['input_packets']/max(.001,audio.get('elapsed_s',0))
if audio['input_packets_per_second']<40:errors.append('Microphone delivery fell below 40 packets/s')
report['completed_requested_duration']=report.get('workload_duration_s',0)>=duration
report['passed']=report['completed_requested_duration'] and not errors
save(OUT/'soak.json',report)
save(CHECKPOINT,{'running':False,'pid':os.getpid(),'passed':report['passed'],
  'elapsed_s':report.get('workload_duration_s',0),'target_s':duration,'errors':errors})
print(json.dumps({k:report[k] for k in ('passed','completed_requested_duration','errors','audio')},indent=2),flush=True)
if not report['passed']:raise SystemExit(1)
