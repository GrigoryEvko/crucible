"""The run of the gate mutants of a header against the tests that can witness them.

THE MUTANTS
    A mutant weakens one gate (assay.mutation.gates).  A test that passes on
    the header and fails on the mutant kills the mutant.  A mutant that no
    test kills shows a gate with no witness.  The run writes each mutant into
    the header of the source tree of the build, and it puts the header back
    after each mutant.  The rule gate-mutants
    (assay.guard.engines.buildsys.gate_mutants) runs it on a configured
    export of the tree, and not on the tree.  The run uses the cmake and the
    ctest that the CMake cache of the build names (assay.setup.pins).

THE CANDIDATES
    The candidates of a header are in three tiers:
    1. Each negative fixture whose compile reads the header
    2. Each test source that reads the header, with -fsyntax-only, for a
       static_assert in a test
    3. Each test executable that reads the header, which the run builds and
       runs, for a contract that only a death test calls.
    In each tier, a candidate that names the gated entity comes first.  Then
    a candidate that names a declaration of the header that uses the entity
    comes, the nearest first.  A fixture usually calls the door, and not the
    concept that the door checks.  Tier 3 builds with ninja in the build
    directory, so it runs only for a contract gate that tiers 1 and 2 do not
    kill.  The program checks a contract when it runs.  Each other gate
    refuses at compile time, and a compile witnesses it.  The run first
    records the result of each candidate on the header with no mutant.  A
    candidate that fails on that header cannot kill a mutant.  The command of
    a test can start with the test launcher `loop test-step`, and the
    candidate is then the program behind the launcher.

THE PROBE
    A mutant whose header no longer compiles alone gets its verdict from the
    first error of a unit that only includes the header (probe_verdict()).
    A refusal by a check of the header kills the mutant.  Each other error
    makes it invalid, because each test that reads the header then fails for
    a reason that is not the gate.  An invalid mutant leaves its gate with no
    test.  A header that does not compile alone stops the run before its
    first mutant, because no probe can then tell a mutant that does not parse
    from a killed one.

Complexity: one pass over the candidates of a header with no mutant, then up
to one pass for each mutant, in batches of `jobs` compiles that stop at the
first kill.
"""

from __future__ import annotations

import dataclasses
import json
import re
import shutil
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import proc
from assay.core.paths import LOOP, REPO_ROOT
from assay.loop import teststep as test_launcher
from assay.mutation import gates as gate_finder
from assay.readers import cmake, compdb, sarif, tsast
from assay.readers import ctest as ctest_reader
from assay.setup import pins as cmake_pin

RULE = "gate-mutants"
# The text between the entity and the gate text in the entity cell of a ledger row.
ENTITY_DIVIDER = ": "
# A negative test runs `python3 -S SRC/utils/bin/loop fixture [--warnings-dir DIR] BUILD SOURCE NAME REGEX...`.
# SRC is the source tree of the build, which can be an export of the tree.  The run finds the master by the end
# of its path.
FIXTURE_MASTER = LOOP.relative_to(REPO_ROOT).as_posix()
FIXTURE_SUBCOMMAND = "fixture"
DEP_FLAGS_WITH_VALUE = ("-o", "-MF", "-MT", "-MQ")
DEP_FLAGS = ("-c", "-MD", "-MMD")
TIMEOUT_S = 600
# The builder of tier 3.  A missing ninja makes every run candidate fail its
# baseline, so the tier drops out and is never counted as a kill.
NINJA = shutil.which("ninja") or "ninja"


class RunError(Exception):
    """The run cannot read an input of the build, or a header that it names does not compile alone."""


@dataclass
class Candidate:
    """One check that can witness a gate: a negative fixture, a syntax-only compile or a test run."""

    name: str
    kind: str                 # "neg", "syntax" or "run"
    argv: list[str]
    cwd: str
    env: dict[str, str] = field(default_factory=dict)
    source: str = ""          # the file whose text is searched for the entity name
    headers: frozenset[str] = frozenset()
    compile_argv: list[str] = field(default_factory=list)  # the source's compile, without output flags
    target: str = ""          # for "run": the ninja target that builds argv[0], relative to build_dir
    build_dir: str = ""       # for "run": the directory that holds build.ninja


@dataclass
class Outcome:
    """The verdict on one mutant."""

    key: str
    header: str
    kind: str
    line: int
    entity: str
    original: str
    status: str               # killed, survived, survived-partial, invalid
    killer: str = ""
    tried: int = 0
    candidates: int = 0


def _strip_output(argv: list[str]) -> list[str]:
    """A compile command without its output, object and dependency-file flags."""
    out: list[str] = []
    skip = False
    for arg in argv:
        if skip:
            skip = False
            continue
        if arg in DEP_FLAGS_WITH_VALUE:
            skip = True
            continue
        if arg in DEP_FLAGS or any(arg.startswith(flag) and arg != flag for flag in ("-MF", "-MT", "-MQ")):
            continue
        out.append(arg)
    return out


def _entry_argv(entry: compdb.CompileEntry) -> list[str]:
    """The argument list of one compile-database entry."""
    return list(entry.argv())


def _run(argv: list[str], cwd: str, env: dict[str, str], timeout: int = TIMEOUT_S) -> tuple[bool, str]:
    """Run one check and say whether it passed, with the tail of its output."""
    try:
        completed = proc.run(proc.Command(argv, timeout_s=timeout, cwd=cwd, env=proc.environment(env)))
    except proc.TimedOut:
        return False, "timeout"
    return completed.exit == 0, (completed.out + completed.err)[-2000:]


# ── the dependency map ─────────────────────────────────────────────────


def _headers_of(entry: compdb.CompileEntry, src_root: Path) -> tuple[str, list[str]]:
    """The file of one compile entry and every header under the source root that it reaches.

    Raises:
        proc.ToolFailed: If the preprocessor does not exit with 0, because its list of headers is then not complete
    """
    argv = _strip_output(_entry_argv(entry)) + ["-M", "-MG"]
    completed = proc.require(proc.run(proc.Command(argv, timeout_s=TIMEOUT_S, cwd=entry.directory)))
    text = completed.out.replace("\\\n", " ")
    deps = text.split(":", 1)[1].split() if ":" in text else []
    headers: list[str] = []
    for dep in deps:
        path = Path(dep)
        if not path.is_absolute():
            path = Path(entry.directory) / path
        try:
            headers.append(str(path.resolve().relative_to(src_root)))
        except ValueError:
            continue
    return str(Path(entry.file).resolve()), headers


def header_sets(build: Path, src_root: Path, jobs: int) -> dict[str, list[str]]:
    """Return the headers of include/ that each compile entry of a build reads, by its file, from the preprocessor.

    Complexity: one run of the preprocessor for each compile entry, with `jobs` runs at a time.

    Raises:
        RunError: If the compile database cannot be read, or a run of the preprocessor fails
    """
    try:
        entries = compdb.entries(build)
    except compdb.CompdbError as error:
        raise RunError(str(error)) from None
    result: dict[str, list[str]] = {}
    try:
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            for file, headers in pool.map(lambda entry: _headers_of(entry, src_root), entries):
                result[file] = sorted(header for header in headers if header.startswith("include/"))
    except proc.ToolFailed as error:
        raise RunError(f"the header set of a compile cannot be read: {error}") from None
    return result


# ── the candidates ─────────────────────────────────────────────────────


# CMake evaluates the CTestTestfile.cmake tree with these three commands
# replaced.  add_test writes one JSON array per test (its name, then its
# command), subdirs follows the tree, and set_tests_properties does nothing.
# Each argument is read by its index, so a `;` inside one stays in it.  A
# subdirectory with no test file is skipped, as ctest skips it.
CTEST_SHIM = r"""
function(_crucible_json_string out value)
  string(REPLACE "\\" "\\\\" value "${value}")
  string(REPLACE "\"" "\\\"" value "${value}")
  string(REPLACE "\n" "\\n" value "${value}")
  string(REPLACE "\t" "\\t" value "${value}")
  set(${out} "\"${value}\"" PARENT_SCOPE)
endfunction()
function(add_test)
  set(line "")
  math(EXPR last "${ARGC} - 1")
  foreach(index RANGE 0 ${last})
    _crucible_json_string(item "${ARGV${index}}")
    if(line STREQUAL "")
      set(line "${item}")
    else()
      string(APPEND line ",${item}")
    endif()
  endforeach()
  file(APPEND "${CRUCIBLE_TESTS_OUT}" "[${line}]\n")
endfunction()
function(set_tests_properties)
endfunction()
macro(subdirs)
  foreach(crucible_subdir ${ARGN})
    include("${CMAKE_CURRENT_LIST_DIR}/${crucible_subdir}/CTestTestfile.cmake" OPTIONAL)
  endforeach()
endmacro()
include("${CRUCIBLE_TESTS_FILE}")
"""


def _build_program(build: Path, name: str) -> str:
    """The cmake or the ctest that the CMake cache of a configured build names (utils/assay/setup/pins.py).

    A cmake or a ctest of PATH can have another version than the one that
    configured the build, so the runner does not look in PATH."""
    try:
        return cmake_pin.configured_program(build, name)
    except cmake_pin.PinError as error:
        raise RunError(str(error)) from None


def _ctest_commands(build: Path) -> dict[str, list[str]]:
    """The command of each test, from CMake's own evaluation of the CTestTestfile.cmake tree.

    ctest --show-only reports no command for a test whose executable is not
    built yet, and the run tier must build that executable first, so the
    files that ctest reads are evaluated here with a shim.
    Complexity: linear in the size of the test files."""
    with tempfile.TemporaryDirectory(prefix="mutation-ctest-") as tmp_name:
        shim = Path(tmp_name) / "shim.cmake"
        listing = Path(tmp_name) / "tests.jsonl"
        shim.write_text(CTEST_SHIM)
        listing.write_text("")
        proc.require(proc.run(proc.Command([_build_program(build, "cmake"),
                                            f"-DCRUCIBLE_TESTS_FILE={build / 'CTestTestfile.cmake'}",
                                            f"-DCRUCIBLE_TESTS_OUT={listing}", "-P", str(shim)],
                                           timeout_s=proc.TOOL_S)))
        commands: dict[str, list[str]] = {}
        for line in listing.read_text().splitlines():
            name, *command = json.loads(line)
            if command and command[0] != "NOT_AVAILABLE":
                commands.setdefault(name, command)
    return commands


def _target_sources(build: Path) -> dict[str, list[str]]:
    """Each artifact of the build, as an absolute path, with the absolute paths of its sources.

    The code model of the CMake file API gives the targets (assay.readers.cmake).
    A build of the tree has the reply, because the root CMakeLists.txt gives
    its query.  A scratch project writes the query with cmake.request() before
    its configure.
    Complexity: linear in the size of the target model."""
    try:
        model = cmake.codemodel(build)
    except cmake.CmakeError as error:
        raise RunError(str(error)) from None
    sources: dict[str, list[str]] = {}
    for target in model.targets:
        files = [str(Path(source.path).resolve()) for source in target.sources]
        for artifact in target.artifacts:
            sources[str(Path(artifact).resolve())] = files
    return sources


def _fixture_word(command: list[str]) -> int | None:
    """Return the index of the word `fixture` after the loop master in a test command, or None.

    Complexity: linear in the length of the command."""
    return next((index + 1 for index, part in enumerate(command[:-1])
                 if part.endswith(FIXTURE_MASTER) and command[index + 1] == FIXTURE_SUBCOMMAND), None)


def load_candidates(build: Path, deps: dict[str, list[str]]) -> list[Candidate]:
    """Return the negative fixtures, the test sources and the test executables of a configured build, as candidates.

    The command of a test that starts with the test launcher gives the program behind the launcher
    (THE CANDIDATES).

    Raises:
        RunError: If the tests, the compile database or the targets of the build cannot be read
    """
    try:
        tests = ctest_reader.tests(build)
        entries = compdb.entries(build)
    except (ctest_reader.CtestError, compdb.CompdbError) as error:
        raise RunError(str(error)) from None
    declared = _ctest_commands(build)
    by_file = {str(Path(entry.file).resolve()): entry for entry in entries}
    artifacts = _target_sources(build)
    candidates: list[Candidate] = []
    for test in tests:
        command = test_launcher.launched(list(test.command) or declared.get(test.name, []))
        env: dict[str, str] = {}
        for item in test.words_of("ENVIRONMENT"):
            key, _, value = item.partition("=")
            env[key] = value
        fixture_at = _fixture_word(command)
        if fixture_at is not None:
            # The fixture subcommand takes `[--warnings-dir DIR] <build-dir> <source> ...`.
            arguments = command[fixture_at + 1:]
            if arguments[:1] == ["--warnings-dir"]:
                arguments = arguments[2:]
            source = str(Path(arguments[1]).resolve())
            entry = by_file.get(source)
            compile_argv = _strip_output(_entry_argv(entry)) if entry else []
            candidates.append(Candidate(test.name, "neg", command, str(build), env, source,
                                        frozenset(deps.get(source, [])), compile_argv))
        elif command and str(Path(command[0]).resolve()) in artifacts:
            test_sources: list[str] = []
            for source in artifacts[str(Path(command[0]).resolve())]:
                entry = by_file.get(source)
                if entry is None or "/test/" not in source:
                    continue
                test_sources.append(source)
                compile_argv = _strip_output(_entry_argv(entry))
                candidates.append(Candidate(f"syntax:{Path(source).name}", "syntax",
                                            compile_argv + ["-fsyntax-only"], entry.directory,
                                            {}, source, frozenset(deps.get(source, [])), compile_argv))
            executable = Path(command[0]).resolve()
            if test_sources and executable.is_relative_to(build):
                workdir = next(iter(test.words_of("WORKING_DIRECTORY")), str(build))
                reached = frozenset(h for source in test_sources for h in deps.get(source, []))
                candidates.append(Candidate(f"run:{test.name}", "run", command, workdir, env, test_sources[0],
                                            reached, target=str(executable.relative_to(build)),
                                            build_dir=str(build)))
    unique: dict[str, Candidate] = {}
    for candidate in candidates:
        unique.setdefault(candidate.name, candidate)
    return list(unique.values())


CLOSURE_LIMIT = 24
# Words of a gate's text that say nothing about which test reaches it.
KEYWORDS = frozenset({"const", "constexpr", "decltype", "false", "noexcept", "nullptr", "requires", "sizeof",
                      "static_cast", "template", "true", "typename"})


def _closure(entity: str, users: dict[str, list[str]]) -> list[str]:
    """The entity, then the declarations that use it, then their users, nearest first.

    Complexity: linear in the number of use edges it visits, and it stops at
    CLOSURE_LIMIT names."""
    if not entity:
        return []
    order = [entity]
    frontier = [entity]
    while frontier and len(order) < CLOSURE_LIMIT:
        following: list[str] = []
        for name in frontier:
            for user in users.get(name, []):
                if user not in order and len(order) < CLOSURE_LIMIT:
                    order.append(user)
                    following.append(user)
        frontier = following
    return order


# The node types whose text is a name that a test source spells in code.
SPELLED_NAMES = ("identifier", "type_identifier", "field_identifier", "namespace_identifier", "operator_name")


def _spelled_names(sources: list[str], cache: dict[str, frozenset[str]]) -> None:
    """Add to cache the names that each new source spells in code, from one parse of all of them.

    A name inside a comment or a string is no node of these types, so it
    does not count.  An operator name is kept in the canonical spelling of
    tsast.spelled, which is the spelling of the gate entity: `operator new[]`.
    Complexity: one kit run over the sources that the cache does not hold."""
    fresh = sorted({source for source in sources if source not in cache})
    readable = [source for source in fresh if Path(source).is_file()]
    for source in set(fresh) - set(readable):
        cache[source] = frozenset()
    for tree in tsast.parse([Path(source) for source in readable], strict=False):
        cache[str(tree.path)] = frozenset(tsast.spelled(node) if node.type == "operator_name" else node.text
                                          for node in tree.find(*SPELLED_NAMES))


def _rank(candidate: Candidate, names: list[str], extra: list[str],
          cache: dict[str, frozenset[str]]) -> tuple[int, int]:
    """How near the candidate's source comes to the gate, as a sort key.

    The first part is the position in names of the nearest name the source
    uses, or len(names).  The second part breaks ties: minus the number of
    distinct names from names and extra that the source uses.  A death test
    of a contract names the words of the contract's condition."""
    if not names:
        return 0, 0
    spelled = cache.get(candidate.source, frozenset())
    used = {word for word in names + extra if word in spelled}
    position = {name: index for index, name in enumerate(names)}
    return min((position[word] for word in used if word in position), default=len(names)), -len(used)


def _build_and_run(batch: list[Candidate], jobs: int, ninja_program: str = NINJA) -> list[tuple[Candidate, bool]]:
    """Build the executables of run candidates with one ninja call, then run each one.

    The old executable is removed first, so a target that fails to build on
    the mutant counts as a failed test and never runs a stale binary.  One
    ninja call per build dir, because two ninja processes in one build dir
    race on its logs."""
    by_dir: dict[str, list[Candidate]] = {}
    for candidate in batch:
        by_dir.setdefault(candidate.build_dir, []).append(candidate)
    for build_dir, group in by_dir.items():
        for candidate in group:
            (Path(build_dir) / candidate.target).unlink(missing_ok=True)
        _run([ninja_program, "-C", build_dir, f"-j{jobs}", "-k", "0"] + sorted({c.target for c in group}),
             build_dir, {}, timeout=6 * TIMEOUT_S)

    def run_one(candidate: Candidate) -> bool:
        if not (Path(candidate.build_dir) / candidate.target).exists():
            return False
        return _run(candidate.argv, candidate.cwd, candidate.env)[0]

    with ThreadPoolExecutor(max_workers=jobs) as pool:
        return list(zip(batch, pool.map(run_one, batch)))


# ── one header ─────────────────────────────────────────────────────────


PARSE_ERROR = re.compile(r"expected [^\n]* before|expected primary-expression|expected unqualified-id|stray ")
# The refusals a check of the header itself makes.  A probe whose first
# error is none of them failed for another reason, and it witnesses
# nothing.  Two examples: a mutant that joined two tokens into a name
# nobody declared, and a mutant of one declaration whose redeclaration
# still carries the gate, which the compiler refuses as a redeclaration
# with different constraints.
SELF_TEST_REFUSAL = re.compile(r"static assertion failed|non-constant condition for static assertion|"
                               r"constraints not satisfied|template constraint failure|use of deleted function|"
                               r"no matching function for call|is ambiguous|is private within this context|"
                               r"is not a constant expression|call to non-.constexpr. function")


def probe_verdict(log: sarif.Log | None) -> str:
    """Return the verdict on a mutant whose header alone no longer compiles, from the SARIF log of the probe.

    Only the first error decides, because each later error can follow from
    it.  A refusal by one of the checks of the header kills the mutant.  A
    parse error in a record, a first error that no check makes, or a probe
    with no log makes the mutant invalid.
    """
    records = sarif.records(log) if log is not None else []
    first = next((record for record in records if record.is_error), None)
    if first is None or any(PARSE_ERROR.search(record.message) for record in records):
        return "invalid"
    return "killed" if SELF_TEST_REFUSAL.search(first.message) else "invalid"


def _probe(header_abs: Path, flags_from: Candidate | None,
           compiler_argv: list[str] | None) -> tuple[bool, sarif.Log | None]:
    """Compile a unit that only includes the header, and return whether it compiled, with the records of its log.

    The verdict reads the errors from the SARIF log of the compile.  The log
    is None when the compile runs longer than its limit or writes no log that
    the reader reads."""
    with tempfile.TemporaryDirectory() as tmp:
        probe = Path(tmp) / "probe.cpp"
        probe.write_text(f'#include "{header_abs}"\n')
        log_flag = f"{sarif.SARIF_FLAG}{Path(tmp) / 'probe.sarif'}"
        if compiler_argv is not None:
            argv = compiler_argv + ["-fsyntax-only", "-fdiagnostics-color=never", log_flag, str(probe)]
            cwd = tmp
        else:
            assert flags_from is not None
            argv = [a for a in flags_from.compile_argv if Path(a).resolve() != Path(flags_from.source)
                    and not a.endswith(".cpp")]
            argv = argv + ["-fsyntax-only", "-fdiagnostics-color=never", log_flag, str(probe)]
            cwd = flags_from.cwd if flags_from.kind == "syntax" else str(Path(flags_from.source).parent)
        try:
            completed = proc.run(proc.Command(argv, timeout_s=TIMEOUT_S, cwd=cwd))
        except proc.TimedOut:
            return False, None
        try:
            log: sarif.Log | None = sarif.read(Path(tmp) / "probe.sarif")
        except (OSError, sarif.SarifError):
            log = None
        return completed.exit == 0, log


def run_header(header: str, src_root: Path, candidates: list[Candidate], jobs: int, neg_cap: int,
               syntax_cap: int, run_cap: int = 4, probe_argv: list[str] | None = None,
               only_lines: set[int] | None = None, ninja_program: str = NINJA) -> list[Outcome]:
    """Every mutant of one header, each with its verdict.

    With only_lines, only the gates on those lines run: this re-checks the
    survivors of an earlier run with larger caps.  The header in the source
    root is restored after each mutant, also when the run stops on an error.
    ninja_program builds the executables of tier 3.

    Raises:
        RunError: If the header does not compile alone (THE PROBE)
    """
    header_abs = src_root / header
    original = header_abs.read_bytes()
    found = gate_finder.find_gates(header_abs, src_root)
    found = [dataclasses.replace(gate, header=header) for gate in found
             if only_lines is None or gate.line in only_lines]
    users = gate_finder.entity_users(header_abs)
    reach = [c for c in candidates if header in c.headers]
    name_cache: dict[str, frozenset[str]] = {}
    _spelled_names([c.source for c in reach if c.source], name_cache)

    def plan(gate: gate_finder.Gate) -> tuple[list[Candidate], list[Candidate], bool]:
        """The compile checks and the run checks for one gate, and whether a cap cut them.

        A test reaches an operator through the class that declares it, and it
        rarely spells the operator's name, so the class follows the closure
        of an operator entity."""
        names = _closure(gate.entity, users)
        if gate.entity.startswith("operator") and gate.owner and gate.owner not in names:
            names.append(gate.owner)
        extra = [word for word in gate.words if len(word) > 3 and word not in KEYWORDS]

        def ranked(kind: str) -> list[tuple[tuple[int, int], Candidate]]:
            rows = [(_rank(c, names, extra, name_cache), c) for c in reach if c.kind == kind]
            return sorted(rows, key=lambda row: row[0])

        negs = ranked("neg")
        named = [c for rank, c in negs if rank[0] == 0]
        others = [c for rank, c in negs if rank[0] != 0]
        syntax = [c for rank, c in ranked("syntax") if rank[0] < len(names)]
        runs = [c for rank, c in ranked("run") if rank[0] < len(names)] if gate.kind == "contract" else []
        capped = len(others) > neg_cap or len(syntax) > syntax_cap or len(runs) > run_cap
        return named + others[:neg_cap] + syntax[:syntax_cap], runs[:run_cap], capped

    everything: dict[str, Candidate] = {}
    for gate in found:
        compiles, runs, _ = plan(gate)
        for candidate in compiles + runs:
            everything[candidate.name] = candidate

    def check_all(batch: list[Candidate]) -> list[tuple[Candidate, bool]]:
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            return list(zip(batch, pool.map(lambda c: _run(c.argv, c.cwd, c.env)[0], batch)))

    baseline = {c.name: ok for c, ok in check_all([c for c in everything.values() if c.kind != "run"])}
    baseline.update({c.name: ok for c, ok in _build_and_run([c for c in everything.values() if c.kind == "run"],
                                                            jobs, ninja_program)})
    probe_from = next((c for c in reach if c.kind == "syntax" and c.compile_argv), None) or next(
        (c for c in reach if c.compile_argv), None)
    base_probe = _probe(header_abs, probe_from, probe_argv)[0] if (probe_from or probe_argv) else False
    if (probe_from or probe_argv) and not base_probe:
        # Without a probe that passes on the original, a mutant that does
        # not parse fails every check and would count as killed.
        raise RunError(f"{header} does not compile on its own, so the run cannot tell a mutant that "
                       f"does not parse from a killed one.  Make the header self-contained first.")

    outcomes: list[Outcome] = []
    try:
        for gate in found:
            chosen, runs, capped = plan(gate)
            chosen = [c for c in chosen if baseline.get(c.name)]
            runs = [c for c in runs if baseline.get(c.name)]
            outcome = Outcome(gate.key, header, gate.kind, gate.line, gate.entity, gate.original[:200],
                              "survived", candidates=len(chosen) + len(runs))
            header_abs.write_bytes(gate_finder.apply(original, gate))
            probe_ok, probe_log = _probe(header_abs, probe_from, probe_argv) if base_probe else (True, None)
            if not probe_ok:
                # The header alone no longer compiles.  A refusal by one of
                # the header's own checks is a witness of the gate.
                outcome.status = probe_verdict(probe_log)
                if outcome.status == "killed":
                    outcome.killer = "header-self-test"
            else:
                batches = [(chosen[start:start + jobs], check_all) for start in range(0, len(chosen), jobs)]
                if runs:
                    batches.append((runs, lambda batch: _build_and_run(batch, jobs, ninja_program)))
                for batch, check in batches:
                    results = check(batch)
                    outcome.tried += len(results)
                    failed = [c.name for c, ok in results if not ok]
                    if failed:
                        outcome.status, outcome.killer = "killed", failed[0]
                        break
                else:
                    if capped:
                        outcome.status = "survived-partial"
            header_abs.write_bytes(original)
            outcomes.append(outcome)
            print(f"{outcome.status:16} {header}:{gate.line} {gate.kind} {gate.entity} {outcome.killer}",
                  file=sys.stderr, flush=True)
    finally:
        header_abs.write_bytes(original)
    return outcomes


def exemption_row(header: str, kind: str, entity: str, text: str) -> rule_ledger.Key:
    """Return the key of the ledger row that admits the invalid mutant of one gate."""
    return rule_ledger.Key(header, kind, f"{entity}{ENTITY_DIVIDER}{' '.join(text.split())}")
