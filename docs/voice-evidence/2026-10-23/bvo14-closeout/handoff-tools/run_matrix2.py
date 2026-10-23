from pathlib import Path
import subprocess,json,sys
r=Path("/home/hello19y/openvela/contest2026_004_TeamFalcons/docs/voice-evidence/2026-10-23/bvo14-closeout")
out=r/"matrix-foreground2"
steps=[(r/"final-boot-plan.json",out/"boot.log")]
steps += [(out/f"matrix-{g}.json",out/f"matrix-{g}.log") for g in ["anomaly","point","recovery"]]
results=[]
for plan,log in steps:
 rc=subprocess.call(["python3",str(Path(__file__).with_name("run_native_plan.py")),str(plan),str(log)])
 results.append({"plan":plan.name,"harness_exit":rc})
 (out/"harness-status.json").write_text(json.dumps(results,indent=2)+chr(10))
 if rc:raise SystemExit(rc)
raise SystemExit(subprocess.call(["python3","scripts/voice_test/check_matrix.py",str(out)]))
