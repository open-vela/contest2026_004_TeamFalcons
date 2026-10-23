#!/usr/bin/env python3
"""Summarize every fixed synthetic attempt; missing/failed attempts stay failures."""
import argparse
import csv
import json
import re
import statistics
from pathlib import Path
p = argparse.ArgumentParser()
p.add_argument('directory', type=Path)
a = p.parse_args()
rows = []
for group, tool in [('anomaly', 'vg_anomaly_read'), ('point', 'vg_point_read'), ('recovery', 'vg_alarm_recovery')]:
    log = (a.directory / f'matrix-{group}.log').read_text()
    for marker in re.finditer(r'CMD echo CASE_(' + group + r'\d{2})', log):
        block = log[marker.end():]
        next_case = re.search(r'CMD echo CASE_', block)
        if next_case:
            block = block[:next_case.start()]
        ready = re.search(r'first_playable gen=\d+ elapsed_ms=(\d+)', block)
        state = re.search(r'round visible=\d+ state=(\d+).*failed=(\d+)', block)
        private = re.search(r'intent read (\w+) rc=0 captured=1', block)
        answers = re.findall(r'(?m)^a=(.*?)(?=\nnsh>|\n\x1b|\n20\d\d-|\Z)', block, re.S)
        answer = next((text.strip() for text in answers if text.strip()), '')
        consistent = ('当前没有异常点位' in answer if group == 'anomaly' else
            bool(re.search(r'^ups_load UPS负载：40(?:\.0+)?\s*%', answer)) if group == 'point' else
            '无法确认 ups_load' in answer and '当前正常不能证明' in answer)
        correct = bool(private and private[1] == tool and consistent)
        ms = int(ready[1]) if ready else None
        complete = bool(ms is not None and ms <= 30000 and state and
                        state[1] == '8' and state[2] == '0' and correct)
        rows.append(dict(id=marker[1], intent=group, first_playable_ms=ms,
            state=int(state[1]) if state else None,
            speech_failed=int(state[2]) if state else None,
            text_correct=correct, complete=complete, answer=answer))
expected = {item['id'] for item in json.loads((a.directory / 'cases.json').read_text())}
if len(rows) != 30 or {row['id'] for row in rows} != expected:
    raise SystemExit('Incomplete or duplicated 30-case evidence; cannot report a complete matrix')
summary = {'attempts': len(rows), 'complete': sum(row['complete'] for row in rows),
           'correct_text': sum(row['text_correct'] for row in rows), 'groups': {}}
for group in ['anomaly', 'point', 'recovery']:
    selected = [row for row in rows if row['intent'] == group]
    times = [row['first_playable_ms'] for row in selected if row['complete']]
    summary['groups'][group] = dict(attempts=len(selected),
        complete=sum(row['complete'] for row in selected),
        correct_text=sum(row['text_correct'] for row in selected),
        median_ms_success=statistics.median(times) if times else None,
        max_ms_success=max(times) if times else None)
(a.directory / 'matrix-results.json').write_text(json.dumps(rows, ensure_ascii=False, indent=2) + '\n')
(a.directory / 'matrix-summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n')
with (a.directory / 'matrix-results.csv').open('w', newline='') as stream:
    writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
    writer.writeheader()
    writer.writerows(rows)
print(json.dumps(summary, ensure_ascii=False))
