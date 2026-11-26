#!/usr/bin/env python3
"""Stable repository and exported-project entry for tools/codecraft/."""
from pathlib import Path
import runpy
import sys


def main(argv=None):
    tooling=Path(__file__).resolve().parent/'tools/codecraft'
    sys.path.insert(0,str(tooling))
    implementation=runpy.run_path(str(tooling/'cli.py'),run_name='codecraft_cli')
    return implementation['main'](argv)


if __name__=='__main__':
    raise SystemExit(main())
