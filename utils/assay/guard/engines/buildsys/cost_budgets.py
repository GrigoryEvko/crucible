"""cost-budgets — a compile, a link, an object, a header alone or a target of a build that costs more than its rule.

THE INPUTS
    The compile database of the build names each compile job and its object.
    The code model of the CMake file API names each linked output: an
    executable, a shared library or a module.  A rule reads only the outputs
    that are there, and no output of the directory bench of the build
    (assay.core.budget, THE SCOPE).  A target that the build does not make,
    such as a negative fixture, has no output to read.  The ledger rows of a
    rule judge each build (THE LEDGERS).

THE RULES OF A STEP
    step-cost and step-memory read the record OUTPUT.cost of each output of
    the kinds compile and link (assay.core.record).  step-cost reads the user
    instructions of the compiler run of a compile, in G (10^9).  step-memory
    reads the peak resident memory of a compile and of a link, in GB of 2^30
    bytes.  A record of a ccache hit holds no time, and the rule counts the
    output but does not judge it.  An output with no usable record did not
    come through the build launcher, and it gives an error of the kind
    no-record.  A record is not usable when it has a different format or
    step, or a different number of bytes.  When no output of a kind has a
    usable record, the launcher is not wired, and the rule gives one error of
    the kind not-wired for that kind.

    The CPU time of a step increases with the load of the host, and the
    instruction count does not (assay.core.meter, THE INSTRUCTION COUNT).
    No rule judges the CPU time of a step, and step-cost judges the cost of
    a compile.  step-cost does not apply to a build in which no record holds
    a count.

THE KINDS OF A STEP METER
    The kinds test and fixture of a rule of a step have no records in the
    build that a check can judge: a step meter judges each test and each
    fixture while it runs.  `loop test-step` judges the kind test of the two
    rules, and the fixture driver judges the kind fixture of step-cost and
    the kind test of the run of a fixture (assay.loop.teststep, THE MEASURED
    RULES).  A record of an earlier run can be stale, so the module does not
    judge it again.  METER_KINDS gives these kinds.  The witness of a rule of
    a step runs the judge of each step meter on each plant too (judge(), with
    the judges of assay.guard.engines.buildsys.held), and `guard write`
    reads the records of the tests and the fixtures for the rows of these
    kinds (THE LEDGERS).

THE RULE object-size
    object-size reads two values of each object, in KB: the largest STT_FUNC
    symbol, of the kind function, and the sum of the allocated, executable
    sections, of the kind object.  The rule reads the section headers and the
    symbols one time for each object, through assay.readers.elf.  It reads
    each object, also a ccache hit, because a hit gives the same machine code
    as a compile.  An object that the reader refuses gives an error of the
    kind input.

THE RULE header-alone
    The sentinels of test/layer/CMakeLists.txt compile each header of include/
    alone.  For each sentinel, the rule reads its list in the dependency log of
    Ninja (assay.readers.ninja) and adds the bytes of the files after the
    source.  The header is the first file of the list in include/, after each
    symbolic link resolves.  The kind of a header is its layer, the first
    directory under include/, and the rule has one limit for each layer:
    foundation, fixy and crucible.  These problems each give an error of the
    kind input: a header of a layer that the rule has no limit for, a
    sentinel with no VALID list, and a sentinel with no header of include/.
    The message also gives the CPU time of the sentinel
    from its record, with no threshold.  The rule does not apply to a build
    with no sentinel object, or to a build that a generator other than Ninja
    made.

THE KINDS
    A rule has one limit for each kind of item (assay.core.registry).
    RULE_KINDS gives the kinds of each rule with a fixed set of kinds.  A
    kind that the table gives and the registry does not, and a kind that the
    registry gives and the table does not, each give an error of the kind
    input, because the judge of a kind with no limit is off.  The kinds of
    header-alone are the layers of include/, which the registry gives.

THE JUDGE
    assay.core.budget judges each value against the limit of its kind and
    the ledger rows of the rule (assay.core.budget, THE VERDICT and THE
    ROWS).  Each finding is an error.  The kinds of the findings:
      over-limit         a value above the limit, with no ledger row
      risen              a value above the limit and above the kept value of
                         its row with the noise of its unit
      no-record          an output with no usable record
      not-wired          no output with a usable record
      input              an input that the rule cannot read
    The entity of a finding is the item: the output relative to the build
    directory, or the header relative to the source root.  It is empty for
    not-wired and for an input of the full build.  A value that a row keeps
    gives no finding.  A row of an item at or below the limit gives no
    finding, because a ledger fails only on a growth.  A row whose item the
    build does not hold gives no finding either.

THE LEDGERS
    utils/ledgers/RULE.txt holds at most one row for each kind and item, with
    the reason, and each row judges each build (assay.core.meter, THE
    REGISTRY AND THE LEDGERS).  KIND is "-" for a rule with one limit:

        @build/OUTPUT | KIND | VALUE | - | REASON     an output of a step or an object
        HEADER | LAYER | VALUE | - | REASON          a header, for header-alone

    `guard write --rule RULE --build-dir DIR --reason TEXT` writes a row for
    each item of the build above the limit that no row keeps, through the
    member function written of the rule (assay.core.budget, THE WRITE).  It
    removes no row and lowers no kept value.  It gives the ledger the head
    comment LEDGER_COMMENT (assay.core.ledger, THE HEAD COMMENT).  A ledger
    with no row has no file.


Complexity: one read of the compile database, the code model and the records of the outputs that a rule reads.
"""

import enum
import os
from collections.abc import Collection, Mapping, Sequence
from dataclasses import dataclass, replace
from functools import partial
from pathlib import Path

from assay.core import budget as cost_budget
from assay.core import ledger as rule_ledger
from assay.core import meter as cost_meter
from assay.core import record as cost_record
from assay.core import registry as rule_registry
from assay.core import report as check_report
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
HEADER_ALONE_SENTINEL = "/layer_sentinel_"
HEADER_ALONE_CHECK_FILES = Path("test/layer/checks")
LINKED_TYPES = frozenset({"EXECUTABLE", "SHARED_LIBRARY", "MODULE_LIBRARY"})
# The head comment that the member function written gives each ledger.  ROWS tells the rows of the kinds of the rule.
LEDGER_COMMENT = (
    "# The ledger of the rule {rule} of utils/registry.txt: the items that a row keeps over the limit of their kind,\n"
    "# one row for each kind and item, and each row judges each build.  {rows}.  VALUE is the kept value, in\n"
    "# {unit}.  A row keeps its item up to VALUE with the noise of its unit (THE NOISE of utils/assay/core/budget.py).\n"
    "# `python3 utils/bin/guard explain {rule}` tells the rule, and `python3 utils/bin/guard write --rule {rule}\n"
    "# --build-dir DIR --reason TEXT` writes a new row or a higher VALUE.\n"
)


class Quantity(enum.StrEnum):
    """The value of a record that a rule of a step reads (THE RULES OF A STEP)."""

    INSTRUCTIONS = "instructions"
    MEMORY = "memory"


@dataclass(frozen=True, slots=True)
class StepKind:
    """The step of the records that a kind of a rule of a step reads, and the value of each record."""

    step: cost_record.Step
    quantity: Quantity


STEP_COST = cost_budget.STEP_COST
STEP_MEMORY = cost_budget.STEP_MEMORY
COMPILE_KIND = cost_budget.COMPILE_KIND
LINK_KIND = cost_budget.LINK_KIND
FIXTURE_KIND = cost_budget.FIXTURE_KIND
TEST_KIND = cost_budget.TEST_KIND
# The kinds of each rule of a step that this module judges from the records of the build (THE RULES OF A STEP).
STEP_KINDS: dict[str, dict[str, StepKind]] = {
    STEP_COST: {COMPILE_KIND: StepKind(cost_record.COMPILE, Quantity.INSTRUCTIONS)},
    STEP_MEMORY: {
        COMPILE_KIND: StepKind(cost_record.COMPILE, Quantity.MEMORY),
        LINK_KIND: StepKind(cost_record.LINK, Quantity.MEMORY),
    },
}
# The kinds of each rule of a step that a step meter judges while it runs (THE KINDS OF A STEP METER).
METER_KINDS: dict[str, tuple[str, ...]] = {STEP_COST: (FIXTURE_KIND, TEST_KIND), STEP_MEMORY: (TEST_KIND,)}
OBJECT_SIZE = "object-size"
# The two kinds of object-size: the largest function of an object, and the machine code of the object.
FUNCTION_KIND = "function"
OBJECT_KIND = "object"
HEADER_ALONE = "header-alone"
# The kinds of the items of each rule with a fixed set of kinds (THE KINDS).
RULE_KINDS: dict[str, tuple[str, ...]] = {
    STEP_COST: (COMPILE_KIND, FIXTURE_KIND, TEST_KIND),
    STEP_MEMORY: (COMPILE_KIND, LINK_KIND, TEST_KIND),
    OBJECT_SIZE: (FUNCTION_KIND, OBJECT_KIND),
}
# The form of the item of a ledger row of each kind of a step meter: the name of a test or of a fixture (THE LEDGERS).
NAMED_KINDS = frozenset({FIXTURE_KIND, TEST_KIND})


@dataclass(frozen=True, slots=True)
class Output:
    """One output of the build that is there: an object of a compile job, or a linked output."""

    path: Path
    source: Path | None


@dataclass(frozen=True, slots=True)
class Measured:
    """The values of the items of one rule, and the errors of its inputs."""

    values: tuple[cost_budget.Measure, ...] = ()
    problems: tuple[check_report.Finding, ...] = ()


@dataclass(frozen=True, slots=True)
class ObjectCode:
    """The machine code of one object: the bytes of its executable sections, and its largest function."""

    text_bytes: int
    largest_function: int
    largest_name: str


def input_error(rule_id: str, path: str, message: str, entity: str = "", line: int = 0) -> check_report.Finding:
    """Return an error of the kind input: an input that the rule cannot read."""
    return check_report.Finding("error", path, line, rule_id, message, "input", entity)


def budgets_of(
    context: BuildContext, rule_id: str
) -> tuple[dict[str, check_report.Budget], list[check_report.Finding]]:
    """Return the limit of each kind of a rule, by the kind, and an error for each kind that does not agree (THE KINDS).

    Complexity: linear in the kinds.
    """
    try:
        budgets = context.limits(rule_id)
    except meta_runner.NotApplicable as problem:
        return {}, [input_error(rule_id, REGISTRY_FILE, f"{problem}.  Add the measured rule")]
    wanted = RULE_KINDS.get(rule_id)
    if wanted is None:
        return budgets, []
    problems = [
        input_error(
            rule_id,
            REGISTRY_FILE,
            f"the rule {rule_id} has no limit of the kind {kind or '-'}, so no judge reads that kind.  Give the field "
            f"limit of its row the kinds {', '.join(item or '-' for item in wanted)}",
        )
        for kind in wanted
        if kind not in budgets
    ]
    problems += [
        input_error(
            rule_id,
            REGISTRY_FILE,
            f"the rule {rule_id} has a limit of the kind {kind or '-'}, and no judge reads that kind.  Its kinds are "
            f"{', '.join(item or '-' for item in wanted)}",
        )
        for kind in budgets
        if kind not in wanted
    ]
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
    context: BuildContext,
    rule_id: str,
    step: cost_record.Step,
    outputs: Sequence[Output],
    problems: Sequence[tuple[Output, str]],
) -> list[check_report.Finding]:
    """Return one error for each output of a step with no usable record, or one error when no output has one."""
    build = context.require_build()
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


def step_values(context: BuildContext, rule_id: str, kind: str, outputs: Sequence[Output]) -> Measured:
    """Read the instructions or the peak memory of each step of one kind from its record (THE RULES OF A STEP).

    Complexity: one read of the record of each output.

    Raises:
        NotApplicable: If the kind reads instructions and no record that holds a cost block holds a count
    """
    build = context.require_build()
    spec = STEP_KINDS[rule_id][kind]
    noun = "compile job" if spec.step == cost_record.COMPILE else "link"
    values: list[cost_budget.Measure] = []
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
            continue
        cost = found.cost
        if spec.quantity == Quantity.INSTRUCTIONS:
            if cost.compiler_instructions is None:
                uncounted += 1
                continue
            value = cost.compiler_instructions / GIGA
            said = (
                f"the {noun} of {item} ran {value:.1f} G user instructions ({cost.cpu_s:.1f} s CPU at a load of "
                f"{cost.load:.0f})"
            )
        else:
            value = cost.peak_rss_kb / cost_meter.KB_PER_GB
            said = f"the {noun} of {item} took {value:.2f} GB of peak memory"
        values.append(cost_budget.Measure(item, context.shown(output.source or output.path), value, said, kind))
    if spec.quantity == Quantity.INSTRUCTIONS and not values and uncounted:
        raise meta_runner.NotApplicable(
            f"no record of the {uncounted} compile jobs that ran holds an instruction count, because the host gives no "
            f"exact counter (utils/assay/core/meter.py, THE INSTRUCTION COUNT).  On such a host, no rule judges the CPU "
            f"work of a compile"
        )
    return Measured(tuple(values), tuple(record_problems(context, rule_id, spec.step, outputs, problems)))


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
    """Read the largest function and the machine code of each object, the two kinds of object-size (THE RULE).

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
    names = dict(readable_names([code.largest_name for _, code in codes]))
    values: list[cost_budget.Measure] = []
    for output, code in codes:
        item = build_census.shown(output.path, build)
        place = context.shown(output.source or output.path)
        function = code.largest_function / KB
        name = names.get(code.largest_name, code.largest_name)
        said = f"the largest function of {item}, {name}, has {function:.1f} KB of machine code"
        values.append(cost_budget.Measure(item, place, function, said, FUNCTION_KIND))
        text = code.text_bytes / KB
        said = f"the object {item} has {text:.1f} KB of machine code"
        values.append(cost_budget.Measure(item, place, text, said, OBJECT_KIND))
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
        return f"its sentinel was a ccache hit, and the compile of its object took {found.last_cost.cpu_s:.1f} s CPU{scope}"
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


def header_values(context: BuildContext, rule_id: str, outputs: Sequence[Output], layers: Collection[str]) -> Measured:
    """Measure the bytes that each header includes alone, from the dependency list of its sentinel (THE RULE).

    `layers` gives the layers that the rule has a limit for, the kinds of the rule.  Complexity: linear in the files
    of the dependency lists, with one stat for each different file.

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
        if layer not in layers:
            problems.append(
                input_error(
                    rule_id,
                    header_shown,
                    f"the header is in the layer {layer}, and the rule {rule_id} has no limit of that kind.  Give the "
                    f"field limit of its row a kind for each layer of include/",
                    header_shown,
                )
            )
            continue
        total = sum(size for _, size in entries)
        said = (
            f"the header alone includes {total / MB:.2f} MB in {len(entries)} files, and "
            f"{sentinel_time(context, output)}"
        )
        values.append(cost_budget.Measure(header_shown, header_shown, total / MB, said, layer))
    return Measured(tuple(values), problems=tuple(problems))


def measured_of(context: BuildContext, rule_id: str, kinds: Collection[str]) -> Measured:
    """Measure each item of a rule, and give the errors of its inputs.

    `kinds` gives the kinds of the rule (budgets_of()).  The rule measures no output of the directory bench of the
    build (assay.core.budget, THE SCOPE).

    Raises:
        NotApplicable: If the rule does not apply to the build
    """
    build = context.require_build()
    if rule_id in STEP_KINDS:
        values: list[cost_budget.Measure] = []
        problems: list[check_report.Finding] = []
        for kind, spec in STEP_KINDS[rule_id].items():
            read = link_outputs if spec.step == cost_record.LINK else compile_outputs
            outputs, missing = read(context, rule_id)
            problems += missing
            outputs = judged_outputs(outputs, build)
            if outputs:
                found = step_values(context, rule_id, kind, outputs)
                values += found.values
                problems += found.problems
        return Measured(tuple(values), tuple(problems))
    outputs, missing = compile_outputs(context, rule_id)
    outputs = judged_outputs(outputs, build)
    if not outputs:
        return Measured(problems=tuple(missing))
    if rule_id == HEADER_ALONE:
        found = header_values(context, rule_id, outputs, kinds)
    else:
        found = object_values(context, rule_id, outputs)
    return replace(found, problems=(*missing, *found.problems))


def judged_outputs(outputs: Sequence[Output], build: Path) -> list[Output]:
    """Return the outputs that the measured rules judge: each output outside the directory bench (THE INPUTS)."""
    return [output for output in outputs if cost_budget.is_judged(build_census.shown(output.path, build))]


def ledger_forms(rule_id: str, kinds: Collection[str]) -> dict[str, str]:
    """Return the form of the item of a ledger row of each kind of a rule (THE LEDGERS).

    A header of header-alone is a file of the tree, a test or a fixture is a name, and each other item is an output.
    """
    if rule_id == HEADER_ALONE:
        return dict.fromkeys(kinds, cost_meter.ITEM_FILE)
    return {kind: cost_meter.ITEM_NAME if kind in NAMED_KINDS else cost_meter.ITEM_OUTPUT for kind in kinds}


def rows_text(forms: Mapping[str, str]) -> str:
    """Return the words of the head comment of a ledger that give the shape of a row of each form (LEDGER_COMMENT)."""
    if set(forms) == {""}:
        return f"A row is '{cost_meter.item_row(forms[''], '')}'"
    by_form: dict[str, list[str]] = {}
    for kind, form in forms.items():
        by_form.setdefault(form, []).append(kind)
    shapes = [
        f"'{cost_meter.item_row(form, 'KIND')}' with KIND {' or '.join(kinds)}" for form, kinds in by_form.items()
    ]
    return f"A row is {', or '.join(shapes)}"


def judged_values(
    rule_id: str, budgets: Mapping[str, check_report.Budget], measured: Measured, rows: cost_budget.Rows
) -> list[check_report.Finding]:
    """Judge each value against the limit of its kind and the ledger rows that judge the build (THE JUDGE).

    Complexity: linear in the values.
    """
    found: list[check_report.Finding] = []
    for value in measured.values:
        verdict = cost_budget.judge(rule_id, budgets[value.kind], value, rows)
        if verdict is not None:
            kind = "over-limit" if (value.kind, value.item) not in rows.admitted else "risen"
            found.append(replace(verdict, kind=kind, entity=value.item))
    return found


def budget_findings(rule_id: str, context: BuildContext) -> list[check_report.Finding]:
    """Give the findings of one rule of a step, of an object or of header-alone (THE JUDGE).

    Raises:
        NotApplicable: If the call gives no build directory, or the rule does not apply to the build
    """
    budgets, missing = budgets_of(context, rule_id)
    if missing:
        return missing
    measured = measured_of(context, rule_id, budgets)
    rows = ledger_rows(context, rule_id, budgets)
    return [*measured.problems, *rows.problems, *judged_values(rule_id, budgets, measured, rows)]


def ledger_rows(context: BuildContext, rule_id: str, kinds: Collection[str]) -> cost_budget.Rows:
    """Return the ledger rows of a rule, with each problem of the ledger as an error of the kind input (THE LEDGERS)."""
    path = rule_ledger.ledger_path(rule_id, context.root)
    rows = cost_budget.read_rows(rule_id, ledger_forms(rule_id, kinds), path)
    return rows.replaced(problems=tuple(replace(problem, kind="input") for problem in rows.problems))


def budget_written(
    rule_id: str, context: BuildContext, ledger: rule_ledger.Ledger, reason: str | None
) -> rule_ledger.Ledger:
    """Return the ledger of a rule with a row for each item of the build that no row keeps (THE LEDGERS).

    Raises:
        NotApplicable: If the call gives no build directory, or the rule does not apply to the build
        rule_ledger.LedgerError: If an input or the ledger cannot be read, or a new row has no reason
    """
    budgets, missing = budgets_of(context, rule_id)
    measured = measured_of(context, rule_id, budgets) if not missing else Measured()
    rows = ledger_rows(context, rule_id, budgets)
    errors = [found for found in (*missing, *measured.problems, *rows.problems) if found.level == "error"]
    if errors:
        for found in errors:
            check_report.write_line(found.text(), is_error=True)
        raise rule_ledger.LedgerError(f"{len(errors)} input errors stop the write.  Correct them first")
    forms = ledger_forms(rule_id, budgets)
    measures = [(value.item, value.value, budgets[value.kind]) for value in measured.values]
    if TEST_KIND in METER_KINDS.get(rule_id, ()):
        measures += [(name, value, budgets[TEST_KIND]) for name, value in test_measures(context, rule_id)]
    if FIXTURE_KIND in METER_KINDS.get(rule_id, ()):
        measures += [(name, value, budgets[FIXTURE_KIND]) for name, value in fixture_measures(context)]
    raised = cost_budget.raised_ledger(ledger, forms, measures, reason)
    units = " or ".join(sorted({budget.unit for budget in budgets.values()}))
    comment = LEDGER_COMMENT.format(rule=rule_id, rows=rows_text(forms), unit=units)
    return replace(raised, comment=comment)


def test_measures(context: BuildContext, rule_id: str) -> list[tuple[str, float]]:
    """Return the name and the measure of each registered test of the build, for a row of the kind test.

    step-memory reads the peak memory of the record of the test in GB, and step-cost reads its instruction count in G.
    A record with no count, and a test of the directory bench, give no measure.  Complexity: linear in the tests and
    the records.

    Raises:
        rule_ledger.LedgerError: If the test list or a record does not read
    """
    from assay.loop import teststep
    from assay.readers import ctest as ctest_reader

    build = context.require_build()
    try:
        registered = {test.name for test in ctest_reader.tests(build)}
        records = ctest_reader.costs(build)
    except ctest_reader.CtestError as problem:
        raise rule_ledger.LedgerError(f"the records of the tests of the build do not read: {problem}") from None
    found: list[tuple[str, float]] = []
    for name, record in sorted(records.items()):
        count = record.cost.test_instructions
        if rule_id == STEP_MEMORY:
            value: float | None = record.cost.peak_rss_kb / cost_meter.KB_PER_GB
        else:
            value = None if count is None else count / GIGA
        if (
            value is not None
            and name in registered
            and cost_budget.is_judged(teststep.program_item(record.program, str(build)))
        ):
            found.append((name, value))
    return found


def fixture_measures(context: BuildContext) -> list[tuple[str, float]]:
    """Return the name and the instruction count of the last compile of each fixture of the build, in G.

    A fixture with no count gives no measure.  Complexity: linear in the tests of the build and in the records of the
    fixtures.

    Raises:
        rule_ledger.LedgerError: If the tests do not read, or a fixture has no record that counts
    """
    from assay.guard.engines.buildsys import held
    from assay.readers import ctest as ctest_reader

    build = context.require_build()
    try:
        tests = held.fixture_tests(build)
    except ctest_reader.CtestError as problem:
        raise rule_ledger.LedgerError(f"the tests of the build do not read: {problem}") from None
    found: list[tuple[str, float]] = []
    unread: list[str] = []
    for name, _, _ in tests:
        try:
            record = cost_record.read_fixture(Path(cost_record.fixture_record_file(build, name)))
        except cost_record.MissingRecord:
            unread.append(f"{name} (no record)")
            continue
        except cost_record.RecordError as problem:
            unread.append(f"{name} ({problem})")
            continue
        if record.instructions is not None:
            found.append((name, record.instructions / GIGA))
    if unread:
        raise rule_ledger.LedgerError(
            f"{len(unread)} fixtures have no record that counts, such as {unread[0]}.  Run each fixture of the build, "
            f"then the write"
        )
    return found


def judge(rule: rule_registry.Rule, context: BuildContext) -> list[check_report.Finding]:
    """Return the findings of a rule on the planted build of a plant, judged against the ledger of the copy.

    The witness calls this function (module_judged() of assay.guard.engines.meta.runner).  For a rule of a step, the
    findings also hold the findings of the judge of each step meter of METER_KINDS on the plant (THE KINDS OF A STEP
    METER).  Complexity: one run of the rule and of each judge of a step meter.

    Raises:
        NotApplicable: If the rule does not apply to the planted build
    """
    found = meta_runner.judged(rule, context.root, context.findings_of(rule))
    meters = METER_KINDS.get(rule.id, ())
    if not meters:
        return found
    from assay.guard.engines.buildsys import held

    if TEST_KIND in meters:
        found += [item for item in held.test_step_findings(rule, context) if item.check == rule.id]
    if FIXTURE_KIND in meters:
        found += [item for item in held.fixture_findings(rule, context) if item.check == rule.id]
    return found


def budget_member(rule_id: str) -> meta_runner.Member:
    """Return the member of a rule of the policy max: its findings and the write of its ledger."""
    return meta_runner.Member(partial(budget_findings, rule_id), partial(budget_written, rule_id))


RULES = {
    "step-cost": budget_member(STEP_COST),
    "step-memory": budget_member(STEP_MEMORY),
    "object-size": budget_member(OBJECT_SIZE),
    "header-alone": budget_member(HEADER_ALONE),
}
