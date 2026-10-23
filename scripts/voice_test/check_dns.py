#!/usr/bin/env python3
"""Check controlled DNS results, bounds and the captured socket snapshots."""
import argparse
from collections import Counter
from pathlib import Path
import re

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('log', type=Path)
p.add_argument('--minimum', type=int, default=27)
a = p.parse_args()
raw = a.log.read_text(encoding='utf-8', errors='replace')
raw = re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]', '', raw)
blocks = re.split(r'^[ \t]*\d{4}-[^\n]* CMD ', raw, flags=re.M)[1:]
counts = Counter()
failures = []
for block in blocks:
    cmd = block.splitlines()[0].strip()
    parts = cmd.split()
    if parts[:2] == ['vgvoice', 'dnsprobe']:
        match = re.search(r'dnsprobe rc=(-?\d+) count=(\d+) ms=(\d+)', block)
        if not match:
            failures.append(f'{cmd}: missing result')
            continue
        rc, count, ms = map(int, match.groups())
        host, budget = parts[2], int(parts[5])
        if len(parts) == 7:
            expected, label, limit = (-125, 0), 'cancel', int(parts[6])
        elif host in ('stall.test', 'tcpstall.test'):
            expected, label, limit = (-110, 0), 'timeout', budget
        elif host == 'wrongid.test':
            expected, label, limit = (-74, 0), 'wrong-id', budget
        elif host == 'short.test':
            expected, label, limit = (-84, 0), 'short', budget
        else:
            expected, label, limit = (0, 1), 'success', budget
        if (rc, count) != expected or ms > limit + 300:
            failures.append(f'{cmd}: rc={rc} count={count} ms={ms}, expected={expected}, maximum_ms={limit+300}')
        else:
            counts[label] += 1
            print(f'PASS {label} ms={ms}')
    elif cmd in ('cat /proc/net/tcp', 'cat /proc/net/udp'):
        # This isolated fixture permits only the product MQTT connection.
        for row in block.splitlines()[1:]:
            if re.match(r'\s*\d+:', row) and ':1883' not in row:
                failures.append(f'{cmd}: unexpected connection {row.strip()}')
if sum(counts.values()) < a.minimum:
    failures.append(f'Only {sum(counts.values())} probes passed; minimum={a.minimum}')
if not re.search(r'vgvoice: asr rc=ok status=200', raw):
    failures.append('Missing subsequent real ASR recovery')
for failure in failures:
    print('FAIL', failure)
print('counts', dict(counts), 'failures', len(failures))
raise SystemExit(bool(failures))
