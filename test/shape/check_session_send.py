"""Compile the probe of a session send, and check the steady loop of a send on a local handle.

usage: check_session_send.py BUILD_DIR SOURCE_ROOT

Probe
    test/shape/session_send.cpp holds five functions: three of the steady
    loops, and two of the count of the steps.  send_on_a_local_handle
    moves a handle into a local, sends each item through it, and moves it
    back.  send_through_a_pin pins the handle of the caller and sends each
    item through the pinned handle (HandleFactory::pin of
    fixy/session/Handle.h).  send_in_caller_memory steps the handle of the
    caller in place.  A step can call the cold note of a holder change
    (fixy/session/Stepping.h), and that call can write each value that the
    caller can reach through a pointer.  So the last loop keeps the handle in
    memory, and the first two loops must not.  A step under the pinned
    policy has no holder check, so the loop of the pin reads no slot of the
    thread either.

Compile
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

Steady path
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

Count of the steps
    A handle outside each loop keeps no count of its steps
    (handle_core::keeps_step_count of fixy/session/Handle.h).
    send_once_outside_a_loop sends one item from the first position of a
    protocol with no loop, and send_once_in_a_loop sends one item through a
    handle in a loop.  Neither function adds one to its item, so an add of one
    in them is the add of the count: an add or a sub of the number 1, an inc
    or a dec, or a lea with the displacement 1.  The check reads each
    instruction of the two functions, in the hot part and in the cold part.

Guard of a self-move
    The move assignment of a handle compares the address of its target with the
    address of its source (fixy/session/Handle.h).  A compare of an address of
    a handle is a cmp or a test of two 64-bit registers, one of which is the
    base of a store in the same instructions: the store writes a field of the
    handle at that address.  Each steady loop of the three forms must hold no
    such compare.  The control move_through_two_names assigns a handle through
    two references, so its hot part must hold one.

Merge of the holder checks
    A step in caller memory runs the holder check of the tracked policy two
    times: in the constructor of the next handle, and in the move of that
    handle into the memory of the caller (fixy/session/Stepping.h).  On the
    fast path the second check sees the values of the first one, and GCC
    merges the two when no store stands between them.  A holder compare is a
    cmp of two 16-bit registers: the slot of a thread has 16 bits
    (fixy/session/Watch.h).  Each steady loop of send_in_caller_memory must
    hold no more holder compares than the steady loop of
    send_on_a_local_handle, which runs one step with one check, and that loop
    must hold at least one, so the check cannot pass on a loop that it does
    not read.

Verdict
    send_on_a_local_handle must have at least one steady loop, and no refusal
    in it.  send_through_a_pin must have at least one steady loop, and no
    refusal and no read of the slot of the thread in it, so the check cannot
    pass on a function that it does not read.  send_in_caller_memory must
    have at least one refusal:
    the control shows that the check sees a handle in memory.
    send_once_outside_a_loop must have no add of one, and send_once_in_a_loop
    must have at least one: the control shows that the check sees the count.
    No steady loop holds a compare of an address of a handle, and
    move_through_two_names holds one: the control shows that the check sees
    the guard.  The steady loop of send_in_caller_memory holds no more holder
    compares than the steady loop of send_on_a_local_handle, which holds at
    least one.  Exit 0 when the seven hold, 1 when one does not hold or the
    probe does not compile, 2 on a usage error, 77 for a skip.  Complexity: one
    compile, one disassembly, and a walk linear in the edges of each loop and
    in the instructions of the functions of the count and the guard.
"""

from __future__ import annotations

import sys
import tempfile
from collections.abc import Mapping, Sequence
from pathlib import Path

REFERENCE_UNIT = "test/shape/core_shapes.cpp"
PROBE_UNIT = "test/shape/session_send.cpp"
LOCAL_FORM = "send_on_a_local_handle"
PIN_FORM = "send_through_a_pin"
CALLER_MEMORY_FORM = "send_in_caller_memory"
OUTSIDE_A_LOOP_FORM = "send_once_outside_a_loop"
IN_A_LOOP_FORM = "send_once_in_a_loop"
GUARD_CONTROL = "move_through_two_names"
# The 64-bit general registers, whose compare can be a compare of two addresses.
ADDRESS_REGISTERS = frozenset(
    {"rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"}
)
# The 16-bit general registers, whose compare can be a compare of the slot of a thread (the merge of the holder
# checks).
SLOT_REGISTERS = frozenset(
    {"ax", "bx", "cx", "dx", "si", "di", "bp", "sp", "r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w"}
)
# The numbers that an add of one and a sub of minus one name, at each width of an operand.
ONE_WORDS = frozenset({1})
MINUS_ONE_WORDS = frozenset({-1, 0xFF, 0xFFFF, 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF})
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


def refusals_of(instruction, callee: str | None, admits_holder_slot: bool) -> list[str]:
    """Return each refusal of one instruction of a steady loop (the steady path).

    A read of the slot of the thread is a refusal too when `admits_holder_slot` is False.
    """
    found: list[str] = []
    if callee is not None or instruction.mnemonic == "call":
        found.append(f"a call of {callee or 'an unknown callee'}")
    if reads_memory(instruction):
        if not reads_holder_slot(instruction):
            found.append("a read of memory")
        elif not admits_holder_slot:
            found.append("a read of the slot of the thread")
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


def findings_of(code, admits_holder_slot: bool = True) -> tuple[int, list[str]]:
    """Return the count of the steady loops of a function, and each refusal in them, with the instruction."""
    from assay.guard.engines.elf import codegen_shapes as shapes

    flow = shapes.flow_of(code)
    loops = steady_loops(code)
    found: list[str] = []
    for body in loops:
        for place in body:
            instruction = flow.parts[0][place[1]]
            for refusal in refusals_of(instruction, flow.callees.get(place), admits_holder_slot):
                found.append(f"{refusal}: {instruction.shown}")
    return len(loops), sorted(set(found))


def adds_of_one(code) -> list[str]:
    """Return each instruction of a function, in each of its parts, that adds one (the count of the steps)."""
    from assay.guard.engines.elf import codegen_shapes as shapes
    from assay.readers import objdump

    found: list[str] = []
    for part in shapes.flow_of(code).parts:
        for instruction in part:
            mnemonic = instruction.mnemonic
            numbers = {operand.value for operand in instruction.operands if isinstance(operand, objdump.Immediate)}
            is_add_of_one = mnemonic.startswith("add") and bool(numbers & ONE_WORDS)
            is_sub_of_minus_one = mnemonic.startswith("sub") and bool(numbers & MINUS_ONE_WORDS)
            is_lea_of_one = mnemonic.startswith("lea") and any(
                isinstance(operand, objdump.Memory) and operand.displacement == 1 for operand in instruction.operands
            )
            if is_add_of_one or is_sub_of_minus_one or is_lea_of_one or mnemonic.startswith("inc"):
                found.append(instruction.shown)
    return found


def address_compares(instructions: Sequence) -> list[str]:
    """Return each compare of an address of a handle in the instructions (the guard of a self-move).

    A compare of an address is a cmp or a test of two 64-bit registers, one of which is the base of a store in the
    same instructions.  Complexity: linear in the instructions.
    """
    from assay.readers import objdump

    bases = {
        item.operands[0].base
        for item in instructions
        if item.mnemonic in STORE_MNEMONICS and item.operands and isinstance(item.operands[0], objdump.Memory)
    }
    found: list[str] = []
    for item in instructions:
        names = [operand.name for operand in item.operands if isinstance(operand, objdump.Register)]
        is_compare = item.mnemonic in ("cmp", "test") and len(names) == 2 == len(item.operands)
        if is_compare and all(name in ADDRESS_REGISTERS for name in names) and any(name in bases for name in names):
            found.append(item.shown)
    return found


def loop_address_compares(code) -> list[str]:
    """Return each compare of an address of a handle in the steady loops of a function."""
    from assay.guard.engines.elf import codegen_shapes as shapes

    flow = shapes.flow_of(code)
    found: list[str] = []
    for body in steady_loops(code):
        found += address_compares([flow.parts[0][place[1]] for place in body])
    return found


def holder_compares(code) -> int:
    """Return the most holder compares that one steady loop of a function holds (the merge of the holder checks).

    A holder compare is a cmp of two 16-bit registers.  Complexity: linear in the instructions of each loop.
    """
    from assay.guard.engines.elf import codegen_shapes as shapes
    from assay.readers import objdump

    flow = shapes.flow_of(code)
    most = 0
    for body in steady_loops(code):
        count = 0
        for place in body:
            item = flow.parts[0][place[1]]
            names = [operand.name for operand in item.operands if isinstance(operand, objdump.Register)]
            if item.mnemonic == "cmp" and len(names) == 2 == len(item.operands) and set(names) <= SLOT_REGISTERS:
                count += 1
        most = max(most, count)
    return most


def hot_address_compares(code) -> list[str]:
    """Return each compare of an address of a handle in the hot part of a function."""
    from assay.guard.engines.elf import codegen_shapes as shapes

    return address_compares(list(shapes.flow_of(code).parts[0]))


def verdict(codes: Mapping[str, object]) -> list[str]:
    """Return the problems of the six functions of the probe (the verdict)."""
    problems: list[str] = []
    for name in (LOCAL_FORM, PIN_FORM, CALLER_MEMORY_FORM, OUTSIDE_A_LOOP_FORM, IN_A_LOOP_FORM, GUARD_CONTROL):
        if name not in codes:
            problems.append(f"the object of {PROBE_UNIT} holds no function {name}")
    if problems:
        return problems
    loops, local = findings_of(codes[LOCAL_FORM])
    if loops == 0:
        problems.append(f"{LOCAL_FORM} has no steady loop, so the check reads nothing")
    problems += [f"{LOCAL_FORM}: the steady loop holds {item}" for item in local]
    pinned_loops, pinned = findings_of(codes[PIN_FORM], admits_holder_slot=False)
    if pinned_loops == 0:
        problems.append(f"{PIN_FORM} has no steady loop, so the check reads nothing")
    problems += [f"{PIN_FORM}: the steady loop holds {item}" for item in pinned]
    _, control = findings_of(codes[CALLER_MEMORY_FORM])
    if not control:
        problems.append(f"{CALLER_MEMORY_FORM} holds no refusal, so the check does not see a handle in memory")
    problems += [f"{OUTSIDE_A_LOOP_FORM}: adds one to a count: {item}" for item in adds_of_one(codes[OUTSIDE_A_LOOP_FORM])]
    if not adds_of_one(codes[IN_A_LOOP_FORM]):
        problems.append(f"{IN_A_LOOP_FORM} adds one to nothing, so the check does not see the count of the steps")
    for name in (LOCAL_FORM, PIN_FORM, CALLER_MEMORY_FORM):
        problems += [f"{name}: the steady loop compares an address of a handle: {item}"
                     for item in loop_address_compares(codes[name])]
    if not hot_address_compares(codes[GUARD_CONTROL]):
        problems.append(f"{GUARD_CONTROL} compares no address of a handle, so the check does not see the guard")
    one_check = holder_compares(codes[LOCAL_FORM])
    in_memory = holder_compares(codes[CALLER_MEMORY_FORM])
    if one_check == 0:
        problems.append(f"{LOCAL_FORM} holds no holder compare, so the check does not see the holder check")
    elif in_memory > one_check:
        problems.append(f"{CALLER_MEMORY_FORM}: a steady loop holds {in_memory} holder compares, and the loop of "
                        f"{LOCAL_FORM} holds {one_check}: the two holder checks of a step are not merged")
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
    pinned_loops, _ = findings_of(codes[PIN_FORM], admits_holder_slot=False)
    _, control = findings_of(codes[CALLER_MEMORY_FORM])
    counted = adds_of_one(codes[IN_A_LOOP_FORM])
    print(f"check_session_send: {LOCAL_FORM} has {loops} steady loop(s) with no call and no read of memory other "
          f"than the slot of the thread.  {PIN_FORM} has {pinned_loops} steady loop(s) with no call and no read "
          f"of memory.  The control {CALLER_MEMORY_FORM} holds {len(control)} refusal(s).  {OUTSIDE_A_LOOP_FORM} "
          f"adds one to no count, and the control {IN_A_LOOP_FORM} adds one {len(counted)} time(s).  No steady loop "
          f"compares an address of a handle, and the control {GUARD_CONTROL} holds "
          f"{len(hot_address_compares(codes[GUARD_CONTROL]))} such compare(s).  A steady loop of "
          f"{CALLER_MEMORY_FORM} holds {holder_compares(codes[CALLER_MEMORY_FORM])} holder compare(s), and the loop "
          f"of {LOCAL_FORM} holds {holder_compares(codes[LOCAL_FORM])}.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
