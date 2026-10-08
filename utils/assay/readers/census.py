"""census — the translation units of one build, the files that each one reads, and the cost of each one.

A UNIT
    A unit is one compile that the build or the test run does.
      * An object of the target all: an output of the compile database that
        `ninja -t inputs all` lists.  An object that only another target
        makes (a one-line unit of layer_alone) does not count, so the census
        of a build does not depend on the targets that it built after all.
      * A negative fixture: a test of `ctest --show-only=json-v1` whose
        command runs `utils/bin/loop fixture` (fixture_words()).  The driver
        writes the record build/neg-compile/NAME/NAME.inputs after each run
        (utils/assay/loop/fixture/store.py, The record of a run).

THE FILES OF A UNIT
    The files of an object come from the ninja dependency log (`ninja -t
    deps`): the source and each header that its compile read.  A ccache hit
    records its dependencies too.  The files of a fixture come from its
    record.  Each name resolves to its real path, and a unit counts one real
    path one time.

THE COST OF A UNIT
    The cost of an object comes from the record OUTPUT.cost of `loop step`:
    the cost block of a compile, or the last cost that the record of a ccache
    hit keeps.  The cost of a fixture comes from its record.  So the cost of a
    unit is the cost of its last real compile, and a sum over the units is the
    cost of a cold build.  The instructions of a unit are those of its
    compiler run: the record of an object gives them also for a ccache miss
    (utils/assay/core/meter.py, A COMPILE THROUGH CCACHE), and the fixture
    driver runs the compiler directly.

THE TOOLS
    The ninja and ctest of a build are the values CMAKE_MAKE_PROGRAM and
    CMAKE_CTEST_COMMAND of its CMakeCache.txt.

FAILURES
    read_census() raises NotApplicable when an input does not exist by the
    design of the build: the generator is not ninja, the build made only some
    targets, or no fixture ran in the build directory.  It raises CensusError
    when an input exists and cannot be read, or when only some fixtures have a
    record.
"""

from __future__ import annotations

import json
import os
from collections import Counter
from collections.abc import Iterable
from dataclasses import dataclass, field
from pathlib import Path

from assay.core import meter as cost_meter
from assay.core import proc
from assay.readers import compdb

# The command of a negative fixture runs the master loop with the subcommand fixture.
FIXTURE_MASTER = "utils/bin/loop"
FIXTURE_SUBCOMMAND = "fixture"
FIXTURE_DIR = "neg-compile"
INPUTS_SUFFIX = ".inputs"
INPUTS_FORMAT = 1
RECORD_SUFFIX = ".cost"
# The number of names that a message lists before it gives only the count.
SHOWN_NAMES = 5


class NotApplicable(Exception):
    """An input of the census does not exist by the design of the build."""


class CensusError(Exception):
    """An input of the census exists and cannot be read, or it is not whole."""


@dataclass(frozen=True, slots=True)
class Cost:
    """The CPU time and the user instructions of the compiler run of the last real compile of a unit, each None when
    unknown."""

    cpu_s: float | None = None
    instructions: int | None = None


@dataclass(slots=True)
class Unit:
    """One compile of the build or of the test run.

    `kind` is "object" or "fixture".  `item` is the object relative to the
    build directory, or the name of the fixture.  `files` holds the real path
    of each file that the compile read, when the census reads the files.
    """

    kind: str
    item: str
    source: str
    cost: Cost
    files: list[str] = field(default_factory=list)


@dataclass(slots=True)
class Census:
    """The units of one build, and the size of each file that a unit reads."""

    build_dir: Path
    units: list[Unit]
    sizes: dict[str, int]

    def unit_bytes(self, unit: Unit) -> int:
        """Return the bytes of the files that one unit reads."""
        return sum(self.sizes[path] for path in unit.files)

    def readers(self) -> Counter[str]:
        """Return the number of units that read each file.

        Complexity: linear in the total length of the file lists.
        """
        counts: Counter[str] = Counter()
        for unit in self.units:
            counts.update(unit.files)
        return counts


def shown(path: Path | str, root: Path) -> str:
    """Return a path relative to the root when it is inside the root, else the path itself."""
    resolved = Path(path)
    return str(resolved.relative_to(root)) if resolved.is_relative_to(root) else str(resolved)


def names_text(names: Iterable[str]) -> str:
    """Return the first names of a list and the count of the others, as words for a message."""
    listed = sorted(names)
    head = ", ".join(listed[:SHOWN_NAMES])
    rest = len(listed) - SHOWN_NAMES
    return head + (f" and {rest} more" if rest > 0 else "")


def cache_value(build_dir: Path, name: str) -> str | None:
    """Return the value of one entry of the CMakeCache.txt of a build, or None."""
    prefix = f"{name}:"
    try:
        with open(build_dir / "CMakeCache.txt", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                if line.startswith(prefix) and "=" in line:
                    return line.split("=", 1)[1].rstrip("\n")
    except OSError:
        return None
    return None


def tools_of(build_dir: Path) -> tuple[str, str | None]:
    """Return the ninja and the ctest of a build.  The ctest is None when the cache names none.

    The census runs no ctest of PATH, because PATH can hold a ctest of another
    version (utils/assay/setup/pins.py).

    Raises:
        NotApplicable: If the generator of the build is not ninja
        CensusError: If the build has no CMakeCache.txt
    """
    generator = cache_value(build_dir, "CMAKE_GENERATOR")
    if generator is None:
        raise CensusError(f"{build_dir}/CMakeCache.txt cannot be read.  Configure the build directory with a preset")
    if "Ninja" not in generator:
        raise NotApplicable(f"the generator of the build is {generator}, and only ninja writes a dependency log")
    ninja = cache_value(build_dir, "CMAKE_MAKE_PROGRAM") or "ninja"
    return ninja, cache_value(build_dir, "CMAKE_CTEST_COMMAND")


def run_tool(command: list[str]) -> str:
    """Run one tool of the build and return its standard output.

    A byte of the output that is not UTF-8 stays as a surrogate escape, so a
    path in the output names its file.

    Raises:
        CensusError: If the tool cannot start, runs past its limit, or fails
    """
    try:
        result = proc.run(proc.Command(command, timeout_s=proc.TOOL_S))
    except proc.CannotStart as problem:
        raise CensusError(f"{command[0]} cannot start: {problem.error}") from None
    except proc.TimedOut:
        raise CensusError(f"{' '.join(command[:4])} ran past its limit of {proc.TOOL_S:g} s") from None
    if result.exit != 0:
        raise CensusError(f"{' '.join(command[:4])} exited {result.exit}: {result.err.strip()[:300]}")
    return proc.decoded(result.stdout, "surrogateescape")


def objects_of_all(build_dir: Path, ninja: str, root: Path) -> list[tuple[Path, str]]:
    """Return each object of the target all, with its source as a message shows it.

    Complexity: linear in the rows of the compile database and the inputs of all.

    Raises:
        NotApplicable: If an object of all does not exist, because the build made only some targets
        CensusError: If the compile database or the inputs of all cannot be read
    """
    database = compdb.database_file(build_dir)
    try:
        compiles = compdb.entries(build_dir)
    except compdb.CompdbError as problem:
        raise CensusError(f"the compile database cannot be read: {problem}") from None
    inputs = {os.path.normpath(os.path.join(build_dir, name))
              for name in run_tool([ninja, "-C", str(build_dir), "-t", "inputs", "all"]).split()}
    objects: dict[str, str] = {}
    for compile_entry in compiles:
        if compile_entry.output is None:
            raise CensusError(f"the row of {compile_entry.file} in {database} has no output field.  CMake 3.20 or "
                              f"a subsequent version writes it")
        output = str(compile_entry.output)
        if output in inputs:
            objects[output] = shown(os.path.normpath(compile_entry.file), root)
    if not objects:
        raise CensusError(f"no row of {database} is an input of the target all")
    missing = [path for path in objects if not os.path.isfile(path)]
    if missing:
        raise NotApplicable(f"{len(missing)} of the {len(objects)} objects of the target all do not exist, so the "
                            f"build made only some targets: {names_text(shown(path, build_dir) for path in missing)}")
    return [(Path(path), source) for path, source in sorted(objects.items())]


def parse_dependencies(output: str) -> tuple[dict[str, list[str]], dict[str, str]]:
    """Read the output of `ninja -t deps`: the files of each target and the state of its list.

    The state is VALID, MISSING (the log holds no list of the target) or
    STALE (the list is older than the output).

    Complexity: linear in the length of the output.

    Args:
        output: The standard output of `ninja -t deps`

    Returns:
        The files of each target, and the state of each target
    """
    lists: dict[str, list[str]] = {}
    states: dict[str, str] = {}
    current: list[str] | None = None
    for line in output.splitlines():
        if not line:
            continue
        if line[0] in " \t":
            if current is not None:
                current.append(line.strip())
            continue
        target, _, rest = line.partition(": ")
        current = []
        lists[target] = current
        states[target] = "VALID" if rest.endswith("(VALID)") else "MISSING" if "deps not found" in rest else "STALE"
    return lists, states


def read_dependencies(build_dir: Path, ninja: str, keys: list[str]) -> dict[str, list[str]]:
    """Read the dependency list of each object from the ninja log.

    Complexity: linear in the length of the log output.

    Raises:
        CensusError: If ninja fails, or the log holds no valid list for an object
    """
    lists, states = parse_dependencies(run_tool([ninja, "-C", str(build_dir), "-t", "deps", *keys]))
    bad = [key for key in keys if states.get(key) != "VALID" or not lists.get(key)]
    if bad:
        raise CensusError(f"the ninja log holds no valid dependency list of {len(bad)} objects: {names_text(bad)}.  "
                          f"Build them again")
    return lists


def object_cost(path: Path) -> Cost:
    """Return the cost of the last real compile of one object, from its record."""
    try:
        record = json.loads(Path(str(path) + RECORD_SUFFIX).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return Cost()
    block = record.get("cost") if isinstance(record, dict) else None
    if not isinstance(block, dict):
        block = record.get("last_cost") if isinstance(record, dict) else None
    if not isinstance(block, dict):
        return Cost()
    cpu = block.get("cpu_s")
    count = block.get(cost_meter.COMPILER_COUNT_KEY)
    return Cost(float(cpu) if isinstance(cpu, (int, float)) and not isinstance(cpu, bool) else None,
                count if isinstance(count, int) and not isinstance(count, bool) else None)


def fixtures_of(build_dir: Path, ctest: str | None) -> list[tuple[str, str]]:
    """Return the name and the source of each negative fixture of the build, from the test list of ctest.

    Raises:
        CensusError: If the cache names no ctest, ctest cannot list the tests, or a fixture command does not have
            the form of the driver
    """
    if ctest is None:
        raise CensusError(f"{build_dir}/CMakeCache.txt has no entry CMAKE_CTEST_COMMAND.  Configure the build "
                          f"directory again")
    text = run_tool([ctest, "--test-dir", str(build_dir), "--show-only=json-v1"])
    try:
        tests = json.loads(text).get("tests", [])
    except (ValueError, AttributeError) as problem:
        raise CensusError(f"the test list of ctest cannot be read ({problem})") from None
    fixtures: list[tuple[str, str]] = []
    for test in tests:
        rest = fixture_words([str(word) for word in test.get("command", [])])
        if rest is None:
            continue
        if rest[:1] == ["--warnings-dir"]:
            rest = rest[2:]
        if len(rest) < 3:
            raise CensusError(f"the command of the test {test.get('name')} runs `loop {FIXTURE_SUBCOMMAND}` with no "
                              f"build directory, source and fixture name")
        fixtures.append((rest[2], rest[1]))
    return fixtures


def fixture_words(command: list[str]) -> list[str] | None:
    """Return the words after `utils/bin/loop fixture` of a test command, or None when the command runs no fixture.

    Complexity: O(n) for n words.

    Args:
        command: The words of the command of one test

    Returns:
        The words that the fixture driver gets, or None
    """
    for index, word in enumerate(command[:-1]):
        if (word == FIXTURE_MASTER or word.endswith(f"/{FIXTURE_MASTER}")) and command[index + 1] == FIXTURE_SUBCOMMAND:
            return command[index + 2:]
    return None


def read_fixture(build_dir: Path, name: str, source: str, root: Path,
                 read_files: bool) -> tuple[Unit | None, list[str] | None, str]:
    """Read the record of one fixture.

    Returns:
        The unit and its file names, or None and None with the reason that the record cannot count ("missing" for
        no record)
    """
    path = build_dir / FIXTURE_DIR / name / f"{name}{INPUTS_SUFFIX}"
    try:
        text = path.read_text(encoding="utf-8", errors="surrogateescape")
    except FileNotFoundError:
        return None, None, "missing"
    except OSError as problem:
        return None, None, f"the record {path} cannot be read ({problem})"
    first, _, body = text.partition("\n")
    try:
        header = json.loads(first)
    except ValueError:
        header = None
    if not isinstance(header, dict) or header.get("format") != INPUTS_FORMAT or header.get("fixture") != name:
        return None, None, f"the record {path} is not a record of format {INPUTS_FORMAT} of {name}"
    if header.get("has_inputs") is not True:
        return None, None, f"the record {path} says that the driver does not know the files of the compile"
    user, system, count = header.get("user_s"), header.get("system_s"), header.get("instructions")
    times = [value for value in (user, system) if isinstance(value, (int, float)) and not isinstance(value, bool)]
    cost = Cost(sum(times) if len(times) == 2 else None,
                count if isinstance(count, int) and not isinstance(count, bool) else None)
    names = body.splitlines() if read_files else None
    if read_files and not names:
        return None, None, f"the record {path} names no file"
    return Unit("fixture", name, shown(source, root), cost), names, ""


def read_census(build_dir: Path, root: Path, read_files: bool = True) -> Census:
    """Read the units of one build, and with read_files the real path and the size of each file that they read.

    Complexity: linear in the total length of the file lists, with one realpath and one stat for each distinct
    name.

    Raises:
        NotApplicable: If an input does not exist by the design of the build
        CensusError: If an input exists and cannot be read, or only some fixtures have a record
    """
    ninja, ctest = tools_of(build_dir)
    objects = objects_of_all(build_dir, ninja, root)
    units: list[Unit] = [Unit("object", shown(path, build_dir), source, object_cost(path))
                         for path, source in objects]
    names_of: list[list[str]] = []
    if read_files:
        lists = read_dependencies(build_dir, ninja, [unit.item for unit in units])
        names_of = [lists[unit.item] for unit in units]
    fixtures = fixtures_of(build_dir, ctest)
    missing: list[str] = []
    problems: list[str] = []
    for name, source in fixtures:
        unit, names, reason = read_fixture(build_dir, name, source, root, read_files)
        if unit is None:
            (missing if reason == "missing" else problems).append(name if reason == "missing" else reason)
            continue
        units.append(unit)
        if names is not None:
            names_of.append(names)
    if fixtures and len(missing) == len(fixtures):
        raise NotApplicable(f"no fixture of the build has a record, because the fixtures did not run in this build "
                            f"directory.  The record is {FIXTURE_DIR}/NAME/NAME{INPUTS_SUFFIX}, and ctest -R '^neg_' "
                            f"writes it")
    if missing:
        raise CensusError(f"{len(missing)} of the {len(fixtures)} fixtures have no record, because they did not run "
                          f"in this build directory: {names_text(missing)}.  Run every fixture, then the check")
    if problems:
        raise CensusError(f"{len(problems)} fixture records cannot count.  The first: {problems[0]}")
    sizes: dict[str, int] = {}
    if read_files:
        resolved: dict[str, str] = {}
        for unit, names in zip(units, names_of, strict=True):
            files: dict[str, None] = {}
            for name in names:
                real = resolved.get(name)
                if real is None:
                    real = os.path.realpath(name if os.path.isabs(name) else os.path.join(build_dir, name))
                    resolved[name] = real
                    if real not in sizes:
                        try:
                            sizes[real] = os.stat(real).st_size
                        except OSError as problem:
                            raise CensusError(f"the file {real}, which the compile of {unit.item} read, cannot be "
                                              f"read ({problem}).  Build the tree and run the fixtures again") from None
                files[real] = None
            unit.files = list(files)
    return Census(build_dir, units, sizes)


def file_key(real: str, root: Path, build_dir: Path) -> str:
    """Return the name of a file in a ledger, which does not depend on the host.

    A file of the build directory is <build>/PATH, a file of the tree is its
    path in the tree, and another file is <system>/PATH after the first
    include/ component of its path, for example <system>/c++/16.2.1/tuple.
    """
    path = Path(real)
    if path.is_relative_to(build_dir):
        return f"<build>/{path.relative_to(build_dir).as_posix()}"
    if path.is_relative_to(root):
        return path.relative_to(root).as_posix()
    _, marker, tail = real.partition("/include/")
    return f"<system>/{tail}" if marker else real
