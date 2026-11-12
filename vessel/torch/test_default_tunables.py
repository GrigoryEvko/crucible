#!/usr/bin/env python3
"""Load the two vessel libraries into a PyTorch process with the default glibc tunables.

A shared library with a variable of the initial-exec TLS model needs its full
TLS block in the static TLS of the process.  dlopen finds that block only in
the surplus that glibc keeps, and the tunable glibc.rtld.optional_static_tls
makes the surplus larger.  The test imports torch first, as each user of the
vessel does, so the libraries of PyTorch use their part of the surplus before
the vessel libraries load.  Then it loads libcrucible_dispatch.so and
libcrucible_vessel.so with dlopen.  A TLS block that does not fit makes dlopen
fail with "cannot allocate memory in static TLS block".

The test launcher of the tree gives each test a clean environment with no
GLIBC_TUNABLES.  A direct run with GLIBC_TUNABLES set does not measure the
default tunables, so the test refuses it.

Usage:
    CRUCIBLE_BUILD_DIR=build-vessel python test_default_tunables.py

Exit 0 when each library loads, 1 when a load fails or GLIBC_TUNABLES is set.
"""

import ctypes
import os
import sys
from pathlib import Path

import torch

from crucible_native import checked_lib_path

# One intra-op thread and one inter-op thread, as in each vessel test.
torch.set_num_threads(1)
torch.set_num_interop_threads(1)

LIBRARIES = ("libcrucible_dispatch.so", "libcrucible_vessel.so")
BUILD_COMMAND = "  cmake --preset vessel && cmake --build --preset vessel\nand set CRUCIBLE_BUILD_DIR=build-vessel"
# The ELF program header type of the TLS segment, the dynamic tag DT_FLAGS and its bit DF_STATIC_TLS.
PT_TLS = 7
PT_DYNAMIC = 2
DT_FLAGS = 30
DF_STATIC_TLS = 0x10


def tls_facts(path: str) -> tuple[int, bool]:
    """Return the memory size of the TLS segment of an ELF64 library, and True when it uses the static TLS model.

    Complexity: linear in the program headers and in the entries of the dynamic section.
    """
    image = Path(path).read_bytes()
    table = int.from_bytes(image[0x20:0x28], "little")
    entry_size = int.from_bytes(image[0x36:0x38], "little")
    count = int.from_bytes(image[0x38:0x3A], "little")
    tls_size = 0
    is_static = False
    for index in range(count):
        at = table + index * entry_size
        kind = int.from_bytes(image[at : at + 4], "little")
        offset = int.from_bytes(image[at + 8 : at + 16], "little")
        file_size = int.from_bytes(image[at + 32 : at + 40], "little")
        if kind == PT_TLS:
            tls_size = int.from_bytes(image[at + 40 : at + 48], "little")
        if kind == PT_DYNAMIC:
            for entry in range(offset, offset + file_size, 16):
                tag = int.from_bytes(image[entry : entry + 8], "little")
                value = int.from_bytes(image[entry + 8 : entry + 16], "little")
                if tag == DT_FLAGS and value & DF_STATIC_TLS:
                    is_static = True
    return tls_size, is_static


def main() -> int:
    """Load each vessel library after torch, and print its TLS facts."""
    if "GLIBC_TUNABLES" in os.environ:
        print(
            f"test_default_tunables: GLIBC_TUNABLES={os.environ['GLIBC_TUNABLES']} is set, and the test measures the "
            f"default tunables.  Unset GLIBC_TUNABLES, or run the test through ctest"
        )
        return 1
    print(f"test_default_tunables: torch {torch.__version__} is loaded")
    failures = 0
    for name in LIBRARIES:
        path = checked_lib_path(name, None, BUILD_COMMAND)
        tls_size, is_static = tls_facts(path)
        model = "initial-exec" if is_static else "dynamic"
        try:
            ctypes.CDLL(path)
        except OSError as problem:
            print(f"test_default_tunables: {name} does not load ({model} TLS of {tls_size} bytes): {problem}")
            failures += 1
            continue
        print(f"test_default_tunables: {name} loads ({model} TLS of {tls_size} bytes) from {path}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
