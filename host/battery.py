"""Durable, local battery-cycle evidence. No transcripts, audio, or watt guesses."""
import json,os,re,threading,time
from pathlib import Path
DEFAULT=Path.home()/'Library/Application Support/Ditto Remote/Battery'
FIELDS=('span_ms','awake_ms','sleep_ms','battery_ms','unknown_power_ms','usb_ms','ble_ms','ble_available_ms','wifi_connected_ms','mic_awake_ms','audio_ms','dictating_ms','brightness_pct_ms','fast_cpu_ms','cpu_elapsed_ms','idle0_us','idle1_us','render_us','services_us','audio_us','radio_us','frames','physics_steps','contacts','display_errors','audio_errors')
def atomic(path,text):
    temporary=path.with_suffix(path.suffix+'.new');temporary.write_text(text);temporary.replace(path)
def read_records(path):
    records={}
    if path.exists():
        for line in path.read_text().splitlines():
            try:r=json.loads(line);records[int(r['sequence'])]=r
            except (ValueError,KeyError,TypeError):continue
    return [records[k] for k in sorted(records)]
def report(records,info):
    lines=['# Ditto battery report','', 'Recorded on the device; automatically archived when USB reconnects.', '',
           '**These are battery-gauge and workload measurements, not an electrical power meter.** No measured mA, watts, or mWh are available.', '']
    if not records:return '\n'.join(lines+['No records yet. Leave Ditto running, then reconnect USB after wireless use.',''])
    total={k:sum(max(0,r.get(k,0)) for r in records) for k in FIELDS}
    span=total['span_ms'];hours=lambda x:f'{x/3600000:.2f} h'
    percent=lambda a,b:f'{100*a/b:.1f}%' if b else '—'
    cpu=total['cpu_elapsed_ms']*2000
    lines+=['| Measurement | Recorded |','| --- | --- |',f"| Observed runtime | {hours(span)} |",f"| Running on battery | {hours(total['battery_ms'])} |",f"| Awake / sleeping | {hours(total['awake_ms'])} / {hours(total['sleep_ms'])} |",f"| CPU busy, both cores | {percent(max(0,cpu-total['idle0_us']-total['idle1_us']),cpu)} |",f"| Rendering CPU / radio CPU | {percent(total['render_us'],cpu)} / {percent(total['radio_us'],cpu)} |",f"| Audio CPU / services CPU | {percent(total['audio_us'],cpu)} / {percent(total['services_us'],cpu)} |",f"| Average brightness | {total['brightness_pct_ms']/span if span else 0:.1f}% |",f"| Bluetooth linked / radio available | {hours(total['ble_ms'])} / {hours(total['ble_available_ms'])} |",f"| Microphone awake / audio streaming | {hours(total['mic_awake_ms'])} / {hours(total['audio_ms'])} |",f"| Dictating | {hours(total['dictating_ms'])} |",f"| Display / audio errors | {total['display_errors']} / {total['audio_errors']} |",f"| Records / last sequence | {len(records)} / {records[-1]['sequence']} |",'', '']
    lines+=['## Workload by state','', '| State (unmixed samples) | Observed time | CPU busy | Brightness |','| --- | --- | --- | --- |']
    for name,field in [('Awake','awake_ms'),('Sleeping','sleep_ms')]:
        selected=[r for r in records if r.get('span_ms',0)>0 and r.get(field)==r.get('span_ms')]
        ms=sum(r.get('span_ms',0) for r in selected);capacity=sum(r.get('cpu_elapsed_ms',0) for r in selected)*2000
        idle=sum(r.get('idle0_us',0)+r.get('idle1_us',0) for r in selected)
        bright=sum(r.get('brightness_pct_ms',0) for r in selected)
        lines.append(f"| {name} | {hours(ms)} | {percent(max(0,capacity-idle),capacity)} | {bright/ms if ms else 0:.1f}% |")
    lines+=['','Transition samples are excluded from this state comparison; CPU sampling boundaries can differ by up to five seconds.','','## Battery sessions','']
    sessions=[];active=None;prev=None
    for r in records:
        valid=bool(r['flags']&4 and r['flags']&2 and 0<=r['percent']<=100)
        on_battery=valid and not r['flags']&1
        if on_battery and active is None:active={'start':r,'end':r,'battery_ms':0,'sleep_ms':0,'awake_ms':0,'complete':False,'gaps':0}
        if active:
            active['battery_ms']+=r.get('battery_ms',0)
            if r.get('battery_ms',0):active['sleep_ms']+=r.get('sleep_ms',0);active['awake_ms']+=r.get('awake_ms',0)
            if prev and (r['boot']!=prev['boot'] or r['sequence']!=prev['sequence']+1):active['gaps']+=1
            if valid:active['end']=r
            if valid and r['flags']&1:active['complete']=True;sessions.append(active);active=None
        prev=r
    if active:sessions.append(active)
    if not sessions:lines+=['No unplugged session has been recorded yet. Charging data does not measure battery life.','']
    for i,s in enumerate(sessions[-10:],max(1,len(sessions)-9)):
        start,end=s['start'],s['end'];h=s['battery_ms']/3600000;drop=start['percent']-end['percent']
        lines += [f"- Session {i}: **{hours(s['battery_ms'])} observed**, {start['percent']}% → {end['percent']}%, {'reconnected' if s['complete'] else 'open / last seen on battery'}. Sleep {hours(s['sleep_ms'])}; awake {hours(s['awake_ms'])}." + (f' Gauge decline: {drop/h:.1f} percentage points/hour.' if h>=.25 and drop>=2 else ' More discharge is needed for a useful drain rate.') + (f" {s['gaps']} reboot/log gaps; this is a lower bound on elapsed use." if s['gaps'] else '')]
    gaps=sum(max(0,b['sequence']-a['sequence']-1) for a,b in zip(records,records[1:]))
    lines+=['','## Measurement limits','',f"- {gaps} missing sequence numbers in the archive; device reports {info.get('dropped',0)} deferred-record drops and {info.get('write_errors',0)} journal write errors in its current boot.",'- One-minute samples, plus power-source transitions. A sudden cutoff can lose the last minute; uninterrupted dictation defers flash writes (up to 16 queued records).','- The onboard journal retains about 68 hours; reconnect USB before that fills to retain a complete archive.','- Percentage is the PMIC fuel-gauge estimate, not a linear capacity measurement. A partial run does not prove full-cycle battery life.','- CPU figures are two-core busy-time estimates sampled every five seconds. Radio availability, audio time, and brightness are workload clues, not direct energy measurements.','- A reconnect without charge completion is not a full charge cycle. Reboot downtime and unknown power-source samples are excluded from observed battery runtime.','',f"Latest firmware: {records[-1].get('firmware','unknown')}. Device: {info.get('device_id','unknown')}.", '']
    return '\n'.join(lines)
class Archive:
    def __init__(self,device,stop,root=DEFAULT):self.device=device;self.stop=stop;self.root=Path(root);self.error='';self.lock=threading.Lock();self.cached='No battery records yet.';self.last_export=0
    def sync(self):
        s=self.device.call('system.status')
        if s.get('usb_recording') or s.get('bluetooth_audio_active') or s.get('remote_dictating'):return
        info=self.device.call('power.log.info')
        if not info.get('available'):return
        name=info.get('device_id','')
        if not re.fullmatch('[0-9a-f]{12}',name):raise ValueError('Invalid battery device identifier')
        self.root.mkdir(parents=True,exist_ok=True);path=self.root/(name+'.jsonl');records=read_records(path);cursor=records[-1]['sequence'] if records else 0
        # Repair only our own incomplete final append after an interrupted export.
        if path.exists():
            raw=path.read_bytes()
            if raw and not raw.endswith(b'\n'):
                tail=raw.rfind(b'\n')+1
                try:json.loads(raw[tail:]);raw+=b'\n'
                except (ValueError,UnicodeError):raw=raw[:tail]
                with path.open('wb') as repaired:repaired.write(raw);repaired.flush();os.fsync(repaired.fileno())
        with path.open('a') as out:
            for _ in range(32): # bounded catch-up; resume cursor next pass
                if self.stop.is_set() or cursor>=info['latest_sequence']:break
                state=self.device.call('system.status')
                if state.get('usb_recording') or state.get('bluetooth_audio_active') or state.get('remote_dictating'):break
                page=self.device.call('power.log.read',{'after':cursor})
                fresh=[r for r in page.get('records',[]) if r['sequence']>cursor]
                if not fresh:break
                for r in fresh:out.write(json.dumps(r,separators=(',',':'))+'\n');records.append(r);cursor=r['sequence']
                out.flush();os.fsync(out.fileno());self.stop.wait(.15)
        text=report(records,info);atomic(self.root/(name+'-info.json'),json.dumps(info,indent=2));atomic(self.root/'Battery Report.md',text)
        with self.lock:self.cached=text;self.last_export=time.time();self.error=''
    def run(self):
        existing=self.root/'Battery Report.md'
        if existing.exists():self.cached=existing.read_text()
        while not self.stop.wait(15):
            try:
                if self.device.connected:self.sync()
            except Exception as e:self.error=str(e)
    def snapshot(self):
        with self.lock:return {'report':self.cached,'archive':str(self.root),'last_export':self.last_export,'error':self.error}
