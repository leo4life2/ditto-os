"""Bounded diagnostics for the 20 ms synthetic PCM delivery workload.

These timestamps are host observations, not speaker output or acoustic latency.
An average of 50 packets/s can hide a pause followed by a catch-up burst.
"""
import math


class AudioCadence:
    period_s = .020
    max_ack_gap_s = .040

    def __init__(self):
        self.count = self.late_slots = self.long_ack_gaps = 0
        self.origin = self.previous_start = self.previous_ack = None
        self.maximum = dict(start_lateness_ms=0., call_ms=0., start_gap_ms=0., ack_gap_ms=0.)
        self.outliers = []
        self.worst_ack_gap = None

    def observe(self, deadline, started, acknowledged):
        if not all(math.isfinite(v) for v in (deadline, started, acknowledged)):
            raise ValueError('Non-finite PCM timestamp')
        if acknowledged < started or (self.previous_ack is not None and started < self.previous_ack):
            raise ValueError('PCM timestamps are not serial and monotonic')
        if self.origin is None:
            self.origin = deadline
        row = {
            'packet': self.count,
            'elapsed_s': started - self.origin,
            'start_lateness_ms': max(0., started - deadline) * 1000,
            'call_ms': (acknowledged - started) * 1000,
            'start_gap_ms': 0. if self.previous_start is None else (started - self.previous_start) * 1000,
            'ack_gap_ms': 0. if self.previous_ack is None else (acknowledged - self.previous_ack) * 1000,
        }
        late = started - deadline > self.period_s + 1e-9
        gap = self.previous_ack is not None and acknowledged - self.previous_ack > self.max_ack_gap_s + 1e-9
        self.late_slots += late
        self.long_ack_gaps += gap
        self.count += 1
        if (late or gap) and len(self.outliers) < 32:
            self.outliers.append(row)
        if self.worst_ack_gap is None or row['ack_gap_ms'] > self.worst_ack_gap['ack_gap_ms']:
            self.worst_ack_gap = row
        for key in self.maximum:
            self.maximum[key] = max(self.maximum[key], row[key])
        self.previous_start, self.previous_ack = started, acknowledged

    def summary(self):
        return {
            'packets': self.count,
            'late_slots': self.late_slots,
            'ack_gaps_over_budget': self.long_ack_gaps,
            'max_ack_gap_ms': self.maximum['ack_gap_ms'],
            'maximum': dict(self.maximum),
            'first_outliers': list(self.outliers),
            'worst_ack_gap': self.worst_ack_gap,
            'thresholds': {'packet_ms': 20, 'maximum_start_lateness_ms': 20, 'maximum_ack_gap_ms': 40},
            'delivery_timing_met': self.count >= 2 and not self.late_slots and not self.long_ack_gaps,
            'scope': 'Synthetic PCM workload: no start more than one packet late, no acknowledgement gap over two packets. Includes host scheduling and USB RPC. Not an audible-underrun or acoustic measurement.',
        }
