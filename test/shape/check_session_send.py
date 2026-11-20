"""Compile the probe of a session send, and check the steady loop of a send on a local handle.

usage: check_session_send.py BUILD_DIR SOURCE_ROOT

THE PROBE
    test/shape/session_send.cpp holds two functions.  send_on_a_local_handle
    moves a handle into a local, sends each item through it, and moves it
    back.  send_in_caller_memory steps the handle of the caller in place.  A
    step can call the cold note of a holder change (fixy/session/Stepping.h),
    and that call can write each value that the caller can reach through a
    pointer.  So the second loop keeps the handle in memory, and the first
    loop must not.

THE COMPILE
    The compile is the compile of REFERENCE_UNIT in the compile database of
    the build, with the probe as its source and an object in a scratch
    directory.  The probe then gets the level, the target and the hardening
    flags of the units of the foundation library.  The compile drops the
    words of the quarantine plugin and of a sanitizer: the check reads the
    code of the optimizer, and a sanitizer adds a test to each memory access.
    It compiles the probe as a unit of a program (-fPIE in place of -fPIC), as
    a test or a bench compiles a session loop, so the read of a thread-local
    value is one operand of the segment fs with the relocation of its name.
    A build at a level below -O2 gives the exit SKIPPED, and so does a build
    with no REFERENCE_UNIT (a host that is not x86-64).

THE STEADY PATH
    The walk of the rule codegen-shapes (assay.guard.engines.elf.codegen_shapes)
    reads the hot part and the cold part of each function.  The steady path
    of a loop is the loop of the hot part, when no branch goes into the cold
    part: the instructions on a path of the hot part from the target of a
    backward jump back to that jump.  The check finds each such loop of a
    function, and in each loop it refuses:
      - a call, or a jump to a different function
      - a read of memory: an operand in memory that the instruction reads.
        A mov whose destination is in memory is a store, and no read.
    A read of memory is a load of a field of the handle, of the item, or of
    the reference of the caller.  The one read that the check admits is the
    read of the slot of the thread (HOLDER_SLOT) by the holder check of the
    step.  The holder compare stays in the steady loop, in registers, and the
    compiler can also keep the read of the slot there: the cold note joins
    the loop again and can change the slot, so the compiler does not split
    the compare out of the loop.

THE VERDICT
    send_on_a_local_handle must have at least one steady loop, and no refusal
    in it.  send_in_caller_memory must have at least one refusal: the control
    shows that the check sees a handle in memory.  Exit 0 when the two hold,
    1 when one does not hold or the probe does not compile, 2 on a usage
    error, 77 for a skip.  Complexity: one compile, one disassembly, and a
    walk linear in the edges of each loop.
"""

from __future__ import annotations

import sys
import tempfile
from collections.abc import Mapping, Sequence
from pathlib import Path

REFERENCE_UNIT = "test/shape/core_shapes.cpp"
PROBE_UNIT = "test/shape/session_send.cpp"
LOCAL_FORM = "send_on_a_local_handle"
CALLER_MEMORY_FORM = "send_in_caller_memory"
DROPPED_PREFIXES = ("-fplugin", "-fsanitize", "-fno-sanitize", "-DCRUCIBLE_QUARANTINE_ACTIVE")
# The words of a unit of a library, and the word of a unit of a program that takes their place.
LIBRARY_WORDS = ("-fPIC", "-fpic")
PROGRAM_WORD = "-fPIE"
# The thread-local slot that the holder check of a step reads (fixy/session/Watch.h).
HOLDER_SLOT = "tls_holder_stamp"
STEADY_LEVELS = ("-O2", "-O3")
# The moves that write their last operand and read no memory.  A different
# instruction with an operand in memory reads it, such as a compare or an add.
STORE_MNEMONICS = frozenset(
    {"mov", "movb", "movw", "movl", "movq", "movabs", "movaps", "movups", "movdqa", "movdqu", "vmovaps", "vmovups",
     "vmovdqa", "vmovdqu", "vmovdqa64", "vmovdqu64", "vmovq", "vmovd", "movd", "movnti"}
)
SKIPPED = 77


def compile_probe(build: Path, root: Path, obj: Path) -> str | None:
    """Compile the probe into obj with the command of REFERENCE_UNIT.  Return a reason to skip, or None."""
    from assay.readers import compdb

    entry = next((item for item in compdb.entries(build) if item.file.endswith("/" + REFERENCE_UNIT)), None)
    if entry is None:
        return f"the compile database of {build} holds no {REFERENCE_UNIT}"
    arguments = list(entry.argv())
    levels = [word for word in arguments if word.startswith("-O")]
    if not levels or levels[-1] not in STEADY_LEVELS:
        return f"the build compiles at {levels[-1] if levels else 'no level'}, and the check reads -O2 and -O3"
    words: list[str] = []
    index = 0
    while index < len(arguments):
        word = arguments[index]
        index += 1
        if word == "-o" and index < len(arguments):
            words += [word, str(obj)]
            index += 1
        elif word.endswith(REFERENCE_UNIT):
            words.append(str(root / PROBE_UNIT))
        elif word in LIBRARY_WORDS:
            words.append(PROGRAM_WORD)
        elif not word.startswith(DROPPED_PREFIXES):
            words.append(word)
    import subprocess

    done = subprocess.run(words, cwd=entry.directory, capture_output=True, text=True, check=False)
    if done.returncode != 0:
        raise RuntimeError(f"the probe does not compile:\n{done.stderr[-6000:]}")
    return None


def hot_successors(successors: Mapping[tuple[int, int], tuple[tuple[int, int], ...]]) -> dict:
    """Return the edges of the hot part alone: no edge into the cold part."""
    return {
        place: tuple(target for target in targets if target[0] == 0)
        for place, targets in successors.items()
        if place[0] == 0
    }


def reads_memory(instruction) -> bool:
    """True when the instruction reads an operand in memory.  lea and nop read none.

    The reader gives the operands in the Intel order, so the destination of a mov is the first operand.
    """
    from assay.readers import objdump

    memory = [index for index, operand in enumerate(instruction.operands) if isinstance(operand, objdump.Memory)]
    if not memory or instruction.mnemonic.startswith(("lea", "nop", "prefetch")):
        return False
    is_store = instruction.mnemonic in STORE_MNEMONICS and memory == [0]
    return not is_store


def reads_holder_slot(instruction) -> bool:
    """True when the instruction reads the slot of the thread: an operand of the segment fs at HOLDER_SLOT."""
    from assay.readers import objdump

    is_thread_local = any(
        isinstance(operand, objdump.Memory) and operand.segment == "fs" for operand in instruction.operands
    )
    return is_thread_local and (instruction.name or "").endswith(HOLDER_SLOT)


def refusals_of(instruction, callee: str | None) -> list[str]:
    """Return each refusal of one instruction of a steady loop (THE STEADY PATH)."""
    found: list[str] = []
    if callee is not None or instruction.mnemonic == "call":
        found.append(f"a call of {callee or 'an unknown callee'}")
    if reads_memory(instruction) and not reads_holder_slot(instruction):
        found.append("a read of memory")
    return found


def steady_loops(code) -> list[list[tuple[int, int]]]:
    """Return the places of each loop of the hot part of a function, with no branch into the cold part."""
    from assay.guard.engines.elf import codegen_shapes as shapes

    flow = shapes.flow_of(code)
    successors = hot_successors(flow.successors)
    predecessors = shapes.predecessors_of(successors)
    loops: list[list[tuple[int, int]]] = []
    for place, targets in successors.items():
        instruction = flow.parts[0][place[1]]
        if not instruction.is_jump:
            continue
        for target in targets:
            if target[1] <= place[1] and place in shapes.reachable(successors, [target]):
                body = shapes.reachable(successors, [target]) & shapes.reachable(predecessors, [place])
                loops.append(sorted(body))
    return loops


def findings_of(code) -> tuple[int, list[str]]:
    """Return the count of the steady loops of a function, and each refusal in them, with the instruction."""
    from assay.guard.engines.elf import codegen_shapes as shapes

    flow = shapes.flow_of(code)
    loops = steady_loops(code)
    found: list[str] = []
    for body in loops:
        for place in body:
            instruction = flow.parts[0][place[1]]
            for refusal in refusals_of(instruction, flow.callees.get(place)):
                found.append(f"{refusal}: {instruction.shown}")
    return len(loops), sorted(set(found))


def verdict(codes: Mapping[str, object]) -> list[str]:
    """Return the problems of the two functions of the probe (THE VERDICT)."""
    problems: list[str] = []
    for name in (LOCAL_FORM, CALLER_MEMORY_FORM):
        if name not in codes:
            problems.append(f"the object of {PROBE_UNIT} holds no function {name}")
    if problems:
        return problems
    loops, local = findings_of(codes[LOCAL_FORM])
    if loops == 0:
        problems.append(f"{LOCAL_FORM} has no steady loop, so the check reads nothing")
    problems += [f"{LOCAL_FORM}: the steady loop holds {item}" for item in local]
    _, control = findings_of(codes[CALLER_MEMORY_FORM])
    if not control:
        problems.append(f"{CALLER_MEMORY_FORM} holds no refusal, so the check does not see a handle in memory")
    return problems


def main(arguments: Sequence[str]) -> int:
    """Compile the probe, read it, and print the verdict."""
    if len(arguments) != 2:
        print(__doc__.splitlines()[2], file=sys.stderr)
        return 2
    build, root = Path(arguments[0]).resolve(), Path(arguments[1]).resolve()
    sys.path.insert(0, str(root / "utils"))
    from assay.guard.engines.elf import codegen_shapes as shapes

    with tempfile.TemporaryDirectory(prefix="session-send-") as scratch:
        obj = Path(scratch) / "session_send.o"
        try:
            skip = compile_probe(build, root, obj)
        except RuntimeError as problem:
            print(f"check_session_send: {problem}")
            return 1
        if skip is not None:
            print(f"check_session_send: SKIPPED: {skip}")
            return SKIPPED
        codes = shapes.codes_of(shapes.disassembled(obj))
    problems = verdict(codes)
    for problem in problems:
        print(f"check_session_send: {problem}")
    if problems:
        return 1
    loops, _ = findings_of(codes[LOCAL_FORM])
    _, control = findings_of(codes[CALLER_MEMORY_FORM])
    print(f"check_session_send: {LOCAL_FORM} has {loops} steady loop(s) with no call and no read of memory other "
          f"than the slot of the thread.  The control {CALLER_MEMORY_FORM} holds {len(control)} refusal(s).")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
