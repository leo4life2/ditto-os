"""Sleep/wake curves, sensor-driven wake and measured task work on hardware.

Motion commands enter the same detector as real IMU samples. The final
inactivity check uses the actual sensor for a complete 60-second timeout.
"""
import base64,datetime,json,math,os,re,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'host'),str(ROOT/'tools')]
from device import Device
from validation_context import evidence_directory
OUT=evidence_directory(ROOT);report={}
FIELDS=('sleeping','effective_brightness','motion_active','motion_wakes','touch_wakes',
 'idle_for_s','render_parked','audio_sleeping','cpu_mhz','frames','physics_steps',
 'audio_input_blocks','audio_output_blocks','imu_samples','touch_samples','fps',
 'audio_rx_errors','audio_tx_errors','audio_overflows','audio_power_errors','display_errors')

def summarize_cpu(a,b):
    elapsed=b['uptime_us']-a['uptime_us'];old={s['id']:s for s in a['tasks']}
    usage={s['name']:100*((s['runtime_us']-old[s['id']]['runtime_us'])%(2**32))/elapsed
           for s in b['tasks'] if s['id'] in old}
    return {'interval_s':elapsed/1e6,'tasks_percent_of_one_core':usage,
            'average_busy_percent':100-(usage.get('IDLE0',0)+usage.get('IDLE1',0))/2}

with Device() as d:
    time.sleep(2);d.sync_clock();d.call('app.home');d.call('display.wake')
    base=d.call('system.status');report['start']=base
    assert base['version']==os.environ.get('SCOUT_EXPECTED_VERSION','0.8.1')
    assert base['idle_timeout_s']==60
    def motion(acc=(0,0,1),gyro=(0,0,0)):
        d.call('debug.motion',{'acceleration':list(acc),'gyro':list(gyro)})
    def status():return d.call('system.status')
    def run(seconds,acc=(0,0,1),gyro=(0,0,0),sample=.04):
        start=time.monotonic();rows=[];next_sample=start
        while time.monotonic()-start<seconds:
            if acc is not None:motion(acc,gyro)
            if time.monotonic()>=next_sample:
                s=status();rows.append({'t':time.monotonic()-start,**{k:s[k] for k in FIELDS}})
                next_sample+=sample
            time.sleep(.014)
        return rows
    def settled_sleep():
        # A synthetic gyro turn against a fixed accelerometer is intentionally
        # inconsistent; let fusion return to rest before testing the next axis.
        run(2.5);assert not status()['motion_active']
        d.call('display.sleep');rows=run(1.95)
        s=status();assert s['sleeping'] and s['effective_brightness']==10,s
        assert s['render_parked'] and s['audio_sleeping'],s
        return rows
    def wait_awake(acc=(0,0,1),gyro=(0,0,0)):
        start=time.monotonic();wake=None;rows=[]
        while time.monotonic()-start<1.5:
            motion(acc,gyro);s=status();t=time.monotonic()-start
            rows.append({'t':t,**{k:s[k] for k in FIELDS}})
            if not s['sleeping'] and wake is None:wake=t
            if wake is not None and s['effective_brightness']==base['brightness'] and not s['audio_sleeping']:
                return {'wake_s':wake,'fully_awake_s':t,'samples':rows}
            time.sleep(.014)
        raise AssertionError(('Failed to wake',s))
    try:
        run(1)
        a=status();p=d.call('debug.power');time.sleep(5);q=d.call('debug.power');b=status()
        report['awake_cpu']=summarize_cpu(p,q)
        report['awake_fps']=(b['frames']-a['frames'])/(b['uptime_s']-a['uptime_s'])
        assert report['awake_fps']>28,report['awake_fps']
        curve=settled_sleep();report['dim_curve']=curve
        assert all(a['effective_brightness']>=b['effective_brightness'] for a,b in zip(curve,curve[1:])),curve
        assert curve[0]['effective_brightness']>=base['brightness']-1
        assert any(.5<r['t']<.9 and 10<r['effective_brightness']<base['brightness'] for r in curve)
        first10=next(r['t'] for r in curve if r['effective_brightness']==10)
        assert 1.25<first10<1.9,first10
        a=status();p=d.call('debug.power');time.sleep(5);q=d.call('debug.power');b=status()
        report['sleep_cpu']=summarize_cpu(p,q);report['parked_start']=a;report['parked_end']=b
        report['sleep_clock_profiles']=[p,q]
        def clock_times(profile):
            return {name:int(us) for name,us in re.findall(r'^(APB_MIN|APB_MAX|CPU_MAX)\s+\d+\s*M\s+(\d+)',profile,re.M)}
        cp,cq=clock_times(p['clock_profile']),clock_times(q['clock_profile'])
        report['sleep_clock_80mhz_percent']=100*sum(cq[k]-cp[k] for k in ('APB_MIN','APB_MAX'))/(q['uptime_us']-p['uptime_us'])
        assert report['sleep_clock_80mhz_percent']>60,report['sleep_clock_80mhz_percent']
        assert b['physics_steps']==a['physics_steps']
        assert b['audio_input_blocks']==a['audio_input_blocks']
        assert b['frames']-a['frames']<=1 # a minute rollover may refresh the clock
        assert b['imu_samples']>a['imu_samples']+70 and b['touch_samples']>a['touch_samples']+70
        assert report['sleep_cpu']['average_busy_percent']<10,report['sleep_cpu']
        # Querying status is itself CPU work, so instantaneous MHz is not an
        # idle-clock measurement. PM time-in-mode counters are saved instead.
        d.screenshot(OUT/'sleep-device.png')
        # Natural minute AND date rollover while asleep, using a temporary
        # clock offset. clock.sync preserves the sleep state.
        midnight=datetime.datetime.now().astimezone().replace(hour=23,minute=59,second=58,microsecond=0)
        d.call('clock.sync',{'epoch':midnight.timestamp()});time.sleep(.25)
        d.screenshot(OUT/'sleep-before-midnight.png');before=status();time.sleep(2.5);after=status()
        assert after['sleeping'] and after['frames']-before['frames']==1,(before,after)
        assert before['physics_steps']==after['physics_steps']
        d.screenshot(OUT/'sleep-after-midnight.png');d.sync_clock()
        report['midnight_rollover']={'before':before,'after':after}
        # Captures and the quiet profiling interval allowed the diagnostic
        # lease to expire; establish its neutral sample before a new impulse.
        run(.65)
        wakes=[];report['motion_wakes']=wakes
        for axis in range(3):
            if axis:settled_sleep()
            before=status();acc=[0,0,1];acc[axis]+=.13
            r=wait_awake(acc);r['axis']=axis;r['kind']='acceleration';wakes.append(r)
            assert r['wake_s']<.25 and r['fully_awake_s']<.6,r
            assert status()['motion_wakes']==before['motion_wakes']+1
            settled_sleep();before=status();gyro=[0,0,0];gyro[axis]=.3
            r=wait_awake(gyro=gyro);r['axis']=axis;r['kind']='rotation';wakes.append(r)
            assert r['wake_s']<.25 and status()['motion_wakes']==before['motion_wakes']+1,r
        report['motion_wakes']=wakes
        settled_sleep()
        # Slow orientation change also wakes; angular speed is below threshold.
        start=time.monotonic();s=status()
        while s['sleeping'] and time.monotonic()-start<3:
            angle=(time.monotonic()-start)*.05
            motion((math.sin(angle),0,math.cos(angle)),(0,.05,0));time.sleep(.016);s=status()
        report['slow_tilt_wake_s']=time.monotonic()-start;assert not s['sleeping'],s
        run(.6);d.call('display.sleep');run(.65);before=status()
        d.call('debug.touch',{'down':True,'x':50,'y':60});start=time.monotonic()
        rows=run(.35);d.call('debug.touch',{'down':False,'x':50,'y':60});after=status()
        report['interrupt_dim']={'before':before,'samples':rows,'after':after}
        assert 10<before['effective_brightness']<base['brightness']
        assert after['touch_wakes']==before['touch_wakes']+1 and after['effective_brightness']==base['brightness']
        # Repeated codec close/open followed by real PCM verifies resume.
        for i in range(3):
            settled_sleep();d.call('display.wake');run(.45)
            d.call('audio.play',{'pcm':base64.b64encode(bytes(640)).decode()});run(.15)
            assert not status()['audio_sleeping']
        assert status()['audio_output_blocks']>=base['audio_output_blocks']+3
        # Timer deadlines remain live in standby and wake the device.
        d.call('timer.start',{'seconds':3});run(.3);d.call('display.sleep');run(3.1)
        assert not status()['sleeping'];report['timer_woke']=True
        d.call('app.home');run(.6)
        # Source lease expires back to real IMU; no synthetic input below.
        d.call('debug.motion',{'enabled':False});time.sleep(.7)
        d.call('display.wake');report['real_idle_start']=status()
        print('Checking a full 60-second quiet interval using the real IMU...',flush=True)
        start=time.monotonic();real=[]
        while time.monotonic()-start<66:
            s=status();real.append(s)
            assert s['imu_available'] and not s['motion_simulated'] and s['imu_sample_age_ms']<150,s
            if s['sleeping'] and s['effective_brightness']==10 and s['power_saving']:break
            time.sleep(.2)
        report['real_idle_samples']=real
        assert s['sleeping'] and s['effective_brightness']==10,{'last':s,'initial':real[0]}
        assert 59.8<time.monotonic()-start<65
        report['automatic_sleep_s']=time.monotonic()-start
        first=status();time.sleep(3);last=status()
        assert last['sleeping'] and last['motion_wakes']==first['motion_wakes']
        assert last['physics_steps']==first['physics_steps']
        assert last['audio_input_blocks']==first['audio_input_blocks']
        report['real_quiet_sleep']={'start':first,'end':last}
        # Tap wake remains immediate; restoring audio must not starve sensors.
        d.call('debug.touch',{'down':True,'x':50,'y':60});time.sleep(.07)
        d.call('debug.touch',{'down':False,'x':50,'y':60});time.sleep(.35)
        final=status();assert not final['sleeping'] and final['effective_brightness']==base['brightness']
        for key in ('display_errors','audio_rx_errors','audio_tx_errors','audio_overflows','audio_power_errors','imu_recoveries'):
            assert final[key]==base[key],(key,base[key],final[key])
        report['passed']=True
    finally:
        d.call('debug.motion',{'enabled':False});d.call('debug.touch',{'down':False,'x':50,'y':60})
        d.call('timer.cancel');d.call('app.home');d.call('display.wake');time.sleep(.6)
        d.sync_clock()
        report['final']=status();report['logs']=list(d.logs)
        (OUT/'sleep-power-check.json').write_text(json.dumps(report,indent=2)+'\n')
        d.screenshot(OUT/'home-device.png')
print(json.dumps({k:v for k,v in report.items() if k in ('passed','awake_cpu','sleep_cpu','awake_fps','automatic_sleep_s','slow_tilt_wake_s')},indent=2))
