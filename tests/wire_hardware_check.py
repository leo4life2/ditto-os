"""Audit real USB record framing during concurrent PCM, controls and captures.

Retains counters/captures only, never audio samples or raw wire data.
Not a frame-rate, acoustic, or physical-input qualification.
"""
from pathlib import Path
import argparse,base64,json,sys,threading,time
import serial

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'host'),str(ROOT/'tools')]
from device import Device
from voice_transport import VoiceTransport
from build_provenance import verify
from audio_cadence import AudioCadence

parser=argparse.ArgumentParser();parser.add_argument('output',type=Path);args=parser.parse_args()
assert not args.output.exists();args.output.mkdir(parents=True)
seal=verify();report={'version':seal['version'],'app_sha256':seal['artifacts']['build/scout_os.bin'],
                     'passed':False,'raw_audio_saved':False,'errors':[]}
original=serial.Serial
class AuditedSerial(original):
    def __init__(self,*a,**kw):
        self.partial=bytearray();self.records=0;self.invalid=0;self.audio=0;self.bad_pcm=0
        super().__init__(*a,**kw)
    def read(self,*a,**kw):
        data=super().read(*a,**kw);self.partial.extend(data)
        while b'\n' in self.partial:
            raw,_,self.partial=self.partial.partition(b'\n');marker=raw.find(b'SCOUT ')
            if marker<0:continue
            try:
                event=json.loads(raw[marker+6:]);self.records+=1
                if event.get('event')=='audio.input':
                    self.audio+=1
                    try:
                        if len(base64.b64decode(event['pcm'],validate=True))!=640:self.bad_pcm+=1
                    except Exception:self.bad_pcm+=1
            except Exception:self.invalid+=1
        if len(self.partial)>65536:self.invalid+=1;self.partial.clear()
        return data
serial.Serial=AuditedSerial
with Device() as device:
    voice=None;stop=threading.Event();threads=[]
    def save():
        report['wire']={k:getattr(device.serial,k) for k in ['records','invalid','audio','bad_pcm']}
        (args.output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    try:
        time.sleep(3);device.sync_clock();device.call('app.home')
        base=device.call('system.status');assert base['version']==seal['version'];boot=device.boot_count
        voice=VoiceTransport(device);voice.start();report['controls']=0;report['speaker_packets']=0
        report['captures']=[];cadence=AudioCadence()
        def speaker():
            try:
                deadline=time.monotonic()
                while not stop.is_set():
                    started=time.monotonic()
                    assert voice.write_audio(bytes(640));report['speaker_packets']+=1
                    cadence.observe(deadline,started,time.monotonic())
                    deadline+=.020;stop.wait(max(0,deadline-time.monotonic()))
            except Exception as e:report['errors'].append(str(e))
        def controls():
            try:
                for _ in range(100):
                    s=device.call('system.status');assert s['version']==seal['version']
                    report['controls']+=1;time.sleep(.04)
            except Exception as e:report['errors'].append(str(e))
        threads=[threading.Thread(target=fn) for fn in (speaker,controls)]
        for thread in threads:thread.start()
        for i in range(3):
            begin=time.monotonic();controls_before=report['controls'];audio_before=report['speaker_packets']
            device.screenshot(args.output/f'frame-{i}.png')
            report['captures'].append({'seconds':time.monotonic()-begin,'controls_completed_during_capture':report['controls']-controls_before,'speaker_packets_during_capture':report['speaker_packets']-audio_before})
            time.sleep(.8)
        threads[1].join(12);assert not threads[1].is_alive()
        stop.set();threads[0].join(9);assert not threads[0].is_alive()
        voice.close();voice=None
        end=device.call('system.status');report['final']=end;report['audio_timing']=cadence.summary()
        report['new_errors']={k:end[k]-base[k] for k in ['display_errors','audio_rx_errors','audio_tx_errors','audio_overflows','audio_power_errors','te_timeouts','scan_deadline_misses']}
        assert not report['errors'] and not any(report['new_errors'].values()),report
        assert device.boot_count==boot and end['uptime_s']>=base['uptime_s']
        assert device.serial.audio>=100 and device.serial.bad_pcm==0 and device.serial.invalid==0
        assert report['controls']==100 and report['speaker_packets']>=100
        assert all(c['controls_completed_during_capture']>=3 and c['speaker_packets_during_capture']>=5 for c in report['captures'])
        report['passed']=True
    finally:
        stop.set()
        for thread in threads:thread.join(9)
        if voice:voice.close()
        device.call('app.home');save()
serial.Serial=original
print(json.dumps({k:v for k,v in report.items() if k!='final'},indent=2))
