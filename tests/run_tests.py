"""Run native C++ regressions using CXX or the optional ziglang Python package."""
from pathlib import Path
import os
import shlex
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = root / '.build'
build.mkdir(exist_ok=True)
compiler = shlex.split(os.environ['CXX']) if 'CXX' in os.environ else [sys.executable, '-m', 'ziglang', 'c++']
exe = build / ('test_component.exe' if os.name == 'nt' else 'test_component')
env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(build / 'zig-cache'))
subprocess.run(compiler + ['-std=c++17', '-g', '-O0', '-Itests/stubs',
    '-Icomponents/hlk_ld2402', 'tests/test_component.cpp',
    'components/hlk_ld2402/hlk_ld2402.cpp', '-o', str(exe)],
    cwd=root, env=env, check=True)
subprocess.run([str(exe)], cwd=root, check=True)
