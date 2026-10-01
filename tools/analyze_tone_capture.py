"""Analyze the controlled 660 Hz hardware tone recording without network access."""
import argparse,hashlib,json,math,struct,wave
from pathlib import Path
from validation_context import evidence_directory

def analyze(path):
    with wave.open(str(path),'rb') as w:
        assert (w.getnchannels(),w.getsampwidth())==(1,2),'Expected mono PCM16'
        rate=w.getframerate();count=w.getnframes();a=struct.unpack('<'+'h'*count,w.readframes(count))
    size=rate//10;windows=[]
    def power(samples,freq):
        x=sum(v*math.cos(2*math.pi*freq*i/rate) for i,v in enumerate(samples))
        y=sum(v*math.sin(2*math.pi*freq*i/rate) for i,v in enumerate(samples))
        return 2*(x*x+y*y)/(len(samples)**2)
    for start in range(0,len(a)-size+1,size):
        samples=a[start:start+size];mean=sum(samples)/len(samples)
        rms=math.sqrt(sum((v-mean)**2 for v in samples)/len(samples));tone=power(samples,660)
        windows.append({'start_s':start/rate,'rms':round(rms,3),'mean':round(mean,3),'peak':max(abs(v) for v in samples),'tone_660_rms':round(math.sqrt(tone),3),'tone_energy_fraction':round(tone/max(1e-20,rms*rms),4)})
    assert len(windows)>5,'Capture is too short to compare post-tone audio'
    peak=max(windows,key=lambda w:w['tone_660_rms']);quiet=windows[5:]
    baseline=(sum(w['tone_660_rms']**2 for w in quiet)/len(quiet))**.5
    return {'source':path.name,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'sample_rate':rate,'sample_count':len(a),'duration_s':len(a)/rate,'clipped_samples':sum(abs(v)>=32760 for v in a),'max_abs_sample':max(abs(v) for v in a),'peak_tone_window':peak,'post_tone_660_rms':round(baseline,3),'tone_vs_post_tone_db':round(20*math.log10(max(1e-20,peak['tone_660_rms'])/max(1e-20,baseline)),2),'windows':windows,'limits':'One short local 660 Hz tone capture. Supports speaker-to-microphone path activity and absence of clipping in this capture. Does not establish spoken-word quality, room noise performance, echo cancellation, or cloud recognition.'}

if __name__=='__main__':
    root=Path(__file__).resolve().parents[1];out=evidence_directory(root)
    parser=argparse.ArgumentParser();parser.add_argument('recording',type=Path,nargs='?',default=out/'device-tone-capture.wav');a=parser.parse_args()
    report=analyze(a.recording);(out/'tone-analysis.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='windows'},indent=2))
