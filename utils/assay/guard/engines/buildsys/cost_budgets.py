"""cost-budgets — a compile, a link, an object, a header alone or a target of a build that costs more than its rule.

THE INPUTS
    The compile database of the build names each compile job and its object.
    The code model of the CMake file API names each linked output: an
    executable, a shared library or a module.  A rule reads only the outputs
    that are there.  A target that the build does not make, such as a
    negative fixture, has no output to read.  The kind of the build and the
    name of its configuration give the ledger rows that apply
    (assay.core.meter).  The kind is the one line of BUILD_DIR/build-kind.txt,
    and the name is the one line of BUILD_DIR/build-configuration.txt.  A
    build with no kind or no name gives one error of the kind input.  A kind
    that is not lowercase words with hyphens gives the same error.  A name
    that the options of the CMake cache do not give also gives it
    (build_names()).

THE RULES OF A STEP
    compile-instructions, compile-memory and link-memory read the record
    OUTPUT.cost of each output (assay.core.record).  compile-instructions
    reads the user instructions of the compiler run, in G (10^9).
    compile-memory and link-memory read the peak resident memory, in GB of
    2^30 bytes.  A record of a ccache hit holds no time, and the rule counts
    the output but does not judge it.  An output
    with no usable record did not come through the build launcher, and it
    gives an error of the kind no-record.  A record is not usable when it has
    a different format or step, or a different number of bytes.  When no
    output has a usable record, the launcher is not wired, and the rule gives
    one error of the kind not-wired.

    The CPU time of a step increases with the load of the host, and the
    instruction count does not (assay.core.meter, THE INSTRUCTION COUNT).
    No rule judges the CPU time of a step, and compile-instructions judges
    the cost of a compile.  compile-instructions does not apply to a build in
    which no record holds a count.

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
    symbolic link resolves.  The limit comes from the held rule of the
    layer of the header: header-alone-foundation, header-alone-fixy or
    header-alone-crucible.  These problems each give an error of the kind
    input: a header of a different layer, a sentinel with no VALID list, and a
    sentinel with no header of include/.  The message also gives the CPU time of the sentinel
    from its record, with no threshold.  The rule does not apply to a build
    with no sentinel object, or to a build that a generator other than Ninja
    made.

THE JUDGE
    assay.core.budget judges each value against the limit and the ledger rows
    that judge the build (assay.core.budget, THE VERDICT and THE ROWS).  Each
    finding is an error.  The kinds of the findings:
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
    utils/ledgers/RULE.txt holds one row for each item and each build kind or
    configuration, with the reason (assay.core.meter, THE REGISTRY AND THE
    LEDGERS):

        @build/OUTPUT | KIND | VALUE | - | REASON     an output of a step or an object
        HEADER | KIND | VALUE | - | REASON            a header, for header-alone

    `guard write --rule RULE --build-dir DIR [--reason TEXT]` writes the rows
    of the name of the configuration of the build again, through the member
    function written of the rule.  It writes the measured value of each item
    with a row over the limit.  It removes each row of an item at or below
    the limit.  It keeps the rows of the other names and the rows of a
    ccache hit.  It keeps the reason of each item that keeps its row.  It
    gives the ledger the head comment LEDGER_COMMENT (assay.core.ledger, THE
    HEAD COMMENT).  An item over the limit with no row gets a new row with
    the reason of --reason.  A row of the build kind keeps an item of a
    build with options.  The write then gives such an item no row of its own.
    A ledger with no row has no file.


Complexity: one read of the compile database, the code model and the records of the outputs that a rule reads.
"""

import enum
import os
from collections.abc import Mapping, Sequence
from dataclasses import dataclass, replace
from functools import partial
from pathlib import Path

from assay.core import budget as cost_budget
from assay.core import configuration as build_config
from assay.core import ledger as rule_ledger
from assay.core import meter as cost_meter
from assay.core import record as cost_record
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
# The head comment that the member function written gives each ledger.  ROW is the row of the item of the rule.
LEDGER_COMMENT = (
    "# The ledger of the rule {rule} of utils/registry.txt: the items that a row keeps over the limit of the rule,\n"
    "# one row for each item and build kind or configuration.  A row is '{row}'.\n"
    "# KIND is the build kind of BUILD_DIR/build-kind.txt or the name of BUILD_DIR/build-configuration.txt, and\n"
    "# VALUE is the value at the last write, in {unit}.  A row keeps its item up to VALUE with the noise of its\n"
    "# unit (THE NOISE of utils/assay/core/budget.py).  `python3 utils/bin/guard explain {rule}` tells the rule,\n"
    "# and `python3 utils/bin/guard write --rule {rule} --build-dir DIR [--reason TEXT]` writes this file.\n"
)


class Quantity(enum.StrEnum):
    """The value of a record that a rule of a step reads (THE RULES OF A STEP)."""

    INSTRUCTIONS = "instructions"
    MEMORY = "memory"


@dataclass(frozen=True, slots=True)
class StepRule:
    """The step of the records that a rule reads, and the value of each record."""

    step: cost_record.Step
    quantity: Quantity


STEP_RULES = {
    "compile-instructions": StepRule(cost_record.COMPILE, Quantity.INSTRUCTIONS),
    "compile-memory": StepRule(cost_record.COMPILE, Quantity.MEMORY),
    "link-memory": StepRule(cost_record.LINK, Quantity.MEMORY),
}
OBJECT_RULES = frozenset({"function-size", "object-text"})
HEADER_ALONE = "header-alone"


@dataclass(frozen=True, slots=True)
class Output:
    """One output of the build that is there: an object of a compile job, or a linked output."""

    path: Path
    source: Path | None


@dataclass(frozen=True, slots=True)
class Measured:
    """The values of the items of one rule.

    `held` holds each item that the build holds and the rule does not judge,
    such as a ccache hit.
    """

    values: tuple[cost_budget.Measure, ...] = ()
    held: frozenset[str] = frozenset()
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


def build_names(context: BuildContext, rule_id: str) -> tuple[str, str, check_report.Finding | None]:
    """Return the build kind and the name of the configuration of the build, or the error of a build with no names.

    The name comes from build-configuration.txt (assay.core.meter).  When the
    CMake cache of the build can be read, its options of NAME_OPTIONS must
    give the same name (assay.core.configuration, THE CONFIGURATION).

    Raises:
        NotApplicable: If the call gives no build directory
    """
    kind, problem = build_kind(context, rule_id)
    if problem is not None:
        return "", "", problem
    build = context.require_build()
    name = cost_meter.read_configuration(str(build))
    shown = context.shown(build / cost_meter.CONFIGURATION_FILE)
    if name is None:
        missing = (
            "the build has no name of its configuration.  cmake/BuildLauncher.cmake writes it at each configure.  "
            "Configure the build again"
        )
        return kind, "", input_error(rule_id, shown, missing)
    wanted = cached_name(build, kind)
    if wanted is not None and name != wanted:
        differs = (
            f"the file names the configuration {name}, and the options of the CMake cache give {wanted}.  "
            f"cmake/BuildLauncher.cmake and NAME_OPTIONS of assay.core.configuration must give the same words"
        )
        return kind, "", input_error(rule_id, shown, differs)
    return kind, name, None


def cached_name(build: Path, kind: str) -> str | None:
    """Return the name of the configuration that the CMake cache of a build gives, or None for no cache to read."""
    try:
        entries = cmake_reader.cache(build)
    except cmake_reader.CmakeError:
        return None
    words = [
        word
        for word, key in build_config.NAME_OPTIONS
        if key in entries and build_config.normalized(entries[key].type_name, entries[key].value) == "on"
    ]
    return "-".join([kind, *words])


def threshold_rules(rule_id: str) -> tuple[str, ...]:
    """Return the measured rules that give the limit of a rule: the held rules of header-alone, or the rule."""
    return tuple(HEADER_ALONE_LAYERS.values()) if rule_id == HEADER_ALONE else (rule_id,)


def budgets_of(
    context: BuildContext, rule_id: str
) -> tuple[dict[str, check_report.Budget], list[check_report.Finding]]:
    """Return the limit of each measured rule that a rule reads, and an error for each one that is missing."""
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
    """Read the instructions or the peak memory of each step from its record (THE RULES OF A STEP).

    Complexity: one read of the record of each output.

    Raises:
        NotApplicable: If the rule reads instructions and no record that holds a cost block holds a count
    """
    build = context.require_build()
    spec = STEP_RULES[rule_id]
    noun = "compile job" if spec.step == cost_record.COMPILE else "link"
    values: list[cost_budget.Measure] = []
    held: set[str] = set()
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
        values.append(cost_budget.Measure(item, context.shown(output.source or output.path), value, said))
    if spec.quantity == Quantity.INSTRUCTIONS and not values and uncounted:
        raise meta_runner.NotApplicable(
            f"no record of the {uncounted} compile jobs that ran holds an instruction count, because the host gives no "
            f"exact counter (utils/assay/core/meter.py, THE INSTRUCTION COUNT).  On such a host, no rule judges the CPU "
            f"work of a compile"
        )
    return Measured(tuple(values), frozenset(held), tuple(record_problems(context, rule_id, outputs, problems)))


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
    """Judge each value against its limit and the ledger rows that judge the build (THE JUDGE).

    Complexity: linear in the values.
    """
    found: list[check_report.Finding] = []
    for value in measured.values:
        verdict = cost_budget.judge(rule_id, budgets[value.threshold_rule or rule_id], value, rows)
        if verdict is not None:
            kind = "over-limit" if value.item not in rows.admitted else "risen"
            found.append(replace(verdict, kind=kind, entity=value.item))
    return found


def budget_findings(rule_id: str, context: BuildContext) -> list[check_report.Finding]:
    """Give the findings of one rule of a step, of an object or of header-alone (THE JUDGE).

    Raises:
        NotApplicable: If the call gives no build directory, or the rule does not apply to the build
    """
    kind, name, problem = build_names(context, rule_id)
    if problem is not None:
        return [problem]
    budgets, missing = budgets_of(context, rule_id)
    if missing:
        return missing
    measured = measured_of(context, rule_id)
    rows = ledger_rows(context, rule_id, kind, name)
    return [*measured.problems, *rows.problems, *judged_values(rule_id, budgets, measured, rows)]


def ledger_rows(context: BuildContext, rule_id: str, kind: str, name: str) -> cost_budget.Rows:
    """Return the ledger rows that judge the build, with each problem of the ledger as an error of the kind input.

    The rows of the name of the configuration come first, then the rows of the build kind (assay.core.budget, THE
    ROWS).
    """
    rows = cost_budget.read_rows(
        rule_id, ledger_shape(rule_id), rule_ledger.ledger_path(rule_id, context.root), kind, name
    )
    return rows.replaced(problems=tuple(replace(problem, kind="input") for problem in rows.problems))


def budget_written(
    rule_id: str, context: BuildContext, ledger: rule_ledger.Ledger, reason: str | None
) -> rule_ledger.Ledger:
    """Return the ledger of a rule with the rows of the configuration of the build written again (THE LEDGERS).

    Raises:
        NotApplicable: If the call gives no build directory, or the rule does not apply to the build
        rule_ledger.LedgerError: If an input or the ledger cannot be read, or a new row has no reason
    """
    kind, name, problem = build_names(context, rule_id)
    budgets, missing = budgets_of(context, rule_id)
    measured = measured_of(context, rule_id) if problem is None and not missing else Measured()
    rows = ledger_rows(context, rule_id, kind, name)
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
        if key.kind != name or cost_meter.item_of(key.path, key.entity, shape) in measured.held
    }
    unexplained: list[str] = []
    for value in measured.values:
        budget = budgets[value.threshold_rule or rule_id]
        path, entity = cost_meter.item_cells(value.item, shape)
        key = rule_ledger.Key(path, name, entity)
        earlier = ledger.base.get(key)
        if value.value <= budget.limit:
            base.pop(key, None)
            continue
        if earlier is None and rule_ledger.Key(path, kind, entity) in ledger.base:
            # A row of the build kind keeps the item.
            continue
        why = earlier.reason if earlier is not None else reason
        if not why:
            unexplained.append(value.item)
            continue
        base[key] = rule_ledger.Entry(key, cost_budget.kept_value(value.value), why)
    if unexplained:
        raise rule_ledger.LedgerError(
            f"these items are more than the limit and have no row.  Give the reason of their rows with --reason: "
            f"{', '.join(unexplained)}"
        )
    units = " or ".join(sorted({budget.unit for budget in budgets.values()}))
    comment = LEDGER_COMMENT.format(rule=rule_id, row=cost_meter.ITEM_ROWS[shape], unit=units)
    return replace(ledger, base=base, comment=comment)


def budget_member(rule_id: str) -> meta_runner.Member:
    """Return the member of a rule of the policy max: its findings and the write of its ledger."""
    return meta_runner.Member(partial(budget_findings, rule_id), partial(budget_written, rule_id))


RULES = {
    "compile-instructions": budget_member("compile-instructions"),
    "compile-memory": budget_member("compile-memory"),
    "link-memory": budget_member("link-memory"),
    "function-size": budget_member("function-size"),
    "object-text": budget_member("object-text"),
    "header-alone": budget_member("header-alone"),
}
