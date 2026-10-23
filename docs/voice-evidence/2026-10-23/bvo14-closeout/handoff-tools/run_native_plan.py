import sys,subprocess,shutil
from pathlib import Path
repo=Path('/home/hello19y/openvela/contest2026_004_TeamFalcons')
tmp=Path('/mnt/c/Users/19y/AppData/Local/Temp/vgflash')
plan=Path(sys.argv[1]).resolve();log=Path(sys.argv[2]).resolve()
shutil.copyfile(repo/'scripts/voice_test/nsh_plan.ps1',tmp/'nsh_plan_closeout.ps1')
shutil.copyfile(plan,tmp/'closeout-plan.json')
args=['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',r'C:\Users\19y\AppData\Local\Temp\vgflash\nsh_plan_closeout.ps1','-Plan',r'C:\Users\19y\AppData\Local\Temp\vgflash\closeout-plan.json','-LogFile',r'C:\Users\19y\AppData\Local\Temp\vgflash\closeout-run.log','-FixtureControlPath',r'\\wsl.localhost\Debian\home\hello19y\openvela\contest2026_004_TeamFalcons\.debug\maintenance-slave-mode.txt']
r=subprocess.run(args,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
shutil.copyfile(tmp/'closeout-run.log',log)
if r.returncode: print(r.stderr.decode(errors='replace'))
print(log.name,'exit',r.returncode,flush=True)
sys.exit(r.returncode)
