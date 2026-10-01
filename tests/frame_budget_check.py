"""Known distributions, counter resets and boundary percentages for profiling."""
import json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from frame_budget import summarize
from validation_context import evidence_directory

start={'version':'test','uptime_s':10.,'frames':1000,'presented_frames':998,
       'frames_within25':900,'missed_frames':11}
def finish(fast=95,late=1):
    return {**start,'uptime_s':14.,'frames':1100,'presented_frames':1098,
            'frames_within25':900+fast,'missed_frames':11+late}
r=summarize(start,finish())
assert r['presented_fps']==25 and r['percent_at_most_25ms']==95
assert r['percent_within_33_333ms']==99 and r['headroom_thresholds_met']
assert not summarize(start,finish(fast=94))['p95_at_most_25ms']
assert not summarize(start,finish(late=2))['at_least_99_percent_within_33_333ms']
for change in ({'version':'other'},{'uptime_s':9.},{'uptime_s':float('nan')},
               {'frames':1000},{'frames_within25':899},{'missed_frames':-1},
               {'presented_frames':1},{'frames_within25':1000,'missed_frames':12}):
    try:summarize(start,{**finish(),**change})
    except ValueError:pass
    else:raise AssertionError(change)
# These three windows have a misleadingly good mean sampled rolling p95.
# Cumulative counters still identify the failing whole-window distribution.
r=summarize(start,finish(fast=90,late=4))
assert not r['headroom_thresholds_met'] and r['percent_within_33_333ms']==96
report={'passed':True,'checks':['exact 95/99 percent boundaries','below-target windows',
        'version/reset/elapsed/counter rejection','whole-window counts, not sampled p95 averages']}
(evidence_directory(ROOT)/'frame-budget-check.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
