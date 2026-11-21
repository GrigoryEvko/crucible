"""Refuse a timed window of a bench whose loop holds no instruction of its body.

THE WINDOW
    A timed window is the code that a path from an rdtsc reaches before it
    gets to an rdtscp.  The edges are the edges of the walk of the rule
    codegen-shapes (assay.guard.engines.elf.codegen_shapes, THE WALK), with
    the part of each jump of a linked binary found by its address
    (successors_of()).  The harness times a loop of calls of the body of a
    row there (bench/bench_harness.h, bench::Run::measure).  A window with no
    loop times no body, such as the measure of the overhead of the clock, and
    the check does not judge it.

THE COUNTER
    A loop is the instructions on a path in the window from the target of a
    loop edge back to the edge.  These instructions of a loop do no work of
    the body:
      - a jump
      - a cmp or a test with no memory operand
      - an inc, a dec, an add, a sub or a lea that writes a register that a
        cmp or a test of the loop reads, from that register and a number
      - a move between two registers or of a number into a register, and the
        xor of a register with itself.  The form of the sink that is not
        const takes its value in a register and gives it back changed, so a
        loop copies a value that the optimizer computed before the loop into
        that register for each call.
    The asm sinks of the harness emit no instruction.  A loop with no other
    instruction is empty: it holds no instruction of the body of its row.  A
    store of a value that the optimizer computed before the loop is work for
    the check, so a body whose sink holds its value in memory with no input
    sink can still pass.  The floor of a cycle of the harness marks some of
    them.

THE REFUSAL
    The check refuses a window in two cases:
      - No loop of the window holds an instruction of its body.
      - A path in the window goes from a loop with work to an empty loop,
        and the two loops compare no common register.  The loop with work is
        then a loop of the body, such as a scan, and it runs before the loop
        of the calls, one time and not one time for each call.
    In each case the optimizer moved the work of the body out of the timed
    loop, because the body read a value that does not change in the loop
    and gave it no input sink.  A window can also hold a version of the
    loop of the calls that the optimizer split off, and that version holds
    only the counter.  For a count of zero, such as a sweep of no slot, no
    path goes from the loop with work to it.  For a test of a value that does
    not change, such as a ref that is not open, the loop with work goes to
    it after the test, and the two versions compare the count of the calls
    in one register.  Neither case is a refusal.

THE PROBE
    The mode probe reads the object of test/bench_windows/probe_windows.cpp.
    Its row run_kept_row reads a value through an input sink, and its row
    run_lost_row reads the same value with no input sink.  The check must
    refuse the windows of the body of the lost row, accept each window of
    the kept row, and find a memory read in the loop of each such window of
    the kept row.  The probe is the control of the check: the test fails
    when the check accepts the lost row.

Usage:
    check_windows.py probe ROOT OBJECT
    check_windows.py scan ROOT BINARY...

ROOT is the root of the tree, which holds utils/assay.  The script only
reads.  Exit 0 for a pass, 1 for a refusal or a failed control, and 77 on a
host that is not x86-64.

Complexity: one disassembly of each file, and for each window a walk that is
linear in the edges of its function, for each loop edge of the window.
"""

from __future__ import annotations

import platform
import sys
from dataclasses import dataclass
from pathlib import Path
from types import ModuleType
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from assay.guard.engines.elf.codegen_shapes import Code, Instruction, Part

# The modules of utils/assay, which load_readers() imports from the root of the tree.
shapes: ModuleType
objdump: ModuleType

# The mnemonics of a counter update: one register written from itself and a number.
COUNTER_UPDATES = frozenset({"inc", "dec", "add", "sub", "lea"})
FLAG_TESTS = frozenset({"cmp", "test"})
# The mnemonics of a move, which do no work between registers or from a number (THE COUNTER).
MOVES = frozenset(
    {
        "mov",
        "movabs",
        "movq",
        "movd",
        "movaps",
        "movups",
        "movdqa",
        "movdqu",
        "vmovq",
        "vmovd",
        "vmovaps",
        "vmovups",
        "vmovapd",
        "vmovupd",
        "vmovdqa",
        "vmovdqu",
        "vmovdqa32",
        "vmovdqa64",
        "vmovdqu8",
        "vmovdqu16",
        "vmovdqu32",
        "vmovdqu64",
    }
)
ZEROING = frozenset({"xor", "pxor", "vpxor", "vpxord", "vpxorq", "xorps", "vxorps", "xorpd", "vxorpd"})
START = "rdtsc"
END = "rdtscp"
FRAME_BASES = frozenset({"rbp", "rsp"})
KEPT_ROW = "run_kept_row"
LOST_ROW = "run_lost_row"
SKIP = 77

# The position of one instruction: the index of its part, and its index in the part.
Place = tuple[int, int]
Edges = dict[Place, tuple[Place, ...]]


@dataclass(frozen=True, slots=True)
class Flow:
    """The instructions of the parts of one function with no padding, and the edges between their positions."""

    parts: tuple[tuple[Instruction, ...], ...]
    successors: Edges


@dataclass(frozen=True, slots=True)
class Loop:
    """One loop of a window: its instructions, the instructions of its body, and the registers that it compares."""

    instructions: tuple[str, ...]
    work: tuple[str, ...]
    reads: bool
    compared: frozenset[str]


@dataclass(frozen=True, slots=True)
class Window:
    """One timed window of a function: its number, its loops, and True when a loop with work goes to an empty loop."""

    function: str
    number: int
    loops: tuple[Loop, ...]
    is_work_before_counter: bool

    @property
    def is_refused(self) -> bool:
        """True when the window gives a case of THE REFUSAL."""
        return all(not loop.work for loop in self.loops) or self.is_work_before_counter


def load_readers(root: Path) -> None:
    """Import the walk of codegen-shapes and the reader of objdump from utils/assay of the tree."""
    global shapes, objdump
    sys.path.insert(0, str(root / "utils"))
    from assay.guard.engines.elf import codegen_shapes as walk_module
    from assay.readers import objdump as reader_module

    shapes, objdump = walk_module, reader_module


def family(name: str) -> str:
    """Return the 64-bit register that a register name of any width names, or the name itself."""
    for letter in "abcd":
        if name in (f"r{letter}x", f"e{letter}x", f"{letter}x", f"{letter}l", f"{letter}h"):
            return f"r{letter}x"
    for base in ("si", "di", "sp", "bp"):
        if name in (f"r{base}", f"e{base}", base, f"{base}l"):
            return f"r{base}"
    if name.startswith("r") and name[1:].rstrip("dwb").isdigit():
        return name.rstrip("dwb")
    return name


def is_counter(instruction: Instruction, compared: set[str]) -> bool:
    """True when an instruction of a loop does no work of the body (THE COUNTER)."""
    mnemonic = instruction.mnemonic
    operands = instruction.operands
    if instruction.is_jump:
        return True
    if any(isinstance(operand, objdump.Memory) for operand in operands) and mnemonic != "lea":
        return False
    if mnemonic in FLAG_TESTS:
        return True
    is_plain = all(isinstance(operand, objdump.Register | objdump.Immediate) for operand in operands)
    if mnemonic in MOVES and is_plain and not instruction.opaque:
        return True
    if mnemonic in ZEROING and len({operand.spelling() for operand in operands}) == 1 and is_plain:
        return True
    if mnemonic not in COUNTER_UPDATES or not operands or not isinstance(operands[0], objdump.Register):
        return False
    written = family(operands[0].name)
    if written not in compared:
        return False
    if mnemonic == "lea":
        memory = operands[1] if len(operands) > 1 else None
        return (
            isinstance(memory, objdump.Memory)
            and memory.index is None
            and memory.base is not None
            and family(memory.base) == written
        )
    return all(
        isinstance(operand, objdump.Immediate)
        or (isinstance(operand, objdump.Register) and family(operand.name) == written)
        for operand in operands[1:]
    )


def reads_data(instruction: Instruction) -> bool:
    """True when an instruction reads or writes memory that is not the frame, other than with lea."""
    if instruction.mnemonic == "lea":
        return False
    for operand in instruction.operands:
        if not isinstance(operand, objdump.Memory):
            continue
        if operand.segment is not None or operand.base is None or family(operand.base) not in FRAME_BASES:
            return True
    return False


def successors_of(owned: tuple[Part, ...]) -> Flow:
    """Return the edges between the positions of the parts of one function (THE WINDOW).

    The edges are the edges of flow_of() of codegen-shapes, with one difference for a linked binary.  A jump of a
    binary has no relocation, and it names an address.  The cold part of a function can lie before its hot part, so
    the address of a jump into the cold part is in that part, not in the part of the jump.  The walk then finds the
    part that holds the address.  A jump to an address of no part leaves the function.
    Complexity: linear in the instructions, and linear in the parts for each jump.
    """
    sections = {part.section: index for index, part in enumerate(owned)}
    parts = tuple(tuple(part.counted()) for part in owned)
    offsets = [[instruction.offset for instruction in items] for items in parts]
    successors: Edges = {}
    for part, items in enumerate(parts):
        for index, instruction in enumerate(items):
            place = (part, index)
            following: tuple[Place, ...] = ((part, index + 1),) if index + 1 < len(items) else ()
            mnemonic = instruction.mnemonic
            successors[place] = following
            if mnemonic == "ret" or mnemonic in shapes.TRAP_MNEMONICS:
                successors[place] = ()
            elif instruction.is_jump:
                target: Place | None = None
                if instruction.symbol is not None:
                    target, _ = shapes.jump_target(instruction, part, sections, offsets)
                else:
                    address = shapes.target_address(instruction)
                    for holder, held in enumerate(offsets):
                        if address is not None and held and held[0] <= address <= held[-1]:
                            target = shapes.place_at(held, holder, address)
                taken = () if target is None else (target,)
                successors[place] = tuple(dict.fromkeys((*following, *taken))) if mnemonic != "jmp" else taken
            elif mnemonic == "call":
                callee = shapes.callee_of(instruction)
                if shapes.is_noreturn(callee) or not following or items[index + 1].mnemonic in shapes.TRAP_MNEMONICS:
                    successors[place] = ()
    return Flow(parts, successors)


def reachable_inside(edges: Edges | dict[Place, list[Place]], starts: list[Place], inside: set[Place]) -> set[Place]:
    """Return each position of the window on a walk of the edges from the starts that stays in the window.

    The walk of the whole function would leave the window through the loop of the samples of the harness, which goes
    back to the next rdtsc.  Complexity: linear in the edges of the window.
    """
    found = {start for start in starts if start in inside}
    pending = list(found)
    while pending:
        for after in edges.get(pending.pop(), ()):
            if after in inside and after not in found:
                found.add(after)
                pending.append(after)
    return found


def window_places(flow: Flow, start: Place) -> set[Place]:
    """Return the positions that a path from an rdtsc reaches before an rdtscp or the next rdtsc (THE WINDOW)."""
    inside: set[Place] = set()
    pending = list(flow.successors.get(start, ()))
    while pending:
        place = pending.pop()
        if place in inside or flow.parts[place[0]][place[1]].mnemonic in (END, START):
            continue
        inside.add(place)
        pending.extend(flow.successors.get(place, ()))
    return inside


def windows_of(code: Code) -> list[Window]:
    """Return each timed window of one function with a loop (THE WINDOW, THE COUNTER, THE REFUSAL)."""
    flow = successors_of(code.parts())
    predecessors = shapes.predecessors_of(flow.successors)
    found: list[Window] = []
    starts = [
        (part, index)
        for part, items in enumerate(flow.parts)
        for index, instruction in enumerate(items)
        if instruction.mnemonic == START
    ]
    for number, start in enumerate(starts):
        inside = window_places(flow, start)
        loops: list[Loop] = []
        bodies: list[set[Place]] = []
        for place in sorted(inside):
            if not flow.parts[place[0]][place[1]].is_jump:
                continue
            for target in flow.successors.get(place, ()):
                # A loop edge goes back in the order of the parts and of the addresses, and a path from its target
                # in the window comes back to it.
                if target not in inside or target > place:
                    continue
                forward = reachable_inside(flow.successors, [target], inside)
                if place not in forward:
                    continue
                body = forward & reachable_inside(predecessors, [place], inside)
                members = [flow.parts[item[0]][item[1]] for item in sorted(body)]
                compared = {
                    family(operand.name)
                    for member in members
                    if member.mnemonic in FLAG_TESTS
                    for operand in member.operands
                    if isinstance(operand, objdump.Register)
                }
                work = tuple(member.shown for member in members if not is_counter(member, compared))
                instructions = tuple(member.shown for member in members)
                loops.append(Loop(instructions, work, any(map(reads_data, members)), frozenset(compared)))
                bodies.append(body)
        if not loops:
            continue
        with_work = [(loop, body) for loop, body in zip(loops, bodies, strict=True) if loop.work]
        empty = [(loop, body) for loop, body in zip(loops, bodies, strict=True) if not loop.work]
        is_work_before_counter = any(
            reachable_inside(flow.successors, sorted(working_body), inside) & counter_body
            for working, working_body in with_work
            for counter, counter_body in empty
            if not counter_body <= working_body and not working.compared & counter.compared
        )
        found.append(Window(code.name, number, tuple(loops), is_work_before_counter))
    return found


def timed_windows(path: str) -> list[Window]:
    """Return each timed window with a loop of each function of one file."""
    codes = shapes.codes_of(objdump.functions(path), keeps_arguments=True)
    result: list[Window] = []
    for code in codes.values():
        if any(instruction.mnemonic == START for part in code.parts() for instruction in part.instructions):
            result.extend(windows_of(code))
    return result


def shown_window(window: Window) -> str:
    """Return the text of a refused window: its function, its number and the instructions of each empty loop."""
    lines = [f"  {window.function[:200]}: window {window.number}"]
    if window.is_work_before_counter:
        lines.append("    a loop with work goes to an empty loop, so the work runs one time before the calls")
    for loop in window.loops:
        if not loop.work:
            lines.append("    a loop with no instruction of its body: " + "; ".join(loop.instructions))
    return "\n".join(lines)


def check_probe(path: str) -> int:
    """Return 0 when the kept row holds its read and the check refuses the body of the lost row (THE PROBE)."""
    windows = timed_windows(path)
    kept = [window for window in windows if KEPT_ROW in window.function]
    lost = [window for window in windows if LOST_ROW in window.function]
    # The harness times other loops too, such as a wait for the next tick of the clock, and the check accepts them
    # in the two rows.  The windows of the body are the windows that the check refuses in the lost row.
    body_windows = {window.number for window in lost if window.is_refused}
    problems: list[str] = []
    if not kept or not lost:
        problems.append(f"the probe has {len(kept)} windows of {KEPT_ROW} and {len(lost)} of {LOST_ROW}")
    if not body_windows:
        problems.append(f"the check refuses no window of {LOST_ROW}, so the control shows no refusal")
    for window in kept:
        if window.is_refused:
            problems.append(f"the check refuses the kept row:\n{shown_window(window)}")
        elif window.number in body_windows and not any(loop.reads for loop in window.loops):
            problems.append(f"the loop of the kept row reads no memory:\n{shown_window(window)}")
    kept_numbers = {window.number for window in kept}
    if not body_windows <= kept_numbers:
        problems.append(f"the kept row has no window {sorted(body_windows - kept_numbers)} with a loop")
    for problem in problems:
        print(f"check_windows: {problem}")
    if problems:
        return 1
    print(
        f"check_windows: the probe passes: the check accepts the {len(kept)} windows of {KEPT_ROW}, and it refuses "
        f"the {len(body_windows)} windows of the body of {LOST_ROW}"
    )
    return 0


def check_scan(paths: list[str]) -> int:
    """Return 0 when no window of the binaries gives a case of THE REFUSAL."""
    refused = 0
    judged = 0
    for path in paths:
        windows = timed_windows(path)
        judged += len(windows)
        bad = [window for window in windows if window.is_refused]
        refused += len(bad)
        if bad:
            print(f"check_windows: {Path(path).name}: {len(bad)} windows time a loop with no instruction of its body")
            for window in bad:
                print(shown_window(window))
    print(f"check_windows: {judged} windows of {len(paths)} binaries, {refused} refused")
    if refused:
        print(
            "check_windows: give each input of such a body an input sink, such as bench::mark_changed(object) or "
            "bench::make_opaque(value) of bench/bench_harness.h"
        )
    return 1 if refused else 0


def main(arguments: list[str]) -> int:
    """Run the mode of the arguments."""
    if platform.machine() not in ("x86_64", "AMD64"):
        print("check_windows: the host is not x86-64, so the check does not apply")
        return SKIP
    if len(arguments) < 3 or arguments[0] not in ("probe", "scan"):
        print(__doc__)
        return 1
    load_readers(Path(arguments[1]).resolve())
    if arguments[0] == "probe":
        return check_probe(arguments[2])
    return check_scan(arguments[2:])


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
