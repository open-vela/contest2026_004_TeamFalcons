#!/usr/bin/env python3
"""记录本次镜像和工作区来源；只读取指定配置项，不输出凭据。"""
import argparse
import datetime
import hashlib
import json
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('output', type=Path)
p.add_argument('--label', required=True)
p.add_argument('--fixture', action='store_true')
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
def git(repo, *args):
    return subprocess.check_output(['git', '-C', str(repo), *args], text=True).strip()
config = (root.parent / 'nuttx/.config').read_text()
keys = ['CONFIG_VG_VOICE_TLS_CACHE', 'CONFIG_VG_VOICE_TEST_FIXTURE', 'CONFIG_VG_VOICE_DIRECT_TOOL',
        'CONFIG_IOB_NBUFFERS', 'CONFIG_NET_SOLINGER', 'CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN']
options = {key: next((line.split('=', 1)[1] for line in config.splitlines()
                     if line.startswith(key + '=')), 'n') for key in keys}
assert options['CONFIG_VG_VOICE_DIRECT_TOOL'] == 'n'
assert options['CONFIG_VG_VOICE_TLS_CACHE'] == 'n'
assert (options['CONFIG_VG_VOICE_TEST_FIXTURE'] == 'y') == a.fixture
result = {'time': datetime.datetime.now(datetime.timezone.utc).isoformat(),
          'label': a.label, 'options': options, 'repositories': {},
          'config_sha256': sha(root.parent / 'nuttx/.config'), 'images': {}}
for rel in ('.', '../packages/ai_agent', '../nuttx', '../apps'):
    repo = root / rel
    paths = set(git(repo, 'diff', '--name-only').splitlines())
    paths.update(git(repo, 'ls-files', '--others', '--exclude-standard').splitlines())
    sources = {}
    for name in sorted(paths):
        f = repo / name
        if f.is_file() and (f.suffix in ('.c', '.h', '.sh', '.py', '.defconfig') or
                            f.name in ('Kconfig', 'Makefile', 'CMakeLists.txt')):
            sources[name] = sha(f)
    result['repositories'][rel] = {'head': git(repo, 'rev-parse', 'HEAD'),
        'branch': git(repo, 'branch', '--show-current'), 'working_sources': sources}
for name in ('nuttx.hex', 'nuttx.bin', 'nuttx.elf', 'qspi_bootstub.hex'):
    result['images'][name] = sha(root / '.debug' / name)
a.output.write_text(json.dumps(result, indent=2) + '\n')
print('firmware provenance saved:', a.output)
