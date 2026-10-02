#!/usr/bin/env python3
"""Run one negative-compile fixture.  CTest runs this script for each fixture.

usage: neg_compile_driver.py [--warnings-dir DIR] <build-dir> <source> <fixture-name> <expected-regex> ...

The code is in test/neg_compile_store.py, whose module text tells what a run
does.  Python compiles the script that it runs on each start, and it keeps the
compiled form of each module that it imports.  So this script holds no code of
its own, and the module loads from its compiled form.  The compiled modules go
to neg-compile/pycache in the build directory (sys.pycache_prefix), so the
source tree gets no __pycache__ directory.
"""

import os
import sys


def _build_directory(arguments: list[str]) -> str:
    """Return the build directory of the command line `arguments`, or an empty text."""
    rest = arguments[1:]
    if rest[:1] == ["--warnings-dir"]:
        rest = rest[2:]
    return rest[0] if rest else ""


if __name__ == "__main__":
    build = _build_directory(sys.argv)
    if build and os.path.isdir(build):
        sys.pycache_prefix = os.path.join(os.path.abspath(build), "neg-compile", "pycache")
    import neg_compile_store

    raise SystemExit(neg_compile_store.main(sys.argv))
