"""Whole-window frame-budget evidence from cumulative firmware counters.

The UI timer includes scheduling/preemption and presentation handoff waits,
but excludes its subsequent pacing delay. Do not call it pure CPU time, sum it
with overlapping worker timings, or average rolling p95s into a global p95.
"""
import math

COUNTERS=('frames','presented_frames','frames_within25','missed_frames')

def summarize(start,end):
    if start['version']!=end['version']:raise ValueError('Firmware changed during measurement')
    elapsed=end['uptime_s']-start['uptime_s']
    if not math.isfinite(elapsed) or elapsed<=0:raise ValueError('Invalid elapsed device time')
    counts={k:end[k]-start[k] for k in COUNTERS}
    if any(not isinstance(v,int) or v<0 for v in counts.values()):
        raise ValueError('Non-monotonic or invalid frame counters; check for a reset')
    frames=counts['frames']
    if not frames:raise ValueError('No timed animation frames in this interval')
    if counts['frames_within25']+counts['missed_frames']>frames:
        raise ValueError('Frame timing counters cannot describe the same interval')
    fast=counts['frames_within25'];on_time=frames-counts['missed_frames']
    return {
        'elapsed_s':elapsed,'timed_frames':frames,'presented_frames':counts['presented_frames'],
        'presented_fps':counts['presented_frames']/elapsed,
        'frames_at_most_25ms':fast,'frames_over_33_333ms':counts['missed_frames'],
        'percent_at_most_25ms':100*fast/frames,'percent_within_33_333ms':100*on_time/frames,
        'p95_at_most_25ms':100*fast>=95*frames,
        'at_least_99_percent_within_33_333ms':100*on_time>=99*frames,
        'headroom_thresholds_met':100*fast>=95*frames and 100*on_time>=99*frames,
        'scope':'All timed UI frames in this interval, including preemption and presentation handoff waits; not a complete performance or product acceptance claim',
    }
