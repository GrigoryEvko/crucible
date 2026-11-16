#!/usr/bin/env python3
"""The CPU allocator follows the mode of the Vigil, and each storage of a record has a generation.

A session of CrucibleNative sets the generation allocator of the dispatch
library (storage_generation.h) as the CPU allocator of PyTorch while its Vigil
records, and the prior allocator while its Vigil replays.  The checks:

    chain           the generation allocator allocates through the CPU
                    allocator that was set before the session, and the end of
                    the session puts that allocator back
    generation      each storage of a record has a known generation of its
                    own, and a view has the generation of its base.  An empty
                    tensor has no data address and no known generation
    alignment       the data of each storage keeps the alignment of 64 bytes
                    that PyTorch asks of its CPU allocator
    other allocators a storage that PyTorch made over a buffer of Python has
                    no known generation, and a loop that reads it still gets a
                    region
    life            a tensor of the session that ends after the session ends
                    gives its bytes back through its own deleter, and a resize
                    of it after the session
                    still allocates through the generation allocator
    oneDNN          an op of oneDNN, which allocates through raw_allocate,
                    runs in a session
    lazy clone      _lazy_clone refuses a storage of the session, so no
                    copy-on-write storage hides its generation
    replay          a replay runs on the prior allocator in a new period, and
                    a storage that it makes has an unknown generation.  A
                    storage of the record keeps its generation, and its free in
                    the replay gives its bytes back through its own deleter
    divergence      the record after a divergence runs on the generation
                    allocator again, in the period of the replay before it
    parent          a divergence of a child op sets the generation allocator
                    before its parent returns, and the ops after the parent
                    read its storages in the period of the replay

Usage:
    python test_storage_generation.py

Exit 0 when each check passes, 1 otherwise.
"""

from __future__ import annotations

import ctypes
import gc
import sys
from pathlib import Path

# A caller starts this file by its path, so the vessel directory is not always
# on the import path.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import torch  # noqa: E402
from crucible_native import STORAGE_GENERATION_UNKNOWN_BIT, CrucibleNative, _DispatchLib  # noqa: E402

torch.set_num_threads(1)
torch.set_num_interop_threads(1)

ALIGNMENT = 64
MAX_ITERATIONS = 40
# The most iterations of the add loop before its region must replay.
MAX_REPLAY_ITERATIONS = 2_000
# The bytes of the storage of the record that the replay frees: 4 MiB, far
# above the other allocations of the process between the two reads of malloc.
# A check of a free asks for half of the bytes, so a small allocation of
# another thread between the two reads does not change its result.
FREED_BYTES = 4 << 20
# The float elements of the storage that outlives its session after its resize:
# 256 KiB.
SURVIVOR_ELEMENTS = 1 << 16


class MallInfo2(ctypes.Structure):
    """The counts of struct mallinfo2 of glibc, in its order."""

    _fields_ = [(name, ctypes.c_size_t) for name in ("arena", "ordblks", "smblks", "hblks", "hblkhd", "usmblks",
                                                     "fsmblks", "uordblks", "fordblks", "keepcost")]


_LIBC = ctypes.CDLL(None)
_LIBC.mallinfo2.restype = MallInfo2
_LIBC.mallinfo2.argtypes = []


def malloc_bytes_in_use() -> int:
    """The bytes of malloc in use: the chunks of the arenas and the mapped chunks."""
    info = _LIBC.mallinfo2()
    return info.uordblks + info.hblkhd


def is_known(word: int) -> bool:
    """True for the word of a known generation."""
    return word != 0 and (word & STORAGE_GENERATION_UNKNOWN_BIT) == 0


def is_unknown(word: int) -> bool:
    """True for the word of an unknown generation."""
    return (word & STORAGE_GENERATION_UNKNOWN_BIT) != 0


def check_session(lib: _DispatchLib, prior: int) -> tuple[list[str], torch.Tensor]:
    """Run the checks inside one session, and return the problems and a tensor that outlives the session."""
    problems = []
    with CrucibleNative() as ctx:
        if lib.cpu_allocator() != lib.generation_allocator():
            problems.append("the CPU allocator of a session that records is not the generation allocator")
        if lib.inner_allocator() != prior:
            problems.append("the generation allocator does not allocate through the allocator before the session")

        first = torch.empty(1000)
        second = torch.empty(1000)
        view = first[10:]
        empty = torch.empty(0)
        generations = [lib.storage_generation(tensor) for tensor in (first, second)]
        if not all(is_known(word) for word in generations) or generations[0] == generations[1]:
            problems.append(f"two storages of the session have the generations {generations}")
        if lib.storage_generation(view) != generations[0]:
            problems.append("a view does not have the generation of its base")
        if empty.data_ptr() != 0 or is_known(lib.storage_generation(empty)):
            problems.append("an empty tensor has a data address or a known generation")
        for size in (1, 7, 1000, 1 << 20):
            address = torch.empty(size, dtype=torch.uint8).data_ptr()
            if address % ALIGNMENT != 0:
                problems.append(f"the data of a storage of {size} bytes is at {address:#x}, not aligned to {ALIGNMENT}")

        buffer = bytearray(64)
        borrowed = torch.frombuffer(buffer, dtype=torch.float32)
        if not is_unknown(lib.storage_generation(borrowed)):
            problems.append("a storage over a buffer of Python has no unknown generation")
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


def check_replay(lib: _DispatchLib, prior: int) -> list[str]:
    """Run the checks of the replay and of the divergence in one session."""
    problems = []
    with CrucibleNative() as ctx:
        recorded = torch.ones(FREED_BYTES // 4)
        recorded_word = lib.storage_generation(recorded)
        if not is_known(recorded_word):
            problems.append("a storage of the record has no known generation")
        buffer = bytearray(64)
        borrowed = torch.frombuffer(buffer, dtype=torch.float32)
        borrowed_word = lib.storage_generation(borrowed)

        first = torch.ones(4)
        second = torch.ones(4)
        third = torch.ones(8)
        for _ in range(MAX_REPLAY_ITERATIONS):
            torch.add(first, second)
            if ctx.is_compiled():
                break
            ctx.flush()
        if not ctx.is_compiled():
            return problems + [f"the add loop does not replay after {MAX_REPLAY_ITERATIONS} iterations"]

        # The op that activates the region ran before the activation, so the
        # change comes before the next op.
        made = torch.add(first, second)
        if not ctx.is_compiled():
            return problems + ["the add loop diverged in its first replayed op"]
        if lib.cpu_allocator() != prior:
            problems.append("the CPU allocator of a replay is not the prior allocator")
        made_word = lib.storage_generation(made)
        if not is_unknown(made_word):
            problems.append(f"a storage of the replay has the word {made_word:#x}, not an unknown generation")
        if lib.storage_generation(recorded) != recorded_word:
            problems.append("a storage of the record lost its generation in the replay")
        if lib.storage_generation(borrowed) == borrowed_word:
            problems.append("the replay did not start a new period of the allocator")

        before = malloc_bytes_in_use()
        del recorded
        gc.collect()
        freed = before - malloc_bytes_in_use()
        if freed < FREED_BYTES // 2:
            problems.append(f"the free of a storage of the record in the replay gave {freed} bytes back to malloc, "
                            f"not about {FREED_BYTES}")
        if not ctx.is_compiled():
            problems.append("the free of a storage of the record stopped the replay")

        # Another shape diverges at the add, which runs no op inside it.  The
        # exit check of the add sets the generation allocator before the add
        # returns, and the Vigil records again.
        diverged = torch.add(third, third)
        if ctx.is_compiled():
            return problems + ["the add of another shape did not diverge"]
        if lib.cpu_allocator() != lib.generation_allocator():
            problems.append("the CPU allocator after the op that diverged is not the generation allocator")
        if lib.storage_generation(diverged) != made_word:
            problems.append("the result of the op that diverged has another period than the replay before it")
        after = torch.add(third, third)
        if lib.cpu_allocator() != lib.generation_allocator():
            problems.append("the CPU allocator of the record after a divergence is not the generation allocator")
        if not is_known(lib.storage_generation(after)):
            problems.append("a storage of the record after a divergence has no known generation")
        if lib.storage_generation(made) != made_word:
            problems.append("the record after a divergence changed the period of a storage of the replay")
    if lib.cpu_allocator() != prior:
        problems.append("the end of a session in a record did not put the prior CPU allocator back")
    return problems


def check_divergence_in_a_parent(lib: _DispatchLib) -> list[str]:
    """Run the checks of a divergence of a child op inside a parent op that the Vigil then records.

    The region is a loop of linear, whose child addmm diverges on another
    batch.  The Vigil records linear after the divergence, with the storages
    that it read and made in the replay.  The exit check of addmm sets the
    generation allocator before linear returns, and the period stays, so the
    ops after linear read those storages with the key that linear gave them.
    """
    problems = []
    with CrucibleNative() as ctx:
        weight = torch.ones(16, 8)
        bias = torch.ones(16)
        batch = torch.ones(4, 8)
        other = torch.ones(2, 8)
        for _ in range(MAX_REPLAY_ITERATIONS):
            torch.nn.functional.linear(batch, weight, bias)
            if ctx.is_compiled():
                break
            ctx.flush()
        if not ctx.is_compiled():
            return [f"the linear loop does not replay after {MAX_REPLAY_ITERATIONS} iterations"]

        replayed = torch.nn.functional.linear(batch, weight, bias)
        if not ctx.is_compiled():
            return ["the linear loop diverged in its first replayed op"]
        replayed_word = lib.storage_generation(replayed)
        if not is_unknown(replayed_word):
            problems.append(f"the result of a replayed linear has the word {replayed_word:#x}, not an unknown one")

        diverged = torch.nn.functional.linear(other, weight, bias)
        if ctx.is_compiled():
            return problems + ["the linear of another batch did not diverge"]
        if lib.cpu_allocator() != lib.generation_allocator():
            problems.append("the CPU allocator after a divergence inside linear is not the generation allocator")
        if lib.storage_generation(replayed) != replayed_word:
            problems.append("a divergence inside linear changed the period of a storage of the replay")
        if lib.storage_generation(diverged) != replayed_word:
            problems.append("the result of linear that diverged has another period than the replay before it")

        torch.relu(diverged)
        if lib.storage_generation(diverged) != replayed_word:
            problems.append("the op after linear reads its result in another period")
    return problems


def main() -> int:
    """Run each check and report."""
    lib = _DispatchLib()
    prior = lib.cpu_allocator()
    problems, survivor = check_session(lib, prior)
    if lib.cpu_allocator() != prior:
        problems.append("the end of the session did not put the prior CPU allocator back")

    generation = lib.storage_generation(survivor)
    if not is_known(generation):
        problems.append("a tensor of the session lost its generation after the session")
    survivor.resize_(SURVIVOR_ELEMENTS)
    if not is_known(lib.storage_generation(survivor)):
        problems.append("a resize after the session did not allocate through the generation allocator")
    before = malloc_bytes_in_use()
    del survivor
    gc.collect()
    freed = before - malloc_bytes_in_use()
    if freed < SURVIVOR_ELEMENTS * 4 // 2:
        problems.append(f"the free of a storage of the session after the session gave {freed} bytes back to malloc, "
                        f"not about {SURVIVOR_ELEMENTS * 4}")
    if is_known(lib.storage_generation(torch.empty(1000))):
        problems.append("a storage after the session has a known generation")

    problems += check_replay(lib, prior)
    problems += check_divergence_in_a_parent(lib)

    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print("test_storage_generation: each check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
