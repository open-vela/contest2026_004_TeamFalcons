#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run real OTA parser/storage fault paths with ASan and UBSan."""
from pathlib import Path
import os, shlex, subprocess
here=Path(__file__).resolve().parent
out=here.parents[2]/'.debug/ota-faults-sanitized'
plan=subprocess.run(['make','--no-print-directory','-B','-n','test_ota_faults'],cwd=here,text=True,capture_output=True,check=True).stdout
command=next(shlex.split(line) for line in plan.splitlines() if line.startswith(('cc ','gcc ','clang ')))
command[command.index('-o')+1]=str(out)
command[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g']
subprocess.run(command,cwd=here,check=True)
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
subprocess.run([str(out)],cwd=here,env=env,check=True,timeout=60)
print('test_ota_sanitize: OK (ASan, UBSan, leak detection)')
