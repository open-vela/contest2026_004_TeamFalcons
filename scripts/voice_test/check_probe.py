#!/usr/bin/env python3
"""Check captured NSH transport probes against their command and shared budget."""
import argparse
from collections import Counter
from pathlib import Path
import re

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('log', type=Path)
p.add_argument('--minimum', type=int, default=25)
a = p.parse_args()
text = a.log.read_text(encoding='utf-8', errors='replace')
text = re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]', '', text)
blocks = re.split(r'^[ \t]*\d{4}-[^\n]* CMD ', text, flags=re.M)[1:]
counts = Counter()
failures = []
for block in blocks:
    cmd = block.splitlines()[0].strip()
    parts = cmd.split()
    if parts[:2] != ['vgvoice', 'netprobe']:
        continue
    m = re.search(r'vgvoice: netprobe rc=(-?\d+) status=(\d+) bytes=(\d+) ms=(\d+)', block)
    if not m:
        failures.append(f'{cmd}: missing result')
        continue
    rc, status, received, ms = map(int, m.groups())
    host, port, path = parts[2:5]
    budget = int(parts[5])
    if len(parts) == 7:
        expected, label, limit = (-7, 0), 'cancel', int(parts[6])
    elif port == '8444' or path == '/stall':
        expected, label, limit = (-6, 0), 'timeout', budget
    elif path == '/truncate':
        expected, label, limit = (-4, 0), 'truncate', budget
    elif host == 'token-plan-cn.xiaomimimo.com' or path == '/unauthorized':
        expected, label, limit = (0, 401), '401', budget
    elif path == '/rate-limit':
        expected, label, limit = (0, 429), '429', budget
    else:
        expected, label, limit = (0, 200), '200', budget
    if (rc, status) != expected or ms > limit + 500:
        failures.append(f'{cmd}: rc={rc} status={status} ms={ms}, '
                        f'expected={expected} maximum_ms={limit + 500}')
    else:
        counts[label] += 1
        print(f'PASS {label} ms={ms} bytes={received}')
if sum(counts.values()) < a.minimum:
    failures.append(f'Only {sum(counts.values())} passed; expected at least {a.minimum}')
for failure in failures:
    print('FAIL', failure)
print('counts', dict(counts), 'failures', len(failures))
raise SystemExit(bool(failures))
