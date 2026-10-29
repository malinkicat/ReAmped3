"""Paths for the Python tools: the repository root and settings from
tools/local.env (not committed; see tools/local.env.example)."""
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def local_setting(name, default):
    value = os.environ.get(name)
    if value:
        return value
    try:
        for line in open(os.path.join(ROOT, 'tools', 'local.env')):
            key, _, val = line.strip().partition('=')
            if key == name and not line.lstrip().startswith('#'):
                return val.strip().strip('"')
    except OSError:
        pass
    return default


SYMBOLIZER = os.path.join(local_setting('LLVM_BIN', '/mnt/c/llvm/bin'), 'llvm-symbolizer.exe')
