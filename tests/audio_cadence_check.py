"""Deterministic timing cases, including pauses hidden by average packet rate."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from audio_cadence import AudioCadence

a = AudioCadence()
assert not a.summary()['delivery_timing_met']
for n in range(100):
    a.observe(n * .020, n * .020 + .001, n * .020 + .004)
assert a.summary()['delivery_timing_met'] and a.summary()['packets'] == 100

# Both streams deliver 100 packets in about two seconds; one stalls and bursts.
b = AudioCadence()
previous = 0.
for n in range(100):
    start = max(n * .020, previous)
    ack = start + (.120 if n == 40 else .002)
    b.observe(n * .020, start, ack)
    previous = ack
report = b.summary()
assert report['packets'] / previous >= 49
assert not report['delivery_timing_met'] and report['late_slots'] > 0
assert report['ack_gaps_over_budget'] == 1
assert report['worst_ack_gap']['call_ms'] > 119

# Distinguish a delayed host start from time inside the blocking transport call.
c = AudioCadence()
c.observe(1., 1., 1.002)
c.observe(1.020, 1.170, 1.172)
assert c.summary()['maximum']['start_lateness_ms'] > 149
assert c.summary()['maximum']['call_ms'] < 3
assert not c.summary()['delivery_timing_met']

# Inclusive budgets tolerate floating-point representation at exact boundaries.
d = AudioCadence()
d.observe(0., 0., .020)
d.observe(.020, .040, .060)
assert d.summary()['delivery_timing_met']
for _ in range(200):
    c.observe(c.previous_ack + .020, c.previous_ack + .100, c.previous_ack + .102)
assert len(c.summary()['first_outliers']) == 32 and c.summary()['packets'] == 202
for values in [(0., 2., 1.), (0., float('nan'), 3.), (0., 1., float('inf'))]:
    try:
        AudioCadence().observe(*values)
    except ValueError:
        pass
    else:
        raise AssertionError(values)
try:
    a.observe(0., 0., .1)
except ValueError:
    pass
else:
    raise AssertionError('Time reversal accepted')
print('PCM cadence: steady/burst/start-delay/boundary/bounded-storage/invalid-clock checks passed')
