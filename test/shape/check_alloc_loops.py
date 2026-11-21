"""Compile the refill and the flush of the allocator, and check the loop of one block of each.

usage: check_alloc_loops.py BUILD_DIR SOURCE_ROOT [--source PATH=COPY]...

THE CLAIMS
    refill  take_slots_ of src/foundation/alloc/Refill.cpp takes one free
            slot of a bitmap word in each turn of its inner loop, with one
            compare for each branch (THE FAST PATHS of
            foundation/alloc/HeapState.h).  -fharden-compares turns a
            compound condition such as "taken < count && bits != 0" into
            set-on-condition values with a second test of each: 26
            instructions and four setcc for each block at x86-64-v3.  So the
            loop of one block holds no setcc and at most REFILL_BLOCK_LIMIT
            instructions.
    flush   give_back_run_ of src/foundation/alloc/Flush.cpp checks the owner
            entry of the chunk and the class of the slab one time for each
            slab, before the loop of the blocks of the slab (THE GIVE-BACK OF
            A RUN there).  So the loop of one block reads one word of memory,
            the block of the run, and calls nothing.  A read of the owner
            entry, of the class or of a record of the slab in that loop is a
            refusal.

THE COMPILE
    Each source compiles with its own command of the compile database of the
    build, into an object in a scratch directory.  The command drops the
    words of the quarantine plugin and of a sanitizer, because the check
    reads the code of the optimizer.  --source PATH=COPY compiles the file
    COPY with the command of PATH, so a control can show that the check
    refuses an older form.  A build at a level below -O2 gives the exit
    SKIPPED.

THE STEADY PATH
    GCC puts the likely path of a loop in one run of the hot part, and each
    unlikely branch out of it.  The steady path of a loop is the run of the
    hot part from the target of a backward jump to that jump.  The loop of one
    block is the shortest such run.  The outer loop of each function holds
    it: that run is the control, so a check cannot pass on code that it does
    not read.  For the flush, the outer loop reads the records of the slab.

THE VERDICT
    Exit 0 when each claim holds.  Exit 1 when one does not hold or a source
    does not compile, 2 on a usage error, 77 for a skip.  Complexity: one
    compile and one disassembly for each source, and a walk linear in the
    instructions of each function.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

REFILL_SOURCE = "src/foundation/alloc/Refill.cpp"
FLUSH_SOURCE = "src/foundation/alloc/Flush.cpp"
REFILL_PART = "take_slots_"
FLUSH_PART = "give_back_run_"
# The loop of one block of the refill: tzcnt, imul, add, the store, inc,
# blsr with its branch, and the compare of the count with its branch, 9
# instructions, and one more for a change of the register allocation.
REFILL_BLOCK_LIMIT = 10
DROPPED_PREFIXES = ("-fplugin", "-fsanitize", "-fno-sanitize", "-DCRUCIBLE_QUARANTINE_ACTIVE")
STEADY_LEVELS = ("-O2", "-O3")
SKIPPED = 77


class Skip(Exception):
    """The build does not compile at a level that the check reads."""


def compile_source(build: Path, source: str, copy: Path | None, obj: Path) -> None:
    """Compile SOURCE, or COPY with the command of SOURCE, into obj.  Raise Skip or RuntimeError."""
    from assay.readers import compdb

    entry = next((item for item in compdb.entries(build) if item.file.endswith("/" + source)), None)
    if entry is None:
        raise RuntimeError(f"the compile database of {build} holds no {source}")
    arguments = list(entry.argv())
    levels = [word for word in arguments if word.startswith("-O")]
    if not levels or levels[-1] not in STEADY_LEVELS:
        raise Skip(f"the build compiles at {levels[-1] if levels else 'no level'}, and the check reads -O2 and -O3")
    words: list[str] = []
    index = 0
    while index < len(arguments):
        word = arguments[index]
        index += 1
        if word == "-o" and index < len(arguments):
            words += [word, str(obj)]
            index += 1
        elif copy is not None and word.endswith("/" + source):
            words.append(str(copy))
        elif not word.startswith(DROPPED_PREFIXES):
            words.append(word)
    done = subprocess.run(words, cwd=entry.directory, capture_output=True, text=True, check=False)
    if done.returncode != 0:
        raise RuntimeError(f"{copy or source} does not compile:\n{done.stderr[-6000:]}")


def loop_runs(flow, include_jmp: bool) -> list[tuple[int, int]]:
    """Return the run (first, last) of the hot part of each loop, shortest first: a backward jump and its target.

    With include_jmp False, only a conditional jump ends a loop (THE STEADY PATH).
    """
    runs: list[tuple[int, int]] = []
    for place, targets in flow.successors.items():
        if place[0] != 0 or not flow.parts[0][place[1]].is_jump:
            continue
        # An unlikely block out of line jumps back with jmp, and its run spans
        # more than its loop, so a loop ends with a conditional jump.
        if flow.parts[0][place[1]].mnemonic == "jmp" and not include_jmp:
            continue
        for target in targets:
            if target[0] == 0 and target[1] <= place[1]:
                runs.append((target[1], place[1]))
    return sorted(set(runs), key=lambda run: run[1] - run[0])


def function_flow(build: Path, source: str, copy: Path | None, part: str):
    """Compile one source and return the flow of the one function whose name holds part."""
    from assay.guard.engines.elf import codegen_shapes as shapes

    with tempfile.TemporaryDirectory(prefix="alloc-loops-") as scratch:
        obj = Path(scratch) / "unit.o"
        compile_source(build, source, copy, obj)
        codes = shapes.codes_of(shapes.disassembled(obj))
    names = [name for name in codes if part in name]
    if len(names) != 1:
        raise RuntimeError(f"the object of {source} holds {len(names)} functions named {part}, and the check reads one")
    return shapes.flow_of(codes[names[0]])


def check_refill(flow) -> list[str]:
    """Return the problems of the loop of one block of take_slots_ (THE CLAIMS, refill)."""
    block_runs = loop_runs(flow, include_jmp=False)
    if len(loop_runs(flow, include_jmp=True)) < 2 or not block_runs:
        return [f"{REFILL_PART} has fewer than two loops, and the check needs the block loop and the word loop"]
    first, last = block_runs[0]
    instructions = [flow.parts[0][index] for index in range(first, last + 1)]
    problems: list[str] = []
    if len(instructions) > REFILL_BLOCK_LIMIT:
        problems.append(
            f"the loop of one block of {REFILL_PART} is {len(instructions)} instructions, and the limit is "
            f"{REFILL_BLOCK_LIMIT}"
        )
    problems += [
        f"the loop of one block of {REFILL_PART} holds a set-on-condition value: {item.shown}"
        for item in instructions
        if item.mnemonic.startswith("set")
    ]
    return problems


def check_flush(flow) -> list[str]:
    """Return the problems of the loop of one block of give_back_run_ (THE CLAIMS, flush)."""
    from check_session_send import reads_memory

    block_runs = loop_runs(flow, include_jmp=False)
    all_runs = loop_runs(flow, include_jmp=True)
    if not block_runs or len(all_runs) < 2:
        return [f"{FLUSH_PART} has {len(all_runs)} loop(s), and the check needs the block loop and the pass loop"]

    def findings(run: tuple[int, int]) -> tuple[int, list[str]]:
        reads = 0
        calls: list[str] = []
        for index in range(run[0], run[1] + 1):
            instruction = flow.parts[0][index]
            callee = flow.callees.get((0, index))
            if callee is not None or instruction.mnemonic == "call":
                calls.append(f"a call of {callee or 'an unknown callee'}: {instruction.shown}")
            if reads_memory(instruction):
                reads += 1
        return reads, calls

    block_reads, block_calls = findings(block_runs[0])
    pass_reads, _ = findings(all_runs[-1])
    problems: list[str] = []
    if block_reads != 1:
        problems.append(
            f"the loop of one block of {FLUSH_PART} reads {block_reads} words of memory, and the claim is "
            f"one: the block of the run"
        )
    problems += [f"the loop of one block of {FLUSH_PART} holds {item}" for item in block_calls]
    if pass_reads <= 1:
        problems.append(
            f"the pass loop of {FLUSH_PART} reads {pass_reads} word(s), so the check does not see the reads of a slab"
        )
    return problems


def main(arguments: list[str]) -> int:
    """Compile the refill and the flush, check the loop of one block of each, and print the verdict."""
    parser = argparse.ArgumentParser(prog="check_alloc_loops.py")
    parser.add_argument("build", type=Path)
    parser.add_argument("root", type=Path)
    parser.add_argument("--source", action="append", default=[], metavar="PATH=COPY")
    try:
        options = parser.parse_args(arguments)
    except SystemExit:
        return 2
    copies: dict[str, Path] = {}
    for item in options.source:
        path, separator, copy = item.partition("=")
        if not separator or path not in (REFILL_SOURCE, FLUSH_SOURCE):
            print(
                f"check_alloc_loops: --source takes {REFILL_SOURCE}=COPY or {FLUSH_SOURCE}=COPY, not {item}",
                file=sys.stderr,
            )
            return 2
        copies[path] = Path(copy).resolve()
    build, root = options.build.resolve(), options.root.resolve()
    sys.path.insert(0, str(root / "utils"))
    sys.path.insert(0, str(root / "test" / "shape"))
    try:
        refill = function_flow(build, REFILL_SOURCE, copies.get(REFILL_SOURCE), REFILL_PART)
        flush = function_flow(build, FLUSH_SOURCE, copies.get(FLUSH_SOURCE), FLUSH_PART)
    except Skip as reason:
        print(f"check_alloc_loops: SKIPPED: {reason}")
        return SKIPPED
    except RuntimeError as problem:
        print(f"check_alloc_loops: {problem}")
        return 1
    problems = check_refill(refill) + check_flush(flush)
    for problem in problems:
        print(f"check_alloc_loops: {problem}")
    if problems:
        return 1
    print(
        f"check_alloc_loops: the loop of one block of {REFILL_PART} holds no setcc and at most {REFILL_BLOCK_LIMIT} "
        f"instructions, and the loop of one block of {FLUSH_PART} reads one word and calls nothing."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
