#!/usr/bin/env python3
"""核对整轮故障现场；只有到达目标故障且完成回收才算覆盖。"""
import argparse
import json
import re
from pathlib import Path
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('directory', type=Path)
a = p.parse_args()
expected = {
    'asr401': ('http', 401), 'asr429': ('http', 429),
    'asrempty': ('empty', 200), 'asrbad': ('format', 200),
    'asrtruncate': ('net', 0), 'asrdns': ('net', 0), 'asrstall': ('timeout', 0),
    'tts401': ('http', 401), 'tts429': ('http', 429),
    'ttstruncate': ('net', 0), 'ttsbad': ('format', 0), 'ttsstall': ('timeout', 0),
}
rows = []
for file in sorted((a.directory / 'faults').glob('*.log')):
    raw = file.read_text(errors='replace')
    log = re.sub(r'\x1b\[[0-9;]*[A-Za-z]', '', raw).replace('nsh> ', '')
    markers = list(re.finditer(r'CMD echo FAULT_(\w+)_(\d+)', log))
    for i, marker in enumerate(markers):
        profile, attempt = marker.groups()
        block = log[marker.end():markers[i+1].start() if i+1 < len(markers) else len(log)]
        target = 'asr' if profile.startswith('asr') else 'tts'
        got = re.search(r'\[vgvoice\] '+target+r' rc=(\w+) status=(\d+)', block)
        settled = re.search(r'round_settled gen=\d+ state=(\d+) failed=(\d+) elapsed_ms=(\d+)', block)
        state = re.search(r'round visible=\d+ state=(\d+).*failed=(\d+)', block)
        matches = bool(got and (got[1],int(got[2])) == expected[profile])
        # TTS 的传输层可在解析前发现截断；畸形 SSE 的 sink 会返回格式错误。
        cleanup = block.split('CMD vgvoice round end', 1)[-1]
        reclaimed = bool(re.search(r'vgvoice: state=idle retained_pcm=0', cleanup) and
                         re.search(r'cancel_settled elapsed_ms=\d+ retained_pcm=0 controlled_active=0', cleanup))
        kept = target == 'asr' or bool(re.search(r'(?m)^a=当前没有异常点位。', block))
        failed_state = bool(state and state[1] == ('9' if target == 'asr' else '8') and
                            state[2] == ('0' if target == 'asr' else '1'))
        elapsed = int(settled[3]) if settled else None
        stages = [int(t) for t in re.findall(r'stage=\w+ t=(\d+)',block)]
        if elapsed is not None and elapsed < max(stages,default=0): elapsed = None
        rows.append(dict(file=file.name, profile=profile, attempt=int(attempt),
            result=got[1] if got else None,status=int(got[2]) if got else None,
            reached_expected_fault=matches, error_state=failed_state,
            text_retained=kept,reclaimed=reclaimed,
            elapsed_ms=elapsed,
            passed=bool(matches and elapsed is not None and failed_state and kept and reclaimed)))
summary = {'attempts':len(rows), 'passed':sum(r['passed'] for r in rows), 'profiles':{}}
for profile in expected:
    selected = [r for r in rows if r['profile']==profile]
    summary['profiles'][profile] = {'attempts':len(selected),'passed':sum(r['passed'] for r in selected)}
summary['coverage_complete'] = all(item['passed'] >= 3 for item in summary['profiles'].values())
(a.directory / 'round-fault-results.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2)+'\n')
(a.directory / 'round-fault-summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n')
print(json.dumps(summary,ensure_ascii=False))
