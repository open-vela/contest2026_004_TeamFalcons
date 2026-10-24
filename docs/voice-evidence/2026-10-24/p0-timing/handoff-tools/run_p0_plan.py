import sys, subprocess, shutil
from pathlib import Path

# P0 计时矩阵的计划运行器：与 bvo14 closeout 的 run_native_plan.py 同构，
# 经 Windows 侧 nsh_plan.ps1 驱动 COM3，日志拷回证据目录。
repo = Path('/home/hello19y/openvela/contest2026_004_TeamFalcons')
tmp = Path('/mnt/c/Users/19y/AppData/Local/Temp/vgflash')
plan = Path(sys.argv[1]).resolve()
log = Path(sys.argv[2]).resolve()
shutil.copyfile(repo / 'scripts/voice_test/nsh_plan.ps1', tmp / 'nsh_plan_p0.ps1')
shutil.copyfile(plan, tmp / 'p0-plan.json')
args = ['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
        r'C:\Users\19y\AppData\Local\Temp\vgflash\nsh_plan_p0.ps1',
        '-Plan', r'C:\Users\19y\AppData\Local\Temp\vgflash\p0-plan.json',
        '-LogFile', r'C:\Users\19y\AppData\Local\Temp\vgflash\p0-run.log',
        '-FixtureControlPath',
        r'\\wsl.localhost\Debian\home\hello19y\openvela\contest2026_004_TeamFalcons\.debug\maintenance-slave-mode.txt']
r = subprocess.run(args, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
shutil.copyfile(tmp / 'p0-run.log', log)
if r.returncode:
    print(r.stderr.decode(errors='replace'))
print(log.name, 'exit', r.returncode, flush=True)
sys.exit(r.returncode)
