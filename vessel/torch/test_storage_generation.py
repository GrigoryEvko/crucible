#!/usr/bin/env python3
"""The generation allocator gives each CPU storage of a session a generation, and gives PyTorch back as it was.

A session of CrucibleNative sets the generation allocator of the dispatch
library (storage_generation.h) as the CPU allocator of PyTorch.  The checks:

    chain           the generation allocator allocates through the CPU
                    allocator that was set before the session, and the end of
                    the session puts that allocator back
    generation      each storage of the session has a generation of its own,
                    and a view has the generation of its base.  An empty
                    tensor has no data address and no generation
    alignment       the data of each storage keeps the alignment of 64 bytes
                    that PyTorch asks of its CPU allocator
    other allocators a storage that PyTorch made over a buffer of Python has
                    no generation, and a loop that reads it still gets a
                    region
    life            a tensor of the session that ends after the session ends
                    gives its bytes back, and a resize of it after the session
                    still allocates through the generation allocator
    oneDNN          an op of oneDNN, which allocates through raw_allocate,
                    runs in a session
    lazy clone      _lazy_clone refuses a storage of the session, so no
                    copy-on-write storage hides its generation

Usage:
    python test_storage_generation.py

Exit 0 when each check passes, 1 otherwise.
"""

from __future__ import annotations

import gc
import sys
from pathlib import Path

# A caller starts this file by its path, so the vessel directory is not always
# on the import path.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import torch  # noqa: E402
from crucible_native import CrucibleNative, _DispatchLib  # noqa: E402

torch.set_num_threads(1)
torch.set_num_interop_threads(1)

ALIGNMENT = 64
MAX_ITERATIONS = 40


def check_session(lib: _DispatchLib, prior: int) -> tuple[list[str], torch.Tensor]:
    """Run the checks inside one session, and return the problems and a tensor that outlives the session."""
    problems = []
    with CrucibleNative() as ctx:
        if lib.cpu_allocator() != lib.generation_allocator():
            problems.append("the CPU allocator of the session is not the generation allocator")
        if lib.inner_allocator() != prior:
            problems.append("the generation allocator does not allocate through the allocator before the session")

        first = torch.empty(1000)
        second = torch.empty(1000)
        view = first[10:]
        empty = torch.empty(0)
        generations = [lib.storage_generation(tensor) for tensor in (first, second)]
        if 0 in generations or generations[0] == generations[1]:
            problems.append(f"two storages of the session have the generations {generations}")
        if lib.storage_generation(view) != generations[0]:
            problems.append("a view does not have the generation of its base")
        if empty.data_ptr() != 0 or lib.storage_generation(empty) != 0:
            problems.append("an empty tensor has a data address or a generation")
        for size in (1, 7, 1000, 1 << 20):
            address = torch.empty(size, dtype=torch.uint8).data_ptr()
            if address % ALIGNMENT != 0:
                problems.append(f"the data of a storage of {size} bytes is at {address:#x}, not aligned to {ALIGNMENT}")

        buffer = bytearray(64)
        borrowed = torch.frombuffer(buffer, dtype=torch.float32)
        if lib.storage_generation(borrowed) != 0:
            problems.append("a storage over a buffer of Python has a generation")
        for _ in range(MAX_ITERATIONS):
            torch.add(borrowed, borrowed)
            if ctx.active_num_ops() > 0:
                break
            ctx.flush()
        if ctx.active_num_ops() == 0:
            problems.append(f"a loop that reads a storage of another allocator has no region after {MAX_ITERATIONS}")

        if torch.backends.mkldnn.is_available():
            dense = torch.ones(4, 4).to_mkldnn().to_dense()
            if float(dense.sum()) != 16.0:
                problems.append("an op of oneDNN in the session gives a wrong result")

        try:
            first._lazy_clone()
            problems.append("_lazy_clone made a copy-on-write storage of a storage of the session")
        except RuntimeError:
            pass

        survivor = torch.ones(1000)
    return problems, survivor


def main() -> int:
    """Run each check and report."""
    lib = _DispatchLib()
    prior = lib.cpu_allocator()
    problems, survivor = check_session(lib, prior)
    if lib.cpu_allocator() != prior:
        problems.append("the end of the session did not put the prior CPU allocator back")

    generation = lib.storage_generation(survivor)
    if generation == 0:
        problems.append("a tensor of the session lost its generation after the session")
    survivor.resize_(1 << 16)
    if lib.storage_generation(survivor) == 0:
        problems.append("a resize after the session did not allocate through the generation allocator")
    del survivor
    gc.collect()
    if lib.storage_generation(torch.empty(1000)) != 0:
        problems.append("a storage after the session has a generation")

    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print("test_storage_generation: each check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
