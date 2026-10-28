#!/usr/bin/env python3
"""Guard the system-entry stack, which ignores builtin STACKSIZE metadata."""
from pathlib import Path
import re

preset = Path(__file__).resolve().parents[3] / 'scripts/configs/velaguard-lvgl.defconfig'
config = preset.read_text()
assert 'CONFIG_INIT_ENTRYPOINT="velaguard_app_main"' in config
match = re.search(r'^CONFIG_INIT_STACKSIZE=(\d+)$', config, re.MULTILINE)
stack = int(match[1]) if match else 2048  # NuttX Kconfig default
# Board reproduction: 2 KiB overflows in open/getumask while refreshing Skills.
assert stack >= 8192, f'boot storage/seed stack insufficient: {stack} < 8192'
for option in ['SCHED_HAVE_PARENT', 'SCHED_CHILD_STATUS']:
    assert f'CONFIG_{option}=y' in config, f'bounded shell waitpid requires {option}'
print(f'test_boot_stack: OK (system entry stack={stack})')
