"""Refuse a timed window of a bench whose loop holds no instruction of its body, or that runs cold code.

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

THE PLACEMENT
    GCC compiles a function that it takes for unlikely executed as cold code:
    it optimizes the function for size, puts it in a section .text.unlikely,
    and does not split it into a hot part and a cold part.  A timed body there
    calls code that the optimizer did not inline, and a row measures that code
    and not the code of the hot path.  This occurs when each path to a row
    calls a cold function first, such as a print of a body of only a cold
    call (THE PRINT DOORS of bench/bench_harness.h).  So the check also
    refuses a window in these cases:
      - The window starts in cold code: the cold part of a function, a
        section .text.unlikely, or a start of its part at an address that is
        not a multiple of 16.
      - A loop of the window in hot code calls a closure of the bench unit (a
        lambda) whose start is in cold code, such as a timed body that GCC
        did not inline.  A call of a slow path, such as the growth of a list
        or a fatal report, goes to cold code by design, and the check does
        not judge it.
    The check judges the windows of the harness and of the bench unit.  A
    window of a function of the product or the base (a qualified name in the
    namespace crucible, fixy, foundation or std) is a read of the clock in
    that code, such as in a cold path of the pipeline, and no row times it.
    A linked binary keeps no section .text.unlikely, so the check reads the
    start address there.  GCC aligns the start of each function that it
    optimizes for speed to 16 bytes, and it does not align a function that
    it optimizes for size.  A cold function starts at a multiple of 16 for
    one in 16 functions, so the scan can miss one cold function.  A cold row
    makes each function of the row cold, so the scan finds it.

THE PROBE
    The mode probe reads the object of test/bench_windows/probe_windows.cpp.
    Its row run_kept_row reads a value through an input sink, and its row
    run_lost_row reads the same value with no input sink.  The check must
    refuse the windows of the body of the lost row, accept each window of
    the kept row, and find in the kept row one window whose loop reads
    memory for each window of the body of the lost row.  The two rows can
    hold a different count of windows of the harness, so the check does not
    match the windows by their number.  The probe is the control of the
    check: the test fails when the check accepts the lost row.  Its rows
    run_state_row and run_with_state_row do one body on one state, through
    run and through run_with.  The body loops of their windows are compared
    with each register as its class and its width: the loop through run_with
    must add no instruction to the loop through run, so a row through
    run_with times no more code than through run.  Its row run_after_print_row
    calls bench::print before the row, and each of its windows must be in hot
    code.  Its row run_after_cold_door_row calls a door of the cold form
    before the row: a function of only a call of report(), which GCC takes
    for unlikely executed.  The check must find a window of that row in cold
    code, so the control fails when the check of THE PLACEMENT sees nothing.

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
from collections import Counter
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
STATE_ROW = "run_state_row"
WITH_ROW = "run_with_state_row"
PRINT_ROW = "run_after_print_row"
COLD_DOOR_ROW = "run_after_cold_door_row"
SKIP = 77
# THE PLACEMENT: the alignment of the start of a function that GCC optimizes for speed, and the section of cold code.
HOT_ALIGNMENT = 16
COLD_SECTION = ".text.unlikely"
# The namespaces of the product and the base, whose windows THE PLACEMENT does not judge, and the marks after the
# parameter list of a demangled name.
CODE_NAMESPACES = ("crucible::", "fixy::", "foundation::", "std::")
NAME_TRAILERS = (" const", " volatile", " &&", " &", " noexcept")
CLONE_MARK = " [clone "
LAMBDA_MARK = "{lambda"

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
    shape: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class Window:
    """One timed window of a function: its number, its loops, its placement and the cold callees of its loops.

    `is_work_before_counter` is True when a loop with work goes to an empty loop.  `cold_start` is the reason that the
    window starts in cold code, or None.  `cold_callees` holds each closure of the bench in cold code that a loop of
    the window in hot code calls, with its reason (THE PLACEMENT).
    """

    function: str
    number: int
    loops: tuple[Loop, ...]
    is_work_before_counter: bool
    cold_start: str | None = None
    cold_callees: tuple[str, ...] = ()

    @property
    def is_empty_refusal(self) -> bool:
        """True when the window gives a case of THE REFUSAL."""
        return all(not loop.work for loop in self.loops) or self.is_work_before_counter

    @property
    def is_cold(self) -> bool:
        """True when the window or a function that its loops call is in cold code (THE PLACEMENT)."""
        return self.cold_start is not None or bool(self.cold_callees)

    @property
    def is_refused(self) -> bool:
        """True when the check refuses the window: a case of THE REFUSAL or of THE PLACEMENT."""
        return self.is_empty_refusal or self.is_cold


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


def qualified_name(name: str) -> str:
    """Return the qualified name of a demangled function: no return type, no parameter list and no clone mark.

    Complexity: linear in the length of the name.
    """
    text = name
    while text.endswith("]") and CLONE_MARK in text:
        text = text[: text.rindex(CLONE_MARK)]
    trailer = next((item for item in NAME_TRAILERS if text.endswith(item)), None)
    while trailer is not None:
        text = text.removesuffix(trailer)
        trailer = next((item for item in NAME_TRAILERS if text.endswith(item)), None)
    text = shapes.strip_arguments(text)
    depth = 0
    for index in range(len(text) - 1, -1, -1):
        character = text[index]
        if character in ">)}":
            depth += 1
        elif character in "<({":
            depth -= 1
        elif character == " " and depth == 0:
            return text[index + 1 :]
    return text


def is_bench_code(name: str) -> bool:
    """True when a function is code of the harness or of the bench unit, and not of the product or the base."""
    return not qualified_name(name).startswith(CODE_NAMESPACES)


def is_timed_body(name: str) -> bool:
    """True when a function is a closure of the bench unit, such as the body of a row (THE PLACEMENT)."""
    return is_bench_code(name) and LAMBDA_MARK in name


def cold_reason(part: Part) -> str | None:
    """Return the reason that a part of a function is cold code, or None for a part in hot code (THE PLACEMENT)."""
    if part.section.startswith(COLD_SECTION):
        return f"it is in the section {part.section}"
    if part.symbol.endswith(shapes.COLD_SUFFIX):
        return "it is the cold part of its function"
    if part.instructions and part.instructions[0].offset % HOT_ALIGNMENT:
        return f"its start {part.instructions[0].offset:x} is not a multiple of {HOT_ALIGNMENT}"
    return None


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


def cold_callees_of(owned: tuple[Part, ...], flow: Flow, body: set[Place], functions_at: dict[int, Code]) -> list[str]:
    """Return each closure of the bench in cold code that a call of a loop body in hot code names, with its reason.

    `functions_at` holds the code of each function of a linked binary by the address of its hot part (THE PLACEMENT).
    Complexity: linear in the instructions of the body.
    """
    found: list[str] = []
    for part, index in sorted(body):
        instruction = flow.parts[part][index]
        if instruction.mnemonic != "call" or cold_reason(owned[part]) is not None:
            continue
        callee = functions_at.get(shapes.target_address(instruction) or -1)
        if callee is None or callee.hot is None or not is_timed_body(callee.name):
            continue
        reason = cold_reason(callee.hot)
        if reason is not None:
            found.append(f"{callee.name[:200]}: {reason}")
    return found


def windows_of(code: Code, functions_at: dict[int, Code]) -> list[Window]:
    """Return each timed window of one function with a loop (THE WINDOW, THE COUNTER, THE REFUSAL, THE PLACEMENT).

    `functions_at` holds the code of each function of a linked binary by the address of its hot part, and it is empty
    for an object, whose calls name a relocation and not an address.
    """
    owned = code.parts()
    flow = successors_of(owned)
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
                shape = tuple(
                    member.mnemonic
                    if member.is_jump or member.mnemonic == "call"
                    else f"{member.mnemonic} {shapes.normalized_operands(member.operands)}"
                    for member in members
                )
                loops.append(Loop(instructions, work, any(map(reads_data, members)), frozenset(compared), shape))
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
        is_judged = is_bench_code(code.name)
        cold_callees = dict.fromkeys(
            callee
            for body in (bodies if is_judged else [])
            for callee in cold_callees_of(owned, flow, body, functions_at)
        )
        found.append(
            Window(
                code.name,
                number,
                tuple(loops),
                is_work_before_counter,
                cold_reason(owned[start[0]]) if is_judged else None,
                tuple(cold_callees),
            )
        )
    return found


def timed_windows(path: str, is_linked: bool) -> list[Window]:
    """Return each timed window with a loop of each function of one file.

    `is_linked` is True for a linked binary, whose calls name an address, so the check of THE PLACEMENT reads the
    callees of each loop too.
    """
    codes = shapes.codes_of(objdump.functions(path), keeps_arguments=True)
    functions_at: dict[int, Code] = {}
    if is_linked:
        functions_at = {
            code.hot.instructions[0].offset: code for code in codes.values() if code.hot and code.hot.instructions
        }
    result: list[Window] = []
    for code in codes.values():
        if any(instruction.mnemonic == START for part in code.parts() for instruction in part.instructions):
            result.extend(windows_of(code, functions_at))
    return result


def shown_window(window: Window) -> str:
    """Return the text of a refused window: its function, its number and the instructions of each empty loop."""
    lines = [f"  {window.function[:200]}: window {window.number}"]
    if window.is_work_before_counter:
        lines.append("    a loop with work goes to an empty loop, so the work runs one time before the calls")
    if window.is_empty_refusal:
        for loop in window.loops:
            if not loop.work:
                lines.append("    a loop with no instruction of its body: " + "; ".join(loop.instructions))
    if window.cold_start is not None:
        lines.append(f"    the window starts in cold code: {window.cold_start}")
    for callee in window.cold_callees:
        lines.append(f"    a loop of the window calls a function in cold code: {callee}")
    return "\n".join(lines)


def body_shapes(windows: list[Window]) -> list[tuple[str, ...]]:
    """Return the shape of each loop of the body of the windows: a loop with work and no call, in a sorted order.

    A loop with a call is a loop of the harness, such as the wait for the next tick of the clock.
    """
    return sorted(loop.shape for window in windows for loop in window.loops if loop.work and "call" not in loop.shape)


def state_row_problems(windows: list[Window]) -> list[str]:
    """Return the problems of the two rows of a state: a missing row, or an instruction that run_with adds.

    A loop through run_with may have fewer instructions than through run: the state is a member of the body of the
    harness, so its address needs no load from a closure.  It must add none (THE PROBE).
    """
    through_run = [window for window in windows if STATE_ROW in window.function and WITH_ROW not in window.function]
    through_with = [window for window in windows if WITH_ROW in window.function]
    if not through_run or not through_with:
        return [f"the probe has {len(through_run)} windows of {STATE_ROW} and {len(through_with)} of {WITH_ROW}"]
    run_shapes, with_shapes = body_shapes(through_run), body_shapes(through_with)
    if not run_shapes or len(run_shapes) != len(with_shapes):
        return [f"{STATE_ROW} has {len(run_shapes)} body loops and {WITH_ROW} has {len(with_shapes)}"]
    problems: list[str] = []
    for run_shape, with_shape in zip(run_shapes, with_shapes, strict=True):
        added = Counter(with_shape) - Counter(run_shape)
        if added:
            problems.append(
                f"the body loop of {WITH_ROW} adds {sorted(added.elements())} to the loop of {STATE_ROW}:\n"
                f"    {STATE_ROW}: {' ; '.join(run_shape)}\n    {WITH_ROW}: {' ; '.join(with_shape)}"
            )
    return problems


def placement_problems(windows: list[Window]) -> list[str]:
    """Return the problems of the two rows of a print (THE PROBE, THE PLACEMENT).

    A problem is a missing row, a window after bench::print in cold code, or a control with no window in cold code.
    """
    printed = [window for window in windows if PRINT_ROW in window.function]
    control = [window for window in windows if COLD_DOOR_ROW in window.function]
    if not printed or not control:
        return [f"the probe has {len(printed)} windows of {PRINT_ROW} and {len(control)} of {COLD_DOOR_ROW}"]
    problems = [
        f"a window after bench::print is in cold code:\n{shown_window(window)}" for window in printed if window.is_cold
    ]
    if not any(window.is_cold for window in control):
        problems.append(f"the check finds no window of {COLD_DOOR_ROW} in cold code, so the control shows no refusal")
    return problems


def check_probe(path: str) -> int:
    """Return 0 when the kept row holds its read and the check refuses the body of the lost row (THE PROBE)."""
    windows = timed_windows(path, is_linked=False)
    kept = [window for window in windows if KEPT_ROW in window.function]
    lost = [window for window in windows if LOST_ROW in window.function]
    # The harness times other loops too, such as a wait for the next tick of the clock, and the check accepts them
    # in the two rows.  The windows of the body are the windows that the check refuses in the lost row.
    body_windows = {window.number for window in lost if window.is_empty_refusal}
    problems: list[str] = []
    if not kept or not lost:
        problems.append(f"the probe has {len(kept)} windows of {KEPT_ROW} and {len(lost)} of {LOST_ROW}")
    if not body_windows:
        problems.append(f"the check refuses no window of {LOST_ROW}, so the control shows no refusal")
    for window in kept:
        if window.is_refused:
            problems.append(f"the check refuses the kept row:\n{shown_window(window)}")
    # The two rows can hold a different count of windows of the harness before their body, so the number of a
    # window of the body differs between them.  The kept row holds one window that reads memory in its loop for
    # each window of the body of the lost row.
    reading = [window for window in kept if any(loop.reads and loop.work for loop in window.loops)]
    if len(reading) < len(body_windows):
        problems.append(
            f"the kept row has {len(reading)} windows whose loop reads memory, and the lost row has "
            f"{len(body_windows)} windows of its body"
        )
    problems.extend(state_row_problems(windows))
    problems.extend(placement_problems(windows))
    for problem in problems:
        print(f"check_windows: {problem}")
    if problems:
        return 1
    print(
        f"check_windows: the probe passes: the check accepts the {len(kept)} windows of {KEPT_ROW}, and it refuses "
        f"the {len(body_windows)} windows of the body of {LOST_ROW}.  The windows of {PRINT_ROW} are in hot code, and "
        f"the check finds the windows of {COLD_DOOR_ROW} in cold code"
    )
    return 0


def check_scan(paths: list[str]) -> int:
    """Return 0 when no window of the binaries gives a case of THE REFUSAL."""
    refused = 0
    judged = 0
    is_empty_seen = False
    is_cold_seen = False
    for path in paths:
        windows = timed_windows(path, is_linked=True)
        judged += len(windows)
        bad = [window for window in windows if window.is_refused]
        refused += len(bad)
        is_empty_seen = is_empty_seen or any(window.is_empty_refusal for window in bad)
        is_cold_seen = is_cold_seen or any(window.is_cold for window in bad)
        if bad:
            print(f"check_windows: {Path(path).name}: {len(bad)} windows refused")
            for window in bad:
                print(shown_window(window))
    print(f"check_windows: {judged} windows of {len(paths)} binaries, {refused} refused")
    if is_empty_seen:
        print(
            "check_windows: give each input of a body with an empty loop an input sink, such as "
            "bench::mark_changed(object) or bench::make_opaque(value) of bench/bench_harness.h"
        )
    if is_cold_seen:
        print(
            "check_windows: a window in cold code follows a cold call on each path to its row.  Print through the "
            "doors of the harness (THE PRINT DOORS of bench/bench_harness.h), and call no report() or other cold "
            "function before the rows"
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
