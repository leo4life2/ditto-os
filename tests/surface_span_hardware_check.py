"""Run the opt-in span corpus on an already-flashed probe. One USB owner only."""
from pathlib import Path
import argparse,json,sys,time
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'host')]
from build_provenance import verify
from device import Device

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--project',type=Path,required=True);p.add_argument('--port',required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.output.exists():p.error('Use a fresh report path')
    seal=verify(a.project.resolve())
    report={'expected_version':seal['version'],'sealed_probe_app_sha256':seal['artifacts']['build/scout_os.bin'],
            'scope':'Actual S3 pixel/depth comparison; same-version production firmware has no diagnostic action. Flash provenance is required separately.'}
    a.output.parent.mkdir(parents=True,exist_ok=True)
    try:
        with Device(a.port) as d:
            time.sleep(3);d.sync_clock();d.call('app.home');time.sleep(.3)
            b=d.call('system.status');boot=d.boot_count
            assert b['version']==seal['version']
            events=d.subscribe('debug.span.result')
            d.call('debug.span.check',timeout=60)
            result=events.get(timeout=3);report['result']=result
            assert 'error' not in result and result['mismatches']==0,result
            assert result['cases']==12531 and result['pixels']==2450898,result
            report['end']=d.call('system.status');report['boot_count_unchanged']=d.boot_count==boot
            assert report['boot_count_unchanged'];report['passed']=True
    except BaseException as e:report['error']=repr(e);raise
    finally:a.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'passed':True,'cases':result['cases'],'compared_pixels':result['pixels']}))
if __name__=='__main__':main()
