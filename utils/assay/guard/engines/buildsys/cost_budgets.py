"""cost-budgets — a compile, a link, an object, a header alone or a target of a build that costs more than its rule.

THE INPUTS
    The compile database of the build names each compile job and its object.
    The code model of the CMake file API names each linked output: an
    executable, a shared library or a module.  A rule reads only the outputs
    that are there.  A target that the build does not make, such as a
    negative fixture, has no output to read.  The kind of the build, the one
    line of BUILD_DIR/build-kind.txt (assay.core.meter), gives the ledger rows
    that apply.  A build with no kind, or with a kind that is not lowercase
    words with hyphens, gives one error of the kind input.

THE RULES OF A STEP
    compile-cpu, compile-instructions, compile-memory, link-time and
    link-memory read the record OUTPUT.cost of each output
    (assay.core.record).  compile-cpu and link-time read the user and system
    CPU time.  compile-instructions reads the user instructions of the
    compiler run, in G (10^9).  compile-memory and link-memory read the peak
    resident memory, in GB of 2^30 bytes.  A record of a ccache hit holds no
    time, and the rule counts the output but does not judge it.  An output
    with no usable record did not come through the build launcher, and it
    gives an error of the kind no-record.  A record is not usable when it has
    a different format or step, or a different number of bytes.  When no
    output has a usable record, the launcher is not wired, and the rule gives
    one error of the kind not-wired.

    The CPU time of a compile increases with the load of the host, and the
    instruction count does not (assay.core.meter, THE INSTRUCTION COUNT).
    When the record of a compile holds a count, compile-instructions holds the
    error level of the compile.  compile-cpu then gives a warning of the kind
    count-holds-error for a time over its error threshold.  A record with no
    count keeps the error of compile-cpu.  compile-instructions does not apply
    to a build in which no record holds a count.

THE RULES OF AN OBJECT
    function-size reads the largest STT_FUNC symbol of each object, in KB.
    object-text reads the sum of the allocated, executable sections of each
    object, in KB.  The rules read the section headers and the symbols
    through assay.readers.elf.  The two rules read each object, also a ccache
    hit, because a hit gives the same machine code as a compile.  An object
    that the reader refuses gives an error of the kind input.

THE RULE header-alone
    The sentinels of test/layer/CMakeLists.txt compile each header of include/
    alone.  For each sentinel, the rule reads its list in the dependency log of
    Ninja (assay.readers.ninja) and adds the bytes of the files after the
    source.  The header is the first file of the list in include/, after each
    symbolic link resolves.  The thresholds come from the held rule of the
    layer of the header: header-alone-foundation, header-alone-fixy or
    header-alone-crucible.  These problems each give an error of the kind
    input: a header of a different layer, a sentinel with no VALID list, and a
    sentinel with no header of include/.  The message also gives the CPU time of the sentinel
    from its record, with no threshold.  The rule does not apply to a build
    with no sentinel object, or to a build that a generator other than Ninja
    made.

THE JUDGE
    assay.core.budget judges each value against the thresholds and the ledger
    rows of the kind of the build.  The kinds of the findings:
      over-threshold     a value over the warning or the error threshold, with
                         no ledger row
      admitted           a value over the error threshold that a row admits: a
                         warning that gives the row and its reason
      `row-not-needed`   a row of an item at or below the error threshold: an
                         error at the row
      `row-unused`       a row of an item that the build does not hold: a
                         warning at the row
      count-holds-error  a compile-cpu time over the error threshold, in a
                         record that holds an instruction count: a warning
      no-record          an output with no usable record
      not-wired          no output with a usable record
      input              an input that the rule cannot read
    The entity of a finding is the item: the output relative to the build
    directory, or the header relative to the source root.  It is empty for
    not-wired and for an input of the full build.

THE LEDGERS
    utils/ledgers/RULE.txt holds one row for each item and build kind, with
    the reason (assay.core.meter, THE REGISTRY AND THE LEDGERS):

        @build/OUTPUT | KIND | VALUE | - | REASON     an output of a step or an object
        HEADER | KIND | VALUE | - | REASON            a header, for header-alone

    `guard write --rule RULE --build-dir DIR [--reason TEXT]` writes the rows
    of the kind of the build again, through the member function written of
    the rule.  It keeps the rows of the other kinds and the rows of a ccache
    hit.  It keeps the reason of each item that keeps its row, and the head
    comment.  A new row takes the reason of --reason, and a ledger with no row
    has no file.

THE RULE compile-first
    cmake/CompileFirst.cmake gives each target of cmake/compile-first.txt one
    more edge in the Ninja graph.  With that edge, Ninja starts its objects
    first.  The rule
    reads the graph, the list, BUILD_DIR/compile-first-targets.txt (each
    target of all that compiles a source) and the records of the objects:
      short-chain     an object of a listed target of all with a chain of less
                      than three edges: an error at the row of the target
      unlisted-long   a target of all with a largest compile over the error
                      threshold, which the list does not name: an error.  On a
                      GitHub runner it is a warning, because the list applies to
                      the build host
      listed-short    a listed target with a largest compile at or below the
                      warning threshold: a warning at its row
      input           an input that the rule cannot read
    The rule judges the records only in a build of the kind of the row `kind`
    of the list, because the instruction count changes with the kind.  The
    entity of a finding is the target.  `guard write --rule compile-first
    --build-dir DIR` writes the list again from the records of a build,
    through compile_first_write(), the member function refresh of the rule.

THE TOTALS
    `guard warnings` (assay.guard.warnings) prints the totals of a build after
    the warnings.  time_totals() gives the time totals total-compile-cpu,
    total-fixture-cpu and total-test-cpu, from the census of
    assay.readers.census and the test records.  build_totals() gives
    link-count, total-link-cpu, build-peak-memory and configure-time, from the
    records of the build launcher.  total_report_totals() and
    total_report_build() print the lines of the two with the thresholds of the
    registry.  A total gives a warning only, because the CPU time increases
    with the load of the shared host.  These rules are held by guard warnings,
    and this module gives no member for them.  The witness of each such rule
    runs the two functions on its plants (assay.guard.engines.buildsys.held).

Complexity: one read of the compile database, the code model and the records of the outputs that a rule reads.
"""

import enum
import os
from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass, field, replace
from functools import partial
from pathlib import Path

from assay.core import budget as cost_budget
from assay.core import ledger as rule_ledger
from assay.core import meter as cost_meter
from assay.core import record as cost_record
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core.paths import REPO_ROOT
from assay.guard.engines.buildsys.runner import BuildContext
from assay.guard.engines.meta import runner as meta_runner
from assay.readers import census as build_census
from assay.readers import cmake as cmake_reader
from assay.readers import compdb
from assay.readers import elf as elf_reader
from assay.readers import ninja as ninja_reader

GIGA = 1e9
KB = 1024
MB = 1024 * 1024
REGISTRY_FILE = "utils/registry.txt"
# The held rule of header-alone for the headers of each layer of include/.  The headers of a base layer go into each
# translation unit of the tree, and their rules are tighter for this reason.
HEADER_ALONE_LAYERS = {
    "foundation": "header-alone-foundation",
    "fixy": "header-alone-fixy",
    "crucible": "header-alone-crucible",
}
HEADER_ALONE_SENTINEL = "/layer_sentinel_"
HEADER_ALONE_CHECK_FILES = Path("test/layer/checks")
LINKED_TYPES = frozenset({"EXECUTABLE", "SHARED_LIBRARY", "MODULE_LIBRARY"})
# The head comment of a ledger that the member function written makes.  ROW is the row of the item of the rule.
LEDGER_COMMENT = (
    "# The ledger of the rule {rule} of utils/registry.txt: the items that can be more than the error threshold\n"
    "# of the rule, one row for each item and build kind.  A row is '{row}'.\n"
    "# KIND is the build kind of BUILD_DIR/build-kind.txt, and VALUE is the value at the last write, in {unit}.\n"
    "# A row applies only to a build of its kind.  `python3 utils/bin/guard explain {rule}` tells the rule, and\n"
    "# `python3 utils/bin/guard write --rule {rule} --build-dir DIR [--reason TEXT]` writes this file.\n"
)
COMPILE_FIRST = "compile-first"
COMPILE_FIRST_LIST = Path("cmake/compile-first.txt")
COMPILE_FIRST_TARGETS = "compile-first-targets.txt"
COMPILE_FIRST_KIND = "kind"
# The chain of the object of a test: the compile and the link.  An object of a listed target has one edge more.
COMPILE_FIRST_CHAIN = 3
COMPILE_FIRST_COMMAND = "python3 utils/bin/guard write --rule compile-first --build-dir BUILD_DIR"
# Each time total, the kind of unit that it adds, and the words that tell what it adds.
TOTAL_TIMES = (
    ("total-compile-cpu", "object", "the last real compile of each object of all"),
    ("total-fixture-cpu", "fixture", "the last real compile of each negative fixture"),
    ("total-test-cpu", "test", "each executable test in its last run"),
)
TOTAL_NO_RULE = ", no measured rule"
# The prefix of each line that `guard warnings` (assay.guard.warnings) writes about the warnings and the totals.
TOTAL_PREFIX = "guard warnings:"


class Quantity(enum.StrEnum):
    """The value of a record that a rule of a step reads (THE RULES OF A STEP)."""

    TIME = "time"
    INSTRUCTIONS = "instructions"
    MEMORY = "memory"


@dataclass(frozen=True, slots=True)
class StepRule:
    """The step of the records that a rule reads, and the value of each record."""

    step: cost_record.Step
    quantity: Quantity


STEP_RULES = {
    "compile-cpu": StepRule(cost_record.COMPILE, Quantity.TIME),
    "compile-instructions": StepRule(cost_record.COMPILE, Quantity.INSTRUCTIONS),
    "compile-memory": StepRule(cost_record.COMPILE, Quantity.MEMORY),
    "link-time": StepRule(cost_record.LINK, Quantity.TIME),
    "link-memory": StepRule(cost_record.LINK, Quantity.MEMORY),
}
OBJECT_RULES = frozenset({"function-size", "object-text"})
HEADER_ALONE = "header-alone"
# The count rule of a time rule.  When a record holds an accurate count, the count rule holds the error level.
COUNT_RULES = {"compile-cpu": "compile-instructions"}


@dataclass(frozen=True, slots=True)
class Output:
    """One output of the build that is there: an object of a compile job, or a linked output."""

    path: Path
    source: Path | None


@dataclass(frozen=True, slots=True)
class Measured:
    """The values of the items of one rule.

    `held` holds each item that the build holds and the rule does not judge,
    such as a ccache hit.  `counted` holds each item with an instruction count
    that holds the error level of its time (COUNT_RULES).
    """

    values: tuple[cost_budget.Measure, ...] = ()
    held: frozenset[str] = frozenset()
    counted: frozenset[str] = frozenset()
    problems: tuple[check_report.Finding, ...] = ()


@dataclass(frozen=True, slots=True)
class ObjectCode:
    """The machine code of one object: the bytes of its executable sections, and its largest function."""

    text_bytes: int
    largest_function: int
    largest_name: str


@dataclass(frozen=True, slots=True)
class Compile:
    """The largest compile of one target: its source and the user instructions of its compiler run."""

    source: str
    instructions: int


@dataclass(frozen=True, slots=True)
class CompileList:
    """cmake/compile-first.txt: its head comment, its kind, and the line of each target name."""

    comment: str
    kind: str
    names: Mapping[str, int] = field(default_factory=dict)


@dataclass(frozen=True, slots=True)
class CompileFirstInputs:
    """The inputs of compile-first: the graph, the list, the targets of all that compile a source, and the thresholds."""

    graph: ninja_reader.Graph
    compile_list: CompileList
    candidates: frozenset[str]
    budget: check_report.Budget
    list_shown: str


def input_error(rule_id: str, path: str, message: str, entity: str = "", line: int = 0) -> check_report.Finding:
    """Return an error of the kind input: an input that the rule cannot read."""
    return check_report.Finding("error", path, line, rule_id, message, "input", entity)


def build_kind(context: BuildContext, rule_id: str) -> tuple[str, check_report.Finding | None]:
    """Return the kind of the build, or an empty kind and the error of a build with no valid kind (THE INPUTS).

    Raises:
        NotApplicable: If the call gives no build directory
    """
    build = context.require_build()
    kind = cost_meter.read_kind(str(build))
    shown = context.shown(build / cost_meter.KIND_FILE)
    if kind is None:
        return "", input_error(
            rule_id,
            shown,
            "the build has no build kind.  cmake/BuildLauncher.cmake writes it at each configure.  Configure the "
            "build again",
        )
    if not cost_meter.is_build_kind(kind):
        return "", input_error(rule_id, shown, f"the kind {kind!r} is not lowercase words with hyphens")
    return kind, None


def threshold_rules(rule_id: str) -> tuple[str, ...]:
    """Return the measured rules that give the thresholds of a rule: the held rules of header-alone, or the rule."""
    return tuple(HEADER_ALONE_LAYERS.values()) if rule_id == HEADER_ALONE else (rule_id,)


def budgets_of(
    context: BuildContext, rule_id: str
) -> tuple[dict[str, check_report.Budget], list[check_report.Finding]]:
    """Return the thresholds of each measured rule that a rule reads, and an error for each one that is missing."""
    budgets: dict[str, check_report.Budget] = {}
    problems: list[check_report.Finding] = []
    for name in threshold_rules(rule_id):
        try:
            budgets[name] = context.budget(name)
        except meta_runner.NotApplicable as problem:
            problems.append(input_error(rule_id, REGISTRY_FILE, f"{problem}.  Add the measured rule"))
    return budgets, problems


def compile_outputs(context: BuildContext, rule_id: str) -> tuple[list[Output], list[check_report.Finding]]:
    """Return the object of each compile job of the build that is there, and an error for each input problem.

    Complexity: linear in the rows of the compile database.
    """
    build = context.require_build()
    shown = context.shown(compdb.database_file(build))
    try:
        entries = context.entries()
    except compdb.CompdbError as problem:
        message = f"the compile database cannot be read ({problem}).  Configure the build directory with a preset first"
        return [], [input_error(rule_id, shown, message)]
    outputs: list[Output] = []
    problems: list[check_report.Finding] = []
    seen: set[Path] = set()
    for entry in entries:
        if entry.output is None:
            problems.append(
                input_error(
                    rule_id,
                    shown,
                    f"the row of {entry.file} has no output field.  CMake 3.20 or a subsequent version writes it",
                )
            )
            continue
        path = Path(entry.output)
        if path in seen or not path.is_file():
            continue
        seen.add(path)
        outputs.append(Output(path, Path(os.path.normpath(entry.file))))
    if not outputs and not problems:
        problems.append(
            input_error(rule_id, shown, "no object of the compile database is there.  Build the tree before the check")
        )
    return outputs, problems


def link_outputs(context: BuildContext, rule_id: str) -> tuple[list[Output], list[check_report.Finding]]:
    """Return each linked output of the build that is there, from the code model, and an error for an input problem.

    Complexity: linear in the targets of the code model.
    """
    shown = context.shown(cmake_reader.api_directory(context.require_build()))
    try:
        model = context.codemodel()
    except cmake_reader.CmakeError as problem:
        return [], [input_error(rule_id, shown, f"the code model of the CMake file API cannot be read: {problem}")]
    outputs: list[Output] = []
    seen: set[Path] = set()
    for target in model.targets:
        if target.type_name not in LINKED_TYPES:
            continue
        for artifact in target.artifacts:
            path = Path(artifact)
            if path not in seen and path.is_file():
                seen.add(path)
                outputs.append(Output(path, None))
    if not outputs:
        message = "no linked output of the build is there.  Build the tree before the check"
        return [], [input_error(rule_id, shown, message)]
    return outputs, []


def step_record(output: Output, step: cost_record.Step) -> cost_record.CostRecord:
    """Return the record of one output, when it can count for a rule of the step.

    Raises:
        cost_record.MissingRecord: If the output has no record
        cost_record.RecordError: If the record is of a different step, gives a different number of bytes, or has no
            cost block
    """
    found = cost_record.read_of(output.path)
    name = cost_record.record_file(output.path).name
    if found.step != step:
        raise cost_record.RecordError(f"the record {name} is not a {step} record of format {cost_record.FORMAT}")
    size = output.path.stat().st_size
    if found.output_bytes != size:
        raise cost_record.RecordError(
            f"the record {name} gives {found.output_bytes} bytes and the output has {size}, so the output did not "
            f"come through the build launcher.  Build it again"
        )
    if not found.is_hit and found.cost is None:
        raise cost_record.RecordError(f"the record {name} holds no cost block")
    return found


def record_problems(
    context: BuildContext, rule_id: str, outputs: Sequence[Output], problems: Sequence[tuple[Output, str]]
) -> list[check_report.Finding]:
    """Return one error for each output with no usable record, or one error when no output has one."""
    build = context.require_build()
    step = STEP_RULES[rule_id].step
    if outputs and len(problems) == len(outputs):
        return [
            check_report.Finding(
                "error",
                context.shown(build),
                0,
                rule_id,
                f"no {step} output of the build has a usable record, so the build launcher is not wired.  "
                f"cmake/BuildLauncher.cmake puts `loop step` in front of each compile and each link.  Configure the "
                f"build again and build it.  The first problem: {problems[0][1]}",
                "not-wired",
            )
        ]
    found: list[check_report.Finding] = []
    for output, problem in problems:
        item = build_census.shown(output.path, build)
        found.append(
            check_report.Finding(
                "error",
                context.shown(output.source or output.path),
                0,
                rule_id,
                f"the output {item}: {problem}",
                "no-record",
                item,
            )
        )
    return found


def step_values(context: BuildContext, rule_id: str, outputs: Sequence[Output]) -> Measured:
    """Read the time, the instructions or the peak memory of each step from its record (THE RULES OF A STEP).

    Complexity: one read of the record of each output.

    Raises:
        NotApplicable: If the rule reads instructions and no record that holds a cost block holds a count
    """
    build = context.require_build()
    spec = STEP_RULES[rule_id]
    noun = "compile job" if spec.step == cost_record.COMPILE else "link"
    values: list[cost_budget.Measure] = []
    held: set[str] = set()
    counted: set[str] = set()
    uncounted = 0
    problems: list[tuple[Output, str]] = []
    for output in outputs:
        item = build_census.shown(output.path, build)
        try:
            found = step_record(output, spec.step)
        except cost_record.MissingRecord:
            problems.append((output, "it has no record, so it did not come through the build launcher"))
            continue
        except cost_record.RecordError as problem:
            problems.append((output, str(problem)))
            continue
        if found.is_hit or found.cost is None:
            held.add(item)
            continue
        cost = found.cost
        count = cost.compiler_instructions
        if spec.quantity == Quantity.INSTRUCTIONS:
            if count is None:
                uncounted += 1
                continue
            value = count / GIGA
            said = (
                f"the {noun} of {item} ran {value:.1f} G user instructions ({cost.cpu_s:.1f} s CPU at a load of "
                f"{cost.load:.0f})"
            )
        elif spec.quantity == Quantity.TIME:
            value = cost.cpu_s
            said = f"the {noun} of {item} took {value:.1f} s CPU ({cost.wall_s:.1f} s wall)"
            if count is not None and rule_id in COUNT_RULES:
                counted.add(item)
        else:
            value = cost.peak_rss_kb / cost_meter.KB_PER_GB
            said = f"the {noun} of {item} took {value:.2f} GB of peak memory"
        values.append(cost_budget.Measure(item, context.shown(output.source or output.path), value, said))
    if spec.quantity == Quantity.INSTRUCTIONS and not values and uncounted:
        raise meta_runner.NotApplicable(
            f"no record of the {uncounted} compile jobs that ran holds an instruction count, because the host gives no "
            f"exact counter (utils/assay/core/meter.py, THE INSTRUCTION COUNT).  compile-cpu keeps the error level"
        )
    return Measured(
        tuple(values), frozenset(held), frozenset(counted), tuple(record_problems(context, rule_id, outputs, problems))
    )


def object_code(path: Path) -> ObjectCode:
    """Read the bytes of the executable sections and the largest function of one object.

    Complexity: linear in the sections and the symbols of the object.

    Raises:
        elf_reader.ElfError: If the ELF reader refuses the file
        OSError: If the file cannot be read
    """
    with elf_reader.mapped(path) as member:
        functions = [symbol for symbol in member.symbols() if symbol.is_function]
        # max() keeps the first of the symbols with the largest number of bytes, in the sequence of the table.
        largest = max(functions, key=lambda symbol: symbol.size, default=None)
        code_bytes = sum(section.size for section in member.sections() if section.is_code)
    if largest is None or largest.size == 0:
        return ObjectCode(code_bytes, 0, "")
    return ObjectCode(code_bytes, largest.size, largest.name)


def readable_names(names: Sequence[str]) -> tuple[tuple[str, str], ...]:
    """Return each mangled name with its readable name, or with the name itself when c++filt cannot run."""
    unique = sorted(set(names))
    try:
        readable = elf_reader.demangle(unique)
    except elf_reader.ElfError:
        readable = tuple(unique)
    return tuple(zip(unique, readable, strict=True))


def object_values(context: BuildContext, rule_id: str, outputs: Sequence[Output]) -> Measured:
    """Read the machine code of each object for function-size or object-text (THE RULES OF AN OBJECT).

    Complexity: one read of the section headers and the symbols of each object, and one call of c++filt.
    """
    build = context.require_build()
    codes: list[tuple[Output, ObjectCode]] = []
    problems: list[check_report.Finding] = []
    for output in outputs:
        item = build_census.shown(output.path, build)
        try:
            codes.append((output, object_code(output.path)))
        except (OSError, elf_reader.ElfError) as problem:
            problems.append(
                input_error(
                    rule_id,
                    context.shown(output.source or output.path),
                    f"the object {item} cannot be read: {problem}",
                    item,
                )
            )
    names = dict(readable_names([code.largest_name for _, code in codes])) if rule_id == "function-size" else {}
    values: list[cost_budget.Measure] = []
    for output, code in codes:
        item = build_census.shown(output.path, build)
        if rule_id == "function-size":
            value = code.largest_function / KB
            name = names.get(code.largest_name, code.largest_name)
            said = f"the largest function of {item}, {name}, has {value:.1f} KB of machine code"
        else:
            value = code.text_bytes / KB
            said = f"the object {item} has {value:.1f} KB of machine code"
        values.append(cost_budget.Measure(item, context.shown(output.source or output.path), value, said))
    return Measured(tuple(values), problems=tuple(problems))


def sentinel_time(context: BuildContext, output: Output) -> str:
    """Return a clause that tells the CPU time of one sentinel compile from its record, for the message of a header."""
    try:
        found = step_record(output, cost_record.COMPILE)
    except cost_record.RecordError:
        return "its sentinel has no usable record"
    has_check_file = output.source is not None and output.source.is_relative_to(context.root / HEADER_ALONE_CHECK_FILES)
    scope = " with the checks of its check file" if has_check_file else ""
    if not found.is_hit and found.cost is not None:
        return f"its sentinel took {found.cost.cpu_s:.1f} s CPU{scope}"
    if found.last_cost is not None:
        return f"its sentinel was a ccache hit, and the last real compile took {found.last_cost.cpu_s:.1f} s CPU{scope}"
    return "its sentinel was a ccache hit, and its record keeps no time"


def resolved_size(name: str, build: Path, sizes: dict[str, tuple[Path, int]]) -> tuple[Path, int]:
    """Return the resolved path and the bytes of one file of a dependency list, read one time for each name.

    Raises:
        OSError: If the file cannot be read
    """
    if name not in sizes:
        real = Path(os.path.realpath(name if os.path.isabs(name) else build / name))
        sizes[name] = (real, real.stat().st_size)
    return sizes[name]


def header_values(context: BuildContext, rule_id: str, outputs: Sequence[Output]) -> Measured:
    """Measure the bytes that each header includes alone, from the dependency list of its sentinel (THE RULE).

    Complexity: linear in the files of the dependency lists, with one stat for each different file.

    Raises:
        NotApplicable: If the build holds no sentinel object, or a generator other than Ninja made it
    """
    build = context.require_build()
    sentinels = [output for output in outputs if HEADER_ALONE_SENTINEL in output.path.as_posix()]
    if not sentinels:
        raise meta_runner.NotApplicable(
            "the build holds no sentinel object of test/layer, because it made only some targets.  The target all "
            "makes the sentinels"
        )
    keys = [build_census.shown(output.path, build) for output in sentinels]
    try:
        lists = ninja_reader.deps(build, keys)
    except ninja_reader.NotNinja as problem:
        raise meta_runner.NotApplicable(f"the build has no dependency log of Ninja: {problem}") from None
    except ninja_reader.NinjaError as problem:
        return Measured(problems=(input_error(rule_id, context.shown(build), str(problem)),))
    include_root = context.root / "include"
    sizes: dict[str, tuple[Path, int]] = {}
    values: list[cost_budget.Measure] = []
    problems: list[check_report.Finding] = []
    for output, key in zip(sentinels, keys, strict=True):
        source = context.shown(output.source or output.path)
        listed = lists[key]
        if listed.state != ninja_reader.DepsState.VALID or not listed.files:
            why = f": {listed.why}" if listed.why else ""
            problems.append(
                input_error(
                    rule_id,
                    source,
                    f"the dependency log of Ninja holds no VALID list of {key} (state {listed.state}{why}).  Build "
                    f"the object again",
                    key,
                )
            )
            continue
        try:
            entries = [resolved_size(name, build, sizes) for name in listed.files[1:]]
        except OSError as problem:
            problems.append(input_error(rule_id, source, f"a dependency of {key} cannot be read: {problem}", key))
            continue
        header = next((real for real, _ in entries if real.is_relative_to(include_root)), None)
        if header is None:
            problems.append(input_error(rule_id, source, f"the sentinel {key} includes no header of include/", key))
            continue
        header_shown = context.shown(header)
        layer = header.relative_to(include_root).parts[0]
        if layer not in HEADER_ALONE_LAYERS:
            problems.append(
                input_error(
                    rule_id,
                    header_shown,
                    f"the header is in the layer {layer}, which has no measured rule.  HEADER_ALONE_LAYERS names a "
                    f"rule for each layer",
                    header_shown,
                )
            )
            continue
        total = sum(size for _, size in entries)
        said = (
            f"the header alone includes {total / MB:.2f} MB in {len(entries)} files, and "
            f"{sentinel_time(context, output)}"
        )
        values.append(cost_budget.Measure(header_shown, header_shown, total / MB, said, HEADER_ALONE_LAYERS[layer]))
    return Measured(tuple(values), problems=tuple(problems))


def measured_of(context: BuildContext, rule_id: str) -> Measured:
    """Measure each item of a rule, and give the errors of its inputs.

    Raises:
        NotApplicable: If the rule does not apply to the build
    """
    if rule_id in STEP_RULES and STEP_RULES[rule_id].step == cost_record.LINK:
        outputs, problems = link_outputs(context, rule_id)
    else:
        outputs, problems = compile_outputs(context, rule_id)
    if not outputs:
        return Measured(problems=tuple(problems))
    if rule_id in STEP_RULES:
        found = step_values(context, rule_id, outputs)
    elif rule_id == HEADER_ALONE:
        found = header_values(context, rule_id, outputs)
    else:
        found = object_values(context, rule_id, outputs)
    return replace(found, problems=(*problems, *found.problems))


def ledger_shape(rule_id: str) -> str:
    """Return the shape of the item of a ledger row of a rule (THE LEDGERS)."""
    return cost_meter.ITEM_FILE if rule_id == HEADER_ALONE else cost_meter.ITEM_OUTPUT


def judged_values(
    rule_id: str, budgets: Mapping[str, check_report.Budget], measured: Measured, rows: cost_budget.Rows
) -> list[check_report.Finding]:
    """Judge each value against its thresholds and the ledger rows of the kind of the build (THE JUDGE).

    Complexity: linear in the values and the rows.
    """
    found: list[check_report.Finding] = []
    for value in measured.values:
        budget = budgets[value.threshold_rule or rule_id]
        level = check_report.classify(value.value, budget)
        row = rows.admitted.get(value.item)
        if row is None and level == "error" and value.item in measured.counted:
            found.append(
                check_report.Finding(
                    "warning",
                    value.path,
                    0,
                    rule_id,
                    f"{value.said}, over the error threshold {budget.error:g} {budget.unit}.  The record holds an "
                    f"instruction count, so the measured rule {COUNT_RULES[rule_id]} holds the error level of this "
                    f"step, and the CPU time, which increases with the load of the host, gives a warning only",
                    "count-holds-error",
                    value.item,
                )
            )
            continue
        verdict = cost_budget.judge(rule_id, budget, value, rows)
        if verdict is None:
            continue
        kind = "over-threshold" if row is None else "admitted" if level == "error" else "row-not-needed"
        found.append(replace(verdict, kind=kind, entity=value.item))
    held = {value.item for value in measured.values} | measured.held
    for item, row in sorted(rows.admitted.items(), key=lambda pair: pair[1].line):
        single = cost_budget.Rows(rows.shown, rows.kind, {item: row})
        found += [
            replace(unused, kind="row-unused", entity=item) for unused in cost_budget.unused(rule_id, single, held)
        ]
    return found


def budget_findings(rule_id: str, context: BuildContext) -> list[check_report.Finding]:
    """Give the findings of one rule of a step, of an object or of header-alone (THE JUDGE).

    Raises:
        NotApplicable: If the call gives no build directory, or the rule does not apply to the build
    """
    kind, problem = build_kind(context, rule_id)
    if problem is not None:
        return [problem]
    budgets, missing = budgets_of(context, rule_id)
    if missing:
        return missing
    measured = measured_of(context, rule_id)
    rows = ledger_rows(context, rule_id, kind)
    return [*measured.problems, *rows.problems, *judged_values(rule_id, budgets, measured, rows)]


def ledger_rows(context: BuildContext, rule_id: str, kind: str) -> cost_budget.Rows:
    """Return the ledger rows of the kind of the build, with each problem of the ledger as an error of the kind input."""
    rows = cost_budget.read_rows(rule_id, ledger_shape(rule_id), rule_ledger.ledger_path(rule_id, context.root), kind)
    return rows.replaced(problems=tuple(replace(problem, kind="input") for problem in rows.problems))


def budget_written(
    rule_id: str, context: BuildContext, ledger: rule_ledger.Ledger, reason: str | None
) -> rule_ledger.Ledger:
    """Return the ledger of a rule with the rows of the kind of the build written again from the build (THE LEDGERS).

    Raises:
        NotApplicable: If the call gives no build directory, or the rule does not apply to the build
        rule_ledger.LedgerError: If an input or the ledger cannot be read, or a new row has no reason
    """
    kind, problem = build_kind(context, rule_id)
    budgets, missing = budgets_of(context, rule_id)
    measured = measured_of(context, rule_id) if problem is None and not missing else Measured()
    rows = ledger_rows(context, rule_id, kind)
    errors = [
        found for found in (problem, *missing, *measured.problems, *rows.problems) if found and found.level == "error"
    ]
    if errors:
        for found in errors:
            check_report.write_line(found.text(), is_error=True)
        raise rule_ledger.LedgerError(f"{len(errors)} input errors stop the write.  Correct them first")
    shape = ledger_shape(rule_id)
    base = {
        key: entry
        for key, entry in ledger.base.items()
        if key.kind != kind or cost_meter.item_of(key.path, key.entity, shape) in measured.held
    }
    unexplained: list[str] = []
    for value in measured.values:
        if value.value <= budgets[value.threshold_rule or rule_id].error:
            continue
        path, entity = cost_meter.item_cells(value.item, shape)
        key = rule_ledger.Key(path, kind, entity)
        earlier = ledger.base.get(key)
        why = earlier.reason if earlier is not None else reason
        if not why:
            unexplained.append(value.item)
            continue
        base[key] = rule_ledger.Entry(key, round(value.value, 3), why)
    if unexplained:
        raise rule_ledger.LedgerError(
            f"these items are more than the error threshold and have no row.  Give the reason of their rows with "
            f"--reason: {', '.join(unexplained)}"
        )
    is_new = not rule_ledger.ledger_path(rule_id, context.root).is_file()
    units = " or ".join(sorted({budget.unit for budget in budgets.values()}))
    comment = LEDGER_COMMENT.format(rule=rule_id, row=cost_meter.ITEM_ROWS[shape], unit=units)
    return replace(ledger, base=base, comment=comment if is_new else ledger.comment)


def compile_first_target_of(object_path: Path) -> str | None:
    """Return the target of an object, from the directory CMakeFiles/<target>.dir of its path, or None."""
    parts = object_path.parts
    for index in range(len(parts) - 1):
        if parts[index] == "CMakeFiles" and parts[index + 1].endswith(".dir"):
            return parts[index + 1][: -len(".dir")]
    return None


def compile_first_chains(graph: ninja_reader.Graph) -> dict[int, int]:
    """Return the chain of each edge on a path to a default target, as Ninja 1.12 and a subsequent version count it.

    The chain of an edge is the number of edges on the longest path from the
    edge to a default target.  The count includes the edge, and no phony
    edge.  Of the edges that can start, Ninja starts the edge with the
    longest chain first.  Complexity: linear in the edges and their inputs.
    """
    order: list[int] = []
    seen: set[int] = set()
    for target in graph.defaults or ("all",):
        root = graph.producer.get(target)
        if root is None or root in seen:
            continue
        seen.add(root)
        stack: list[tuple[int, int]] = [(root, 0)]
        while stack:
            current, child = stack.pop()
            inputs = graph.edges[current].inputs + graph.edges[current].order_only
            if child < len(inputs):
                stack.append((current, child + 1))
                producer = graph.producer.get(inputs[child])
                if producer is not None and producer not in seen:
                    seen.add(producer)
                    stack.append((producer, 0))
            else:
                order.append(current)
    own = {edge_id: 0 if graph.edges[edge_id].rule == ninja_reader.PHONY_RULE else 1 for edge_id in order}
    chain = dict(own)
    for edge_id in reversed(order):
        edge = graph.edges[edge_id]
        for name in edge.inputs + edge.order_only:
            producer = graph.producer.get(name)
            if producer is not None and producer in chain:
                chain[producer] = max(chain[producer], chain[edge_id] + own[producer])
    return chain


def compile_first_read_list(path: Path, shown: str) -> CompileList:
    """Read cmake/compile-first.txt through the rows grammar: one row `kind KIND`, and one target name on each row.

    Raises:
        rows_grammar.RowsError: If the list does not obey the grammar, has no kind row or two, or a bad name row
        OSError: If the list cannot be read
    """
    text = rows_grammar.decode(path.read_bytes(), shown)
    kind = ""
    names: dict[str, int] = {}
    for row in rows_grammar.parse(text, shown):
        if row.cells:
            raise row.error("a row of the list holds one target name, or `kind KIND`, and no cell")
        if row.words[0] == COMPILE_FIRST_KIND and len(row.words) == 2:
            if kind:
                raise row.error("the list has a second kind row.  Keep one `kind KIND` row")
            kind = row.words[1]
        elif len(row.words) != 1:
            raise row.error(f"the row `{' '.join(row.words)}` is not one target name")
        elif row.words[0] in names:
            raise row.error(f"the target {row.words[0]} has a second row.  Remove one")
        else:
            names[row.words[0]] = row.line
    if not kind:
        raise rows_grammar.RowsError(
            shown,
            0,
            "the list has no `kind KIND` row.  Add the kind of the build of the list, such as `kind x86_64-debug-asan`",
        )
    return CompileList(rows_grammar.head_comment(text), kind, names)


def compile_first_candidates(build: Path, shown: str) -> frozenset[str]:
    """Read the targets of all that compile a source, which cmake/CompileFirst.cmake writes at each configure.

    Raises:
        rows_grammar.RowsError: If a row is not one target name
        OSError: If the file cannot be read
    """
    names: set[str] = set()
    for row in rows_grammar.read(build / COMPILE_FIRST_TARGETS, shown):
        if row.cells or len(row.words) != 1:
            raise row.error("the row is not one target name")
        names.add(row.words[0])
    return frozenset(names)


def compile_first_count(object_path: Path) -> int | None:
    """Return the user instructions of the compiler run of one object, or None when its record holds no count.

    A built object gives the count of its cost block.  A ccache hit gives the
    count of the last compile that ran the compiler.
    """
    try:
        found = cost_record.read_of(object_path)
    except cost_record.RecordError:
        return None
    if found.step != cost_record.COMPILE or found.result not in ("built", "hit"):
        return None
    cost = found.real_cost
    return None if cost is None else cost.compiler_instructions


def compile_first_largest(context: BuildContext) -> dict[str, Compile]:
    """Return the largest compile of each target that has a record with a count.

    Complexity: linear in the rows of the compile database.

    Raises:
        compdb.CompdbError: If the compile database cannot be read
    """
    largest: dict[str, Compile] = {}
    for entry in context.entries():
        if not entry.output:
            continue
        object_path = Path(entry.output)
        target = compile_first_target_of(object_path)
        count = compile_first_count(object_path) if target is not None else None
        if target is None or count is None:
            continue
        if target not in largest or count > largest[target].instructions:
            largest[target] = Compile(context.shown(os.path.normpath(entry.file)), count)
    return largest


def compile_first_short_chains(inputs: CompileFirstInputs) -> list[check_report.Finding]:
    """Give one error for each listed target of all with an object that has a chain of less than three edges.

    Complexity: linear in the edges.
    """
    chain = compile_first_chains(inputs.graph)
    names = inputs.compile_list.names
    shortest: dict[str, tuple[int, str]] = {}
    for edge_id, edge in enumerate(inputs.graph.edges):
        if ninja_reader.edge_kind(edge.rule) != "compile":
            continue
        target = compile_first_target_of(Path(edge.outputs[0]))
        if target is None or target not in names or target not in inputs.candidates:
            continue
        length = chain.get(edge_id, 0)
        if target not in shortest or length < shortest[target][0]:
            shortest[target] = (length, edge.outputs[0])
    return [
        check_report.Finding(
            "error",
            inputs.list_shown,
            names[target],
            COMPILE_FIRST,
            f"the target {target} is on the list, and its object {output} has a chain of {length} edge(s) in "
            f"{ninja_reader.GRAPH_NAME}.  cmake/CompileFirst.cmake gives each object of a listed target a chain of "
            f"{COMPILE_FIRST_CHAIN} edges or more, so Ninja starts the object first.  Configure the build again, and "
            f"make sure that cmake/CompileFirst.cmake is included",
            "short-chain",
            target,
        )
        for target, (length, output) in sorted(shortest.items())
        if length < COMPILE_FIRST_CHAIN
    ]


def compile_first_judged(inputs: CompileFirstInputs, largest: Mapping[str, Compile]) -> list[check_report.Finding]:
    """Compare the largest compile of each target of all with the list (THE RULE compile-first).

    Complexity: O(n log n) in the targets, because of the sort.
    """
    found: list[check_report.Finding] = []
    on_runner = cost_meter.is_github_actions()
    budget, list_shown = inputs.budget, inputs.list_shown
    for target in sorted(inputs.candidates & largest.keys()):
        largest_compile = largest[target]
        giga = largest_compile.instructions / GIGA
        line = inputs.compile_list.names.get(target)
        if line is None and giga > budget.error:
            text = (
                f"the target {target} compiles {largest_compile.source} in {giga:.1f} G instructions, more than the "
                f"error threshold {budget.error:g} G, and the list does not name it.  Ninja then starts the compile "
                f"after the compiles of the other tests.  Write the list again: {COMPILE_FIRST_COMMAND}"
            )
            if on_runner:
                text += (
                    ".  The rule gives a warning on a CI runner (GITHUB_ACTIONS is true), because the list serves "
                    "the build host and a runner can use a different build of the compiler"
                )
            level: check_report.Level = "warning" if on_runner else "error"
            found.append(check_report.Finding(level, list_shown, 0, COMPILE_FIRST, text, "unlisted-long", target))
        elif line is not None and giga <= budget.warn:
            verdict = check_report.judged(
                "warning",
                list_shown,
                line,
                COMPILE_FIRST,
                f"the list names the target {target}, with the largest compile {largest_compile.source} of "
                f"{giga:.1f} G instructions, no more than the warning threshold {budget.warn:g} G.  Write the list "
                f"again: {COMPILE_FIRST_COMMAND}",
            )
            found.append(replace(verdict, kind="listed-short", entity=target))
    return found


def compile_first_inputs(context: BuildContext) -> tuple[CompileFirstInputs | None, list[check_report.Finding]]:
    """Read the graph, the list, the candidates and the thresholds of compile-first, and an error for each problem.

    Raises:
        NotApplicable: If the call gives no build directory, or a generator other than Ninja made the build
    """
    build = context.require_build()
    list_shown = context.shown(context.root / COMPILE_FIRST_LIST)
    try:
        graph = context.graph()
    except ninja_reader.NinjaError as problem:
        graph_shown = context.shown(ninja_reader.graph_file(build))
        return None, [input_error(COMPILE_FIRST, graph_shown, f"the rule cannot read its input: {problem}")]
    budgets, missing = budgets_of(context, COMPILE_FIRST)
    try:
        compile_list = compile_first_read_list(context.root / COMPILE_FIRST_LIST, list_shown)
        candidates = compile_first_candidates(build, context.shown(build / COMPILE_FIRST_TARGETS))
    except (rows_grammar.RowsError, OSError) as problem:
        message = f"the rule cannot read its input: {problem}"
        if isinstance(problem, OSError):
            message += ".  Configure the build directory again, so that cmake/CompileFirst.cmake writes its files"
        return None, [*missing, input_error(COMPILE_FIRST, list_shown, message)]
    if missing:
        return None, missing
    return CompileFirstInputs(graph, compile_list, candidates, budgets[COMPILE_FIRST], list_shown), []


def compile_first_findings(context: BuildContext) -> list[check_report.Finding]:
    """Give the findings of compile-first (THE RULE compile-first).

    Raises:
        NotApplicable: If the call gives no build directory, or a generator other than Ninja made the build
    """
    inputs, problems = compile_first_inputs(context)
    if inputs is None:
        return problems
    found = compile_first_short_chains(inputs)
    build = context.require_build()
    kind = cost_meter.read_kind(str(build))
    graph_shown = context.shown(ninja_reader.graph_file(build))
    largest: dict[str, Compile] = {}
    if kind is None:
        found.append(
            input_error(
                COMPILE_FIRST,
                graph_shown,
                f"the rule cannot read its input: {context.shown(build / cost_meter.KIND_FILE)} is not there, so the "
                f"kind of the build is not known",
            )
        )
    elif kind == inputs.compile_list.kind:
        try:
            largest = compile_first_largest(context)
        except compdb.CompdbError as problem:
            found.append(input_error(COMPILE_FIRST, graph_shown, f"the compile database cannot be read: {problem}"))
    return [*found, *compile_first_judged(inputs, largest)]


def compile_first_write(context: BuildContext) -> str:
    """Write cmake/compile-first.txt again from the records of a build of the kind of the list, and tell the result.

    The write adds each target over the error threshold, and it removes each
    listed target at or below the warning threshold.  It keeps each other
    name, the head comment and the kind.

    Raises:
        NotApplicable: If the build is not a Ninja build of the kind of the list, or no record holds a count
        rule_ledger.LedgerError: If an input cannot be read
    """
    inputs, problems = compile_first_inputs(context)
    if inputs is None:
        raise rule_ledger.LedgerError(".  ".join(found.text() for found in problems))
    build = context.require_build()
    kind = cost_meter.read_kind(str(build))
    if kind is None:
        raise rule_ledger.LedgerError(
            f"{context.shown(build / cost_meter.KIND_FILE)} is not there, so the kind of the build is not known"
        )
    if kind != inputs.compile_list.kind:
        raise meta_runner.NotApplicable(
            f"the build has the kind {kind}, and the list holds the counts of the kind {inputs.compile_list.kind}"
        )
    try:
        largest = compile_first_largest(context)
    except compdb.CompdbError as problem:
        raise rule_ledger.LedgerError(f"the compile database cannot be read: {problem}") from None
    if not largest:
        raise meta_runner.NotApplicable("no compile record of the build holds an instruction count")
    judged = inputs.candidates & largest.keys()
    kept = {
        name
        for name in inputs.compile_list.names
        if name not in judged or largest[name].instructions / GIGA > inputs.budget.warn
    }
    kept |= {target for target in judged if largest[target].instructions / GIGA > inputs.budget.error}
    path = context.root / COMPILE_FIRST_LIST
    rows_grammar.write(
        path,
        inputs.compile_list.comment,
        [rows_grammar.render([COMPILE_FIRST_KIND, inputs.compile_list.kind])],
        [((name,), rows_grammar.render([name])) for name in kept],
    )
    return f"{inputs.list_shown} holds {len(kept)} targets of the kind {inputs.compile_list.kind}"


def total_test_times(build_dir: Path) -> list[float | None]:
    """Return the CPU time of the record of each executable test of a build directory: a program in the directory.

    Complexity: linear in the records of the build directory.

    Raises:
        CtestError: If a record cannot be read
    """
    from assay.readers import ctest as ctest_reader

    build = build_dir.resolve()
    return [
        record.cost.cpu_s
        for record in ctest_reader.costs(build_dir).values()
        if os.path.isabs(record.program) and Path(record.program).resolve().is_relative_to(build)
    ]


def total_time_totals(
    census: build_census.Census,
    test_times: Sequence[float | None],
    budgets: Mapping[str, check_report.Budget],
    position: str,
) -> tuple[list[check_report.Finding], list[str]]:
    """Add the three time totals and compare each one with its measured rule (THE TOTALS).

    Returns:
        A warning for each total over its threshold, and one summary line for each total
    """
    times: dict[str, Sequence[float | None]] = {
        "object": [unit.cost.cpu_s for unit in census.units if unit.kind == "object"],
        "fixture": [unit.cost.cpu_s for unit in census.units if unit.kind == "fixture"],
        "test": test_times,
    }
    findings: list[check_report.Finding] = []
    lines: list[str] = []
    for rule_id, unit_kind, meaning in TOTAL_TIMES:
        known = [value for value in times[unit_kind] if value is not None]
        missing = len(times[unit_kind]) - len(known)
        total = sum(known)
        budget = budgets.get(rule_id)
        threshold = f", warning threshold {budget.warn:g} s" if budget is not None else TOTAL_NO_RULE
        unknown = f", {missing} with no time" if missing else ""
        lines.append(f"{TOTAL_PREFIX} {rule_id} {total:.0f} s CPU in {len(known)} units{unknown}{threshold}")
        if budget is not None and total > budget.warn:
            findings.append(
                check_report.Finding(
                    "warning",
                    position,
                    0,
                    rule_id,
                    f"{meaning} took {total:.0f} s of CPU time together ({len(known)} units{unknown}), over the "
                    f"threshold {budget.warn:g} s of the rule {rule_id}.  On 192 cores the wall time is at least the "
                    f"total divided by 192.  A time total gives a warning only, because the CPU time rises with the "
                    f"load of the host",
                )
            )
    return findings, lines


def total_build_records(build_dir: Path) -> list[cost_record.CostRecord]:
    """Return each record of a compile or a link of the build launcher in a build directory.

    A record that cannot be read, and the record of a test, have no part.
    Complexity: linear in the files of the build directory.
    """
    records: list[cost_record.CostRecord] = []
    for path in sorted(build_dir.rglob(f"*{cost_record.SUFFIX}")):
        if path.name.endswith(cost_record.TEST_SUFFIX):
            continue
        try:
            records.append(cost_record.read(path))
        except cost_record.RecordError:
            continue
    return records


def total_build_totals(
    records: Sequence[cost_record.CostRecord],
    configure: Mapping[str, object] | None,
    budgets: Mapping[str, check_report.Budget],
    position: str,
) -> tuple[list[check_report.Finding], list[str]]:
    """Return the warnings and the summary lines of the four build totals (THE TOTALS).

    Args:
        records: The records of the compiles and the links of the build directory
        configure: The times of the last configure (assay.loop.history.configure_time()), or None
        budgets: The thresholds of the measured rules
        position: The path that a finding names
    """
    from assay.loop import history as loop_history

    links = [record for record in records if record.step == cost_record.LINK]
    link_cpu = round(sum(record.cost.cpu_s for record in links if record.cost is not None), 1)
    peak = loop_history.peak_concurrent_gb([record.cost for record in records if record.cost is not None])
    configured = loop_history.number(configure, "total_s") if configure is not None else None
    detail = (
        ""
        if configure is None
        else (
            f" (configure {configure['configure_s']} s, generate {configure['generate_s']} s, reply of the file API "
            f"{configure.get('file_api_s', 0)} s, at {configure['start']})"
        )
    )
    values = (
        ("link-count", float(len(links)), "links", "the link records of the build directory", ""),
        ("total-link-cpu", link_cpu, "s", "the link records of the build directory together", ""),
        ("build-peak-memory", peak, "GB", "the compiles and links whose wall times overlap", ""),
        ("configure-time", configured, "s", "the last configure of the build directory", detail),
    )
    findings: list[check_report.Finding] = []
    lines: list[str] = []
    for rule_id, value, unit, what, extra in values:
        budget = budgets.get(rule_id)
        threshold = f", warning threshold {budget.warn:g} {unit}" if budget is not None else TOTAL_NO_RULE
        shown = "no value" if value is None else f"{value:g} {unit}"
        lines.append(f"{TOTAL_PREFIX} {rule_id} {shown}{extra}{threshold}")
        findings += loop_history.warning_over(dict(budgets), rule_id, value, unit, what, position)
    return findings, lines


def build_totals(
    build_dir: Path, root: Path, budgets: Mapping[str, check_report.Budget]
) -> tuple[list[check_report.Finding], list[str]]:
    """Return the warnings and the summary lines of the build totals of a build directory (THE TOTALS).

    Complexity: linear in the files of the build directory.
    """
    from assay.loop import history as loop_history

    return total_build_totals(
        total_build_records(build_dir),
        loop_history.configure_time(build_dir),
        budgets,
        build_census.shown(build_dir, root),
    )


def time_totals(
    build_dir: Path, root: Path, budgets: Mapping[str, check_report.Budget]
) -> tuple[list[check_report.Finding], list[str]]:
    """Return the warnings and the summary lines of the time totals of a build directory (THE TOTALS).

    When the census or the test records of the build cannot be read, the function gives no warning, and one line
    that tells why.
    Complexity: one read of the census and of the test records of the build directory.
    """
    from assay.readers import ctest as ctest_reader

    try:
        census = build_census.read_census(build_dir, root, read_files=False)
        test_times = total_test_times(build_dir)
    except (
        build_census.NotApplicable,
        build_census.CensusError,
        ctest_reader.CtestError,
        OSError,
        ValueError,
    ) as reason:
        return [], [f"{TOTAL_PREFIX} no time totals: {reason}"]
    return total_time_totals(census, test_times, budgets, build_census.shown(build_dir, root))


type Totals = Callable[[Path, Path, Mapping[str, check_report.Budget]], tuple[list[check_report.Finding], list[str]]]


def written_totals(name: str, totals: Totals, build_dir: Path, root: Path) -> list[check_report.Finding]:
    """Write the summary lines of one group of totals of a build directory, and return their warnings.

    `name` names the group in the line of a registry that does not read.
    """
    try:
        budgets = check_report.read_budgets()
    except (OSError, ValueError) as reason:
        check_report.write_line(f"{TOTAL_PREFIX} no {name}: {reason}")
        return []
    findings, lines = totals(build_dir, root, budgets)
    for line in lines:
        check_report.write_line(line)
    return findings


def total_report_build(build_dir: Path, root: Path = REPO_ROOT) -> list[check_report.Finding]:
    """Write the summary lines of the build totals of a build directory, and return their warnings."""
    return written_totals("build totals", build_totals, build_dir, root)


def total_report_totals(build_dir: Path, root: Path = REPO_ROOT) -> list[check_report.Finding]:
    """Write the summary lines of the time totals of a build directory, and return their warnings."""
    return written_totals("time totals", time_totals, build_dir, root)


def budget_member(rule_id: str) -> meta_runner.Member:
    """Return the member of a rule of the policy max: its findings and the write of its ledger."""
    return meta_runner.Member(partial(budget_findings, rule_id), partial(budget_written, rule_id))


RULES = {
    "compile-cpu": budget_member("compile-cpu"),
    "compile-instructions": budget_member("compile-instructions"),
    "compile-memory": budget_member("compile-memory"),
    "link-time": budget_member("link-time"),
    "link-memory": budget_member("link-memory"),
    "function-size": budget_member("function-size"),
    "object-text": budget_member("object-text"),
    "header-alone": budget_member("header-alone"),
    "compile-first": meta_runner.Member(compile_first_findings, refresh=compile_first_write),
}
