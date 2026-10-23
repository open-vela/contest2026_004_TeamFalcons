from pathlib import Path
import subprocess,json
r=Path('/home/hello19y/openvela/contest2026_004_TeamFalcons/docs/voice-evidence/2026-10-23/bvo14-closeout')
steps=[(r/'final-boot-plan.json',r/'foreground-boot.log')]
steps += [(r/'matrix-foreground'/f'matrix-{g}.json',r/'matrix-foreground'/f'matrix-{g}.log') for g in ['anomaly','point','recovery']]
results=[]
for plan,log in steps:
 rc=subprocess.call(['python3',str(Path(__file__).with_name('run_native_plan.py')),str(plan),str(log)])
 results.append({'plan':plan.name,'harness_exit':rc})
 (r/'foreground-matrix-harness-status.json').write_text(json.dumps(results,indent=2)+'\n')
 if rc:raise SystemExit(rc)
raise SystemExit(subprocess.call(['python3','scripts/voice_test/check_matrix.py',str(r/'matrix-foreground')]))
