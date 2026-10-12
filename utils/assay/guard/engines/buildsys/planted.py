"""The planted build of a plant of a build rule: build.txt tells what the build directory of the plant holds.

THE FILE
    A plant of a rule of the engine build holds the file build.txt in the
    rows grammar of assay.core.rows.  plant_context() copies the plant to a
    throwaway repository (assay.guard.engines.meta.runner.copy_plant).  It
    makes the build directory build/ there from the rows, and it gives the
    context of the engine for that tree.  The rows:

        format 1
        kind KIND                         the build kind of build-kind.txt, and the
                                          configuration KIND with the one fact kind
                                          KIND (BuildContext, THE CONFIGURATION)
        configuration NAME                the name of build-configuration.txt.  A
                                          build with a kind row and no such row has
                                          the name KIND there
        cache NAME [type=TYPE] | VALUE    an entry of the CMake cache
        compile OBJECT SOURCE [FIELD=VALUE]... [| WORD...]
                                          a compile job: a row of the compile
                                          database, an ELF object and a record
        link OUTPUT [FIELD=VALUE]...      a linked output of a target, and its record
        target NAME [FIELD=VALUE]... | SOURCE... [| FRAGMENT]...
                                          a target of the code model, with its
                                          sources and the fragments of its link line
        deps OUTPUT [state=STATE] | FILE...
                                          the list of an output in the dependency log
        fixture NAME SOURCE [FIELD=VALUE]... | FILE...
                                          a negative fixture: a test of the driver and
                                          the record of its last run
        test NAME [FIELD=VALUE]... | WORD...
                                          a registered test and its command
        test-record NAME [FIELD=VALUE]... the record of the last run of a test
        file PATH size=BYTES              a file of the tree with that number of bytes
        cache-file PATH size=BYTES        a file in the root of the caches
        limit NAME BYTES                  the limit of one cache, as an alternative to LIMITS
        configure seconds=SECONDS         a last configure that ends SECONDS after its start

    The fields of a compile:
      * result: `built`, `hit` or `failed`
      * cpu and wall: the times in seconds
      * peak: the peak memory in GB
      * instructions: the instruction count in G
      * last-cpu and last-instructions: the cost that a record of a hit keeps
        from the cost store, the cost of the compile that made the object
      * record: yes, no, stale or format
      * functions: NAME:BYTES,...
      * text: the bytes of one more executable section with no symbol
      * all: yes when `ninja -t inputs all` gives the object.
    The fields of a link: type, result, cpu, peak, record and `exists`.  The
    fields of a target: type, pch (headers, divided by commas), file, line,
    pch-line and backtrace.  The file and the line give the position of the
    command that makes the target.  backtrace=no gives a target with no
    backtrace, as the reply of CMake gives for some targets.  The fields of a fixture: user, system,
    instructions and record (yes, no or blind).  A blind record does not know
    the files of the compile.  The fields of a test: line (of its
    registration in test/CMakeLists.txt) and labels.  The fields of a record
    of a test: result, cpu, wall, peak, instructions (in G) and program.

    A configure row writes build.ninja after each other file of the build.
    It also writes the file configure-time.txt of a configure of SECONDS
    (assay.loop.history, THE CONFIGURE TIME).  With plugin=SECONDS, it writes
    the record of a plugin build of those seconds at the start of the
    configure (assay.loop.history, THE PLUGIN BUILD).  The times of a
    configure come from the clock.  A file of a plant cannot hold them.

    A word or a cell can hold {root}, {build}, {cxx} (the patched compiler
    of assay.core.testing) and {python}.  A FILE of a list is a path of the
    tree, @build/PATH of the build directory, or an absolute path.  A plant
    can also hold the directory planted-build/, such as a build.ninja.
    plant_context() copies its files into the build directory first.

THE TOOLS
    The build gets a ninja and a ctest of its own.  They print the inputs of
    the target all, the lists of the dependency log and the list of the tests
    of the rows.  The CMake cache names them, as it names the tools of a
    configured build (CMAKE_MAKE_PROGRAM, CMAKE_CTEST_COMMAND).  The readers
    of assay.readers then read the planted build as a configured one.

`guard selftest tests.guard.engines.buildsys.test_planted` does a test of the module.
"""

import json
import shutil
import sys
from collections.abc import Mapping, MutableMapping, Sequence
from dataclasses import dataclass, field
from pathlib import Path

from assay.core import configuration as build_config
from assay.core import meter as cost_meter
from assay.core import record as cost_record
from assay.core import registry as rule_registry
from assay.core import rows as rows_grammar
from assay.core import testing
from assay.guard.engines.buildsys.runner import BuildContext
from assay.guard.engines.meta import runner as meta_runner
from assay.readers import cmake as cmake_reader
from assay.readers import compdb
from assay.readers import elf as elf_reader
from assay.readers import ninja as ninja_reader

BUILD_FILE = "build.txt"
FORMAT = "1"
BUILD = "build"
COPIED_BUILD = "planted-build"
CACHES = "cache"
TOOLS = "planted-tools"
GIGA = 10**9
KB_PER_GB = cost_meter.KB_PER_GB
DEFAULT_FUNCTION = ("f", 100)
CMAKE_FILE = "test/CMakeLists.txt"
# The fields of each row kind.
COMPILE_FIELDS = frozenset(
    {
        "result",
        "cpu",
        "wall",
        "peak",
        "instructions",
        "last-cpu",
        "last-instructions",
        "record",
        "functions",
        "text",
        "all",
    }
)
LINK_FIELDS = frozenset({"type", "result", "cpu", "peak", "record", "exists"})
TARGET_FIELDS = frozenset({"type", "pch", "file", "line", "pch-line", "backtrace"})
DEPS_FIELDS = frozenset({"state"})
FIXTURE_FIELDS = frozenset({"user", "system", "instructions", "record"})
TEST_FIELDS = frozenset({"line", "labels"})
RECORD_FIELDS = frozenset({"result", "cpu", "wall", "peak", "instructions", "program"})
SIZE_FIELDS = frozenset({"size"})
CACHE_FIELDS = frozenset({"type"})
CONFIGURE_FIELDS = frozenset({"seconds", "plugin"})
# The planted ninja: it prints the inputs of the target all and the lists of the dependency log from ninja.json.
NINJA_TOOL = """\
import json
import sys
from pathlib import Path

data = json.loads(Path(__file__).with_name("ninja.json").read_text(encoding="utf-8"))
words = sys.argv[1:]
tool = words[words.index("-t") + 1]
rest = words[words.index("-t") + 2:]
if tool == "inputs":
    sys.stdout.write("".join(name + "\\n" for name in data["inputs"]))
elif tool == "deps":
    names = rest or sorted(data["deps"])
    sys.stdout.write("".join(data["deps"].get(name, name + ": deps not found\\n") + "\\n" for name in names))
else:
    sys.stderr.write("the planted ninja has no tool " + tool + "\\n")
    sys.exit(2)
"""
# The planted ctest: it prints the list of the tests from ctest.json, as `ctest --show-only=json-v1` does.
CTEST_TOOL = """\
import sys
from pathlib import Path

sys.stdout.write(Path(__file__).with_name("ctest.json").read_text(encoding="utf-8"))
"""


class PlantError(ValueError):
    """The file build.txt of a plant does not obey its rows (THE FILE)."""


@dataclass(frozen=True, slots=True)
class Planted:
    """The parts of a planted build that the rows give, before the writes.

    Each part is a container that the rows fill.  `rows`, `targets` and
    `tests` hold the JSON documents of the compile database, the code model
    and the test list.  `kinds` holds the kind of each kind row, and the last
    one is the build kind.
    """

    rows: list[Mapping[str, object]] = field(default_factory=list)
    targets: list[Mapping[str, object]] = field(default_factory=list)
    inputs: list[str] = field(default_factory=list)
    deps: MutableMapping[str, str] = field(default_factory=dict)
    tests: list[Mapping[str, object]] = field(default_factory=list)
    cache: MutableMapping[str, str | tuple[str, str]] = field(default_factory=dict)
    limits: MutableMapping[str, int] = field(default_factory=dict)
    kinds: list[str] = field(default_factory=list)
    configures: list[tuple[float, float]] = field(default_factory=list)
    names: list[str] = field(default_factory=list)

    @property
    def kind(self) -> str | None:
        """The build kind of the last kind row, or None when build.txt has no kind row."""
        return self.kinds[-1] if self.kinds else None

    @property
    def configuration_name(self) -> str | None:
        """The name of the last configuration row, else the build kind, or None when build.txt has no such row."""
        return self.names[-1] if self.names else self.kind

    @property
    def configure(self) -> tuple[float, float] | None:
        """The seconds of the configure and of its plugin build of the last configure row, or None with no such row."""
        return self.configures[-1] if self.configures else None


def expanded(text: str, root: Path, build: Path) -> str:
    """Return a word or a cell with each placeholder of THE FILE replaced."""
    return (
        text.replace("{root}", str(root))
        .replace("{build}", str(build))
        .replace("{cxx}", testing.patched_compiler() or "c++")
        .replace("{python}", sys.executable)
    )


def file_path(text: str, root: Path, build: Path) -> str:
    """Return the absolute path of a FILE of a list (THE FILE)."""
    named = expanded(text, root, build)
    if named.startswith("@build/"):
        return str(build / named.removeprefix("@build/"))
    return named if Path(named).is_absolute() else str(root / named)


def number(fields: Mapping[str, str], key: str, row: rows_grammar.Row, default: float) -> float:
    """Return a decimal field of a row, or the default when the row has no such field.

    Raises:
        rows_grammar.RowsError: If the value is not a decimal number
    """
    return rows_grammar.decimal(fields[key], row, key) if key in fields else default


def count_of(fields: Mapping[str, str], key: str, row: rows_grammar.Row) -> int | None:
    """Return a field in G (10^9) as an integer count, or None when the row has no such field.

    Raises:
        rows_grammar.RowsError: If the value is not a decimal number
    """
    return int(rows_grammar.decimal(fields[key], row, key) * GIGA) if key in fields else None


def choice(fields: Mapping[str, str], key: str, allowed: Sequence[str], row: rows_grammar.Row) -> str:
    """Return a field that has one of the permitted values, or the first value when the row has no such field.

    Raises:
        rows_grammar.RowsError: If the value is not one of the permitted values
    """
    value = fields.get(key, allowed[0])
    if value not in allowed:
        raise row.error(f"the field {key} is {value!r}, and it is one of {', '.join(allowed)}")
    return value


def measurement(cpu: float, wall: float, peak_gb: float, instructions: int | None = None) -> cost_meter.Measurement:
    """Return the measurement of a planted step."""
    return cost_meter.Measurement(0, cpu, 0.0, wall, int(peak_gb * KB_PER_GB), 1.0, 1.0, None, instructions)


def write_object(path: Path, functions: Sequence[tuple[str, int]], loose_text: int) -> None:
    """Write an ELF object with one executable section for each function, and one more of loose_text bytes.

    The sections hold no bytes in the file.  The readers of the object read only the section headers and the symbols.
    """
    symbols, strings = elf_reader.symbol_table(
        elf_reader.PlantedSymbol(name, size=size, kind=elf_reader.STT_FUNC, section=1) for name, size in functions
    )
    code = elf_reader.SHF_ALLOC | elf_reader.SHF_EXECINSTR
    sections = [
        elf_reader.Planted(".symtab", symbols, elf_reader.SHT_SYMTAB, link=2, entry_size=elf_reader.SYMBOL_SIZE),
        elf_reader.Planted(".strtab", strings, elf_reader.SHT_STRTAB),
    ]
    sections += [elf_reader.Planted(".text", flags=code, size=size) for _, size in functions]
    if loose_text:
        sections.append(elf_reader.Planted(".text", flags=code, size=loose_text))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(elf_reader.make_file(sections))


def functions_of(text: str, row: rows_grammar.Row) -> list[tuple[str, int]]:
    """Return the functions of a field functions=NAME:BYTES,...

    Raises:
        rows_grammar.RowsError: If an item is not NAME:BYTES
    """
    found: list[tuple[str, int]] = []
    for item in text.split(","):
        name, colon, size = item.partition(":")
        if not name or not colon:
            raise row.error(f"the item {item!r} of the field functions is not NAME:BYTES")
        found.append((name, rows_grammar.number(size, 1 << 40, row, "function size")))
    return found


def write_record(
    step: cost_record.Step,
    output: Path,
    source: Path | None,
    run: cost_meter.Measurement,
    result: str,
    last: cost_meter.Measurement | None = None,
) -> None:
    """Write the record of one step as `loop step` writes it.

    `last` is the measurement of the compile that made the object of a hit, which the cost store gives its record.
    """
    from assay.loop import step as step_meter

    counts = step_meter.compile_counts(run, False, None) if step == cost_record.COMPILE else ()
    last_cost = None if last is None else cost_record.measured(last, step_meter.compile_counts(last, False, None))
    record = step_meter.step_record(
        step, str(output), None if source is None else str(source), run, result, counts, last_cost
    )
    cost_record.write(cost_record.record_file(output), record)


def spoil_record(output: Path, how: str) -> None:
    """Make the record of an output a stale record or a record of a different format (THE FILE, the field record)."""
    if how == "stale":
        output.write_bytes(output.read_bytes() + b"\0" * 8)
    elif how == "format":
        cost_record.record_file(output).write_text('{"format": 2}\n', encoding="utf-8")


def plant_compile(planted: Planted, row: rows_grammar.Row, root: Path, build: Path) -> None:
    """Plant one compile job: its row of the compile database, its object, its record and its position in all.

    Raises:
        rows_grammar.RowsError: If the row does not have the shape of a compile row
    """
    if len(row.words) < 3:
        raise row.error("a compile row is 'compile OBJECT SOURCE [FIELD=VALUE]... [| WORD...]'")
    fields = rows_grammar.fields(row, COMPILE_FIELDS, start=3)
    output, source = build / row.words[1], root / row.words[2]
    functions = functions_of(fields["functions"], row) if "functions" in fields else [DEFAULT_FUNCTION]
    write_object(output, functions, int(number(fields, "text", row, 0)))
    words = [expanded(word, root, build) for word in rows_grammar.head_words(row.cells[0])] if row.cells else []
    entry: dict[str, object] = {"directory": str(build), "file": str(source), "output": str(output)}
    if words:
        entry["arguments"] = words
    else:
        entry["command"] = f"c++ -o {output} -c {source}"
    planted.rows.append(entry)
    if choice(fields, "all", ("yes", "no"), row) == "yes":
        planted.inputs.append(row.words[1])
    record = choice(fields, "record", ("yes", "no", "stale", "format"), row)
    if record == "no":
        return
    result = choice(fields, "result", ("built", "hit", "failed"), row)
    cpu = number(fields, "cpu", row, 1.0)
    run = measurement(
        cpu, number(fields, "wall", row, cpu), number(fields, "peak", row, 0.1), count_of(fields, "instructions", row)
    )
    last = None
    if result == "hit" and ("last-cpu" in fields or "last-instructions" in fields):
        last_cpu = number(fields, "last-cpu", row, 1.0)
        last = measurement(last_cpu, last_cpu, 0.1, count_of(fields, "last-instructions", row))
    write_record(cost_record.COMPILE, output, source, run, result, last)
    spoil_record(output, record)


def plant_link(planted: Planted, row: rows_grammar.Row, build: Path) -> None:
    """Plant one linked output: its target in the code model, the file and its record.

    Raises:
        rows_grammar.RowsError: If the row does not have the shape of a link row
    """
    if len(row.words) < 2:
        raise row.error("a link row is 'link OUTPUT [FIELD=VALUE]...'")
    fields = rows_grammar.fields(row, LINK_FIELDS)
    output = build / row.words[1]
    kind = choice(fields, "type", ("EXECUTABLE", "SHARED_LIBRARY", "MODULE_LIBRARY"), row)
    planted.targets.append({"name": output.name, "type": kind, "artifacts": [{"path": row.words[1]}]})
    if choice(fields, "exists", ("yes", "no"), row) == "no":
        return
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(elf_reader.MAGIC + b" program")
    record = choice(fields, "record", ("yes", "no", "stale", "format"), row)
    if record == "no":
        return
    cpu = number(fields, "cpu", row, 0.5)
    write_record(
        cost_record.LINK,
        output,
        None,
        measurement(cpu, cpu, number(fields, "peak", row, 0.1)),
        choice(fields, "result", ("built", "failed"), row),
    )
    spoil_record(output, record)


def plant_target(planted: Planted, row: rows_grammar.Row, root: Path, build: Path) -> None:
    """Plant one target of the code model with its sources, its precompiled headers and the fragments of its link line.

    Raises:
        rows_grammar.RowsError: If the row does not have the shape of a target row
    """
    if len(row.words) < 2 or not row.cells:
        raise row.error("a target row is 'target NAME [FIELD=VALUE]... | SOURCE... [| FRAGMENT]...'")
    fields = rows_grammar.fields(row, TARGET_FIELDS)
    line = int(number(fields, "line", row, 4))
    pch_line = int(number(fields, "pch-line", row, 9))
    sources = [
        {"path": expanded(path, root, build), "compileGroupIndex": 0}
        if index == 0
        else {"path": expanded(path, root, build)}
        for index, path in enumerate(rows_grammar.head_words(row.cells[0]))
    ]
    group: dict[str, object] = {"language": "CXX", "sourceIndexes": [0] if sources else []}
    headers = [header for header in fields.get("pch", "").split(",") if header]
    if headers:
        group["precompileHeaders"] = [{"header": header, "backtrace": 2} for header in headers]
    position = {"backtrace": 1} if choice(fields, "backtrace", ("yes", "no"), row) == "yes" else {}
    planted.targets.append(
        {
            "name": row.words[1],
            "type": fields.get("type", "STATIC_LIBRARY"),
            **position,
            "paths": {"source": ".", "build": "."},
            "backtraceGraph": {
                "commands": ["add_library", "target_precompile_headers"],
                "files": [fields.get("file", "CMakeLists.txt")],
                "nodes": [
                    {"file": 0},
                    {"command": 0, "file": 0, "line": line, "parent": 0},
                    {"command": 1, "file": 0, "line": pch_line, "parent": 0},
                ],
            },
            "sources": sources,
            "compileGroups": [group],
            "link": {
                "commandFragments": [
                    {"fragment": expanded(cell, root, build), "role": "flags"} for cell in row.cells[1:]
                ]
            },
        }
    )


def plant_deps(planted: Planted, row: rows_grammar.Row, root: Path, build: Path) -> None:
    """Plant the list of one output in the dependency log, as `ninja -t deps` prints it.

    Raises:
        rows_grammar.RowsError: If the row does not have the shape of a deps row
    """
    if len(row.words) < 2:
        raise row.error("a deps row is 'deps OUTPUT [state=VALID|STALE|MISSING] | FILE...'")
    fields = rows_grammar.fields(row, DEPS_FIELDS)
    output = row.words[1]
    state = choice(fields, "state", ("VALID", "STALE", "MISSING"), row)
    if state == "MISSING":
        planted.deps[output] = f"{output}{ninja_reader.NOT_FOUND}\n"
        return
    files = [file_path(name, root, build) for name in rows_grammar.head_words(row.cells[0])] if row.cells else []
    planted.deps[output] = f"{output}{ninja_reader.DEPS_MARK}{len(files)}, deps mtime 1 ({state})\n" + "".join(
        f"    {name}\n" for name in files
    )


def plant_fixture(planted: Planted, row: rows_grammar.Row, root: Path, build: Path) -> None:
    """Plant one negative fixture: a test with a command that runs the driver, and the record of its last run.

    Raises:
        rows_grammar.RowsError: If the row does not have the shape of a fixture row
    """
    if len(row.words) < 3:
        raise row.error("a fixture row is 'fixture NAME SOURCE [FIELD=VALUE]... | FILE...'")
    fields = rows_grammar.fields(row, FIXTURE_FIELDS, start=3)
    name, source = row.words[1], root / row.words[2]
    planted.tests.append(
        {
            "name": name,
            "command": [
                sys.executable,
                "-S",
                str(root / "utils" / "bin" / "loop"),
                "fixture",
                "--warnings-dir",
                str(build / "check-warnings"),
                str(build),
                str(source),
                name,
                "a planted regex",
            ],
        }
    )
    record = choice(fields, "record", ("yes", "no", "blind"), row)
    if record == "no":
        return
    files = tuple(file_path(item, root, build) for item in rows_grammar.head_words(row.cells[0])) if row.cells else ()
    planted_record = cost_record.FixtureRecord(
        name,
        "compiled",
        number(fields, "user", row, 1.0),
        number(fields, "system", row, 0.1),
        count_of(fields, "instructions", row),
        None if record == "blind" else files,
    )
    cost_record.write(cost_record.fixture_record_file(build, name), planted_record)


def plant_test(planted: Planted, row: rows_grammar.Row, root: Path, build: Path) -> None:
    """Plant one registered test: its name, its command, its labels and the line of its registration.

    Raises:
        rows_grammar.RowsError: If the row does not have the shape of a test row
    """
    if len(row.words) < 2 or not row.cells:
        raise row.error("a test row is 'test NAME [FIELD=VALUE]... | WORD...'")
    fields = rows_grammar.fields(row, TEST_FIELDS)
    test: dict[str, object] = {
        "name": row.words[1],
        "command": [expanded(word, root, build) for word in rows_grammar.head_words(row.cells[0])],
        "backtrace": len(planted.tests) + 1,
    }
    labels = [label for label in fields.get("labels", "").split(",") if label]
    if labels:
        test["properties"] = [{"name": "LABELS", "value": labels}]
    test["line"] = int(number(fields, "line", row, 7))
    planted.tests.append(test)


def plant_test_record(row: rows_grammar.Row, root: Path, build: Path) -> None:
    """Plant the record of the last run of one test.

    Raises:
        rows_grammar.RowsError: If the row does not have the shape of a record row
    """
    if len(row.words) < 2:
        raise row.error("a test-record row is 'test-record NAME [FIELD=VALUE]...'")
    fields = rows_grammar.fields(row, RECORD_FIELDS)
    wall = number(fields, "wall", row, 1.0)
    run = measurement(number(fields, "cpu", row, wall), wall, number(fields, "peak", row, 0.1))
    program = expanded(fields.get("program", f"{build}/test/{row.words[1]}"), root, build)
    result = choice(fields, "result", ("passed", "failed", "rejected"), row)
    count = count_of(fields, "instructions", row)
    counts = () if count is None else ((cost_record.TEST_COUNT_KEY, count),)
    record = cost_record.TestRecord(row.words[1], result, program, run.exit_code, cost_record.measured(run, counts))
    cost_record.write(cost_record.test_record_file(build, row.words[1]), record)


def plant_file(row: rows_grammar.Row, base: Path) -> None:
    """Write one file in a base directory, with the number of bytes that its field `size` gives.

    Raises:
        rows_grammar.RowsError: If the row has no field `size`, or its path goes out of the base directory
    """
    fields = rows_grammar.fields(row, SIZE_FIELDS)
    if len(row.words) < 2 or "size" not in fields or row.words[1].startswith("/") or ".." in row.words[1].split("/"):
        raise row.error(f"a {row.words[0]} row is '{row.words[0]} PATH size=BYTES', with a relative PATH")
    path = base / row.words[1]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"x" * rows_grammar.number(fields["size"], 1 << 34, row, "size"))


def _ctest_document(tests: Sequence[Mapping[str, object]], root: Path) -> dict[str, object]:
    """Return the JSON document of `ctest --show-only=json-v1` for the planted tests."""
    nodes: list[dict[str, object]] = [{"file": 0}]
    records: list[dict[str, object]] = []
    for test in tests:
        line = test.get("line", 7)
        nodes.append({"file": 0, "line": line, "command": 0, "parent": 0})
        record = {key: value for key, value in test.items() if key != "line"}
        record["backtrace"] = len(nodes) - 1
        records.append(record)
    return {
        "kind": "ctestInfo",
        "version": {"major": 1, "minor": 0},
        "backtraceGraph": {"commands": ["add_test"], "files": [str(root / CMAKE_FILE)], "nodes": nodes},
        "tests": records,
    }


def read_rows(plant_file_path: Path, root: Path, build: Path) -> Planted:
    """Read build.txt, and write each file that its rows give in the tree and in the build directory (THE FILE).

    Complexity: linear in the rows and the bytes that they write.

    Raises:
        PlantError: If a row does not obey the grammar or the shape of its kind
    """
    planted = Planted()
    try:
        rows = rows_grammar.read(plant_file_path, BUILD_FILE)
        if not rows or rows[0].words != ("format", FORMAT):
            raise rows_grammar.RowsError(BUILD_FILE, rows[0].line if rows else 0, f"the first row is 'format {FORMAT}'")
        for row in rows[1:]:
            kind = row.words[0]
            if kind == "kind" and len(row.words) == 2:
                planted.kinds.append(row.words[1])
            elif kind == "configuration" and len(row.words) == 2:
                planted.names.append(row.words[1])
            elif kind == "cache" and len(row.words) >= 2 and len(row.cells) == 1:
                cache_type = rows_grammar.fields(row, CACHE_FIELDS).get("type")
                value = expanded(row.cells[0], root, build)
                planted.cache[row.words[1]] = (cache_type, value) if cache_type else value
            elif kind == "compile":
                plant_compile(planted, row, root, build)
            elif kind == "link":
                plant_link(planted, row, build)
            elif kind == "target":
                plant_target(planted, row, root, build)
            elif kind == "deps":
                plant_deps(planted, row, root, build)
            elif kind == "fixture":
                plant_fixture(planted, row, root, build)
            elif kind == "test":
                plant_test(planted, row, root, build)
            elif kind == "test-record":
                plant_test_record(row, root, build)
            elif kind == "file":
                plant_file(row, root)
            elif kind == "cache-file":
                plant_file(row, root / CACHES)
            elif kind == "limit" and len(row.words) == 3:
                planted.limits[row.words[1]] = rows_grammar.number(row.words[2], 1 << 50, row, "limit")
            elif kind == "configure" and len(row.words) in (2, 3):
                fields = rows_grammar.fields(row, CONFIGURE_FIELDS, start=1)
                if "seconds" not in fields:
                    raise row.error("a configure row is 'configure seconds=SECONDS [plugin=SECONDS]'")
                seconds = rows_grammar.decimal(fields["seconds"], row, "seconds")
                plugin = rows_grammar.decimal(fields.get("plugin", "0"), row, "plugin")
                if plugin > seconds:
                    raise row.error("the plugin build of a configure row is longer than the configure")
                planted.configures.append((seconds, plugin))
            else:
                raise row.error(f"the row kind {kind!r} or its words do not have a shape of {BUILD_FILE}")
    except rows_grammar.RowsError as problem:
        raise PlantError(str(problem)) from None
    return planted


def write_build(planted: Planted, root: Path, build: Path) -> None:
    """Write the compile database, the replies of the file API, the tools and the build kind of a planted build."""
    build.mkdir(parents=True, exist_ok=True)
    if planted.kind is not None:
        (build / cost_meter.KIND_FILE).write_text(f"{planted.kind}\n", encoding="utf-8")
    if planted.configuration_name is not None:
        (build / cost_meter.CONFIGURATION_FILE).write_text(f"{planted.configuration_name}\n", encoding="utf-8")
    compdb.plant(build, planted.rows)
    tools = build / TOOLS
    tools.mkdir(exist_ok=True)
    for name, text in (("ninja", NINJA_TOOL), ("ctest", CTEST_TOOL)):
        tool = tools / name
        tool.write_text(f"#!{sys.executable} -S\n{text}", encoding="utf-8")
        tool.chmod(0o755)
    (tools / "ninja.json").write_text(json.dumps({"inputs": planted.inputs, "deps": planted.deps}), encoding="utf-8")
    (tools / "ctest.json").write_text(json.dumps(_ctest_document(planted.tests, root)), encoding="utf-8")
    log = ninja_reader.log_file(build)
    if not log.exists():
        log.write_text(f"# ninja log v{ninja_reader.LOG_VERSIONS.stop - 1}\n", encoding="utf-8")
    cache: dict[str, str | tuple[str, str]] = {
        "CMAKE_GENERATOR": "Ninja",
        "CMAKE_MAKE_PROGRAM": ("FILEPATH", str(tools / "ninja")),
        "CMAKE_CTEST_COMMAND": ("FILEPATH", str(tools / "ctest")),
        "CMAKE_HOME_DIRECTORY": ("INTERNAL", str(root)),
    }
    cache.update(planted.cache)
    cmake_reader.plant(build, cache=cache, targets=planted.targets, source=str(root), build=str(build))
    if planted.configure is not None:
        write_configure(build, *planted.configure)


def write_configure(build: Path, seconds: float, plugin: float) -> None:
    """Write build.ninja again, and configure-time.txt of a configure that ends at that write (THE FILE).

    build.ninja keeps its bytes.  Its write comes after each other file of
    the build.  The reply of the file API is then not newer than it.  The
    configure takes SECONDS from its start to its end
    (assay.loop.history.configure_time()).  A plugin build of more than 0 s
    starts with the configure, and its record goes into the build directory.
    """
    from assay.loop import history as loop_history

    graph = ninja_reader.graph_file(build)
    graph.write_bytes(graph.read_bytes() if graph.is_file() else b"")
    written = graph.stat().st_mtime
    start = written - seconds
    (build / loop_history.CONFIGURE_TIME_FILE).write_text(f"{start!r} {written!r}\n", encoding="ascii")
    if plugin > 0:
        record = build / loop_history.PLUGIN_BUILD_TIME_FILE
        record.parent.mkdir(parents=True, exist_ok=True)
        record.write_text(f"{start!r} {start + plugin!r}\n", encoding="ascii")


def plant_context(plant: Path, scratch: Path) -> BuildContext:
    """Copy a plant to a throwaway repository, make its planted build, and give the context of the engine (THE FILE).

    A plant with no build.txt and no planted-build/ gives a context with no
    build directory.  A plant that holds utils/registry.txt gives the
    thresholds of that registry.  The root of the caches is the directory
    cache/ of the copy, also when the plant has none.  The context then does
    not read the caches of the host.

    Raises:
        NotApplicable: If build.txt or the planted registry does not obey its rows
    """
    meta_runner.copy_plant(plant, scratch)
    build: Path | None = None
    planted = Planted()
    if (scratch / COPIED_BUILD).is_dir():
        shutil.copytree(scratch / COPIED_BUILD, scratch / BUILD, dirs_exist_ok=True)
        build = scratch / BUILD
    if (scratch / BUILD_FILE).is_file():
        build = scratch / BUILD
        try:
            planted = read_rows(scratch / BUILD_FILE, scratch, build)
        except PlantError as problem:
            raise meta_runner.NotApplicable(f"the planted build does not read: {problem}") from None
        write_build(planted, scratch, build)
    registry_file = scratch / rule_registry.REGISTRY_PATH
    try:
        registry = rule_registry.load(registry_file) if registry_file.is_file() else None
    except rows_grammar.RowsError as problem:
        raise meta_runner.NotApplicable(f"the planted registry does not read: {problem}") from None
    configuration = None
    if planted.kind is not None:
        configuration = build_config.BuildConfiguration(planted.kind, ((build_config.KIND_KEY, planted.kind),))
    return BuildContext(
        scratch,
        build,
        cache_root=scratch / CACHES,
        cache_limits=planted.limits or None,
        registry=registry,
        configuration=configuration,
    )
