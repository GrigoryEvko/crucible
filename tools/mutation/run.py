#!/usr/bin/env python3
"""Run the gate mutants of a header against the tests that can witness them.

A mutant weakens one gate (see gates.py).  The mutant is killed when a test
that passes on the original header fails on the mutant.  A mutant that no
such test kills marks a gate with no witness.

The run works on a configured export of the tree, never on the repository:
    run.py deps   --build B --out O        map each compile entry to its headers
    run.py mutate --src S --build B --out O --header H [H ...]
    run.py selftest --compiler CXX         one planted survivor and two kills

Candidates for a header, in this order:
    1. each negative fixture whose compile reaches the header
    2. each test source that reaches the header, compiled with -fsyntax-only,
       so a static_assert in a test counts
    3. each test executable that reaches the header, built and run, so a
       contract that only a death test reaches counts
Within each tier, a candidate that names the gated entity comes first, then
one that names a declaration of the header that uses the entity, nearest
first.  A fixture usually calls the door, not the concept the door checks.
Tier 3 builds with ninja in the build dir, so it runs only for a contract
gate that tiers 1 and 2 leave alive.  A contract is checked when the program
runs.  Every other gate refuses at compile time, and a compile witnesses it.
The run records a baseline of every candidate on the original header first.
A candidate that fails on the original header can never kill a mutant.

A mutant whose header does not compile on its own is recorded as invalid and
never as killed, because every dependent test would then fail for a reason
that says nothing about the gate.  An invalid mutant leaves its gate
untested, so it stops the run after the header that holds it, with the gate
named, unless tools/mutation/invalid-exemptions.txt lists the gate with a
reason.  A header that does not compile on its own stops the run before its
first mutant, because then no probe can tell a malformed mutant from a
killed one.

Complexity: one baseline pass over the candidates of a header, then for each
mutant up to one pass, in batches of --jobs compiles that stop at the first
kill.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from dataclasses import asdict, dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gates as gate_finder  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[2]
INVALID_EXEMPTIONS = Path(__file__).resolve().parent / "invalid-exemptions.txt"
DEP_FLAGS_WITH_VALUE = ("-o", "-MF", "-MT", "-MQ")
DEP_FLAGS = ("-c", "-MD", "-MMD")
TIMEOUT_S = 600
# The builder of tier 3.  A missing ninja makes every run candidate fail its
# baseline, so the tier drops out and is never counted as a kill.
NINJA = shutil.which("ninja") or "ninja"


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


def _entry_argv(entry: dict) -> list[str]:
    """The argument list of one compile-database entry."""
    return list(entry.get("arguments") or shlex.split(entry["command"]))


def _run(argv: list[str], cwd: str, env: dict[str, str], timeout: int = TIMEOUT_S) -> tuple[bool, str]:
    """Run one check and say whether it passed, with the tail of its output."""
    full_env = dict(os.environ)
    full_env.update(env)
    try:
        proc = subprocess.run(argv, cwd=cwd, env=full_env, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return False, "timeout"
    return proc.returncode == 0, (proc.stdout + proc.stderr)[-2000:]


# ── the dependency map ─────────────────────────────────────────────────


def _headers_of(entry: dict, src_root: Path) -> tuple[str, list[str]]:
    """The file of one compile entry and every header under the source root that it reaches."""
    argv = _strip_output(_entry_argv(entry)) + ["-M", "-MG"]
    proc = subprocess.run(argv, cwd=entry["directory"], capture_output=True, text=True, timeout=TIMEOUT_S)
    text = proc.stdout.replace("\\\n", " ")
    deps = text.split(":", 1)[1].split() if ":" in text else []
    headers: list[str] = []
    for dep in deps:
        path = Path(dep)
        if not path.is_absolute():
            path = Path(entry["directory"]) / path
        try:
            headers.append(str(path.resolve().relative_to(src_root)))
        except ValueError:
            continue
    return str(Path(entry["file"]).resolve()), headers


def cmd_deps(args: argparse.Namespace) -> int:
    """Write the header set of every compile entry, computed with the preprocessor."""
    build = Path(args.build).resolve()
    src_root = Path(args.src).resolve()
    entries = json.loads((build / "compile_commands.json").read_text())
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    result: dict[str, list[str]] = {}
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for file, headers in pool.map(lambda e: _headers_of(e, src_root), entries):
            result[file] = sorted(h for h in headers if h.startswith("include/"))
    (out / "deps.json").write_text(json.dumps(result))
    print(f"deps: {len(result)} compile entries", file=sys.stderr)
    return 0


# ── the candidates ─────────────────────────────────────────────────────


CTEST_ADD_TEST = re.compile(r'^add_test\((.*)\)\s*$', re.M)
CTEST_ARG = re.compile(r'"((?:[^"\\]|\\.)*)"')


def _ctest_commands(build: Path) -> dict[str, list[str]]:
    """The command of each test, read from the CTestTestfile.cmake files that ctest itself reads.

    ctest --show-only reports no command for a test whose executable is not
    built yet, and the run tier must build that executable first.
    Complexity: linear in the size of the test files."""
    commands: dict[str, list[str]] = {}
    for testfile in build.rglob("CTestTestfile.cmake"):
        for match in CTEST_ADD_TEST.finditer(testfile.read_text(errors="replace")):
            args = [re.sub(r"\\(.)", r"\1", arg) for arg in CTEST_ARG.findall(match.group(1))]
            if len(args) >= 2 and args[1] != "NOT_AVAILABLE":
                commands.setdefault(args[0], args[1:])
    return commands


def load_candidates(build: Path, deps: dict[str, list[str]]) -> list[Candidate]:
    """Every negative fixture and every test source of the configured build, as candidates."""
    tests = json.loads(subprocess.run(["ctest", "--show-only=json-v1"], cwd=build, capture_output=True,
                                      text=True, check=True).stdout)["tests"]
    declared = _ctest_commands(build)
    entries = json.loads((build / "compile_commands.json").read_text())
    by_file = {str(Path(entry["file"]).resolve()): entry for entry in entries}
    by_target: dict[str, list[dict]] = {}
    for entry in entries:
        match = re.search(r"CMakeFiles/([^/]+)\.dir/", entry.get("output", "") or entry.get("command", ""))
        if match:
            by_target.setdefault(match.group(1), []).append(entry)
    candidates: list[Candidate] = []
    for test in tests:
        command = test.get("command") or declared.get(test["name"], [])
        env: dict[str, str] = {}
        for prop in test.get("properties", []):
            if prop.get("name") == "ENVIRONMENT":
                for item in prop.get("value", []):
                    key, _, value = item.partition("=")
                    env[key] = value
        if any(part.endswith("neg_compile_driver.py") for part in command):
            source = str(Path(command[3]).resolve())
            entry = by_file.get(source)
            compile_argv = _strip_output(_entry_argv(entry)) if entry else []
            candidates.append(Candidate(test["name"], "neg", command, str(build), env, source,
                                        frozenset(deps.get(source, [])), compile_argv))
        elif command and Path(command[0]).name in by_target:
            test_sources: list[str] = []
            for entry in by_target[Path(command[0]).name]:
                source = str(Path(entry["file"]).resolve())
                if "/test/" not in source:
                    continue
                test_sources.append(source)
                compile_argv = _strip_output(_entry_argv(entry))
                candidates.append(Candidate(f"syntax:{Path(source).name}", "syntax",
                                            compile_argv + ["-fsyntax-only"], entry["directory"],
                                            {}, source, frozenset(deps.get(source, [])), compile_argv))
            executable = Path(command[0]).resolve()
            if test_sources and executable.is_relative_to(build):
                workdir = next((prop.get("value") for prop in test.get("properties", [])
                                if prop.get("name") == "WORKING_DIRECTORY"), str(build))
                reached = frozenset(h for source in test_sources for h in deps.get(source, []))
                candidates.append(Candidate(f"run:{test['name']}", "run", command, workdir, env, test_sources[0],
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


def _rank(candidate: Candidate, names: list[str], extra: list[str], cache: dict[str, str]) -> tuple[int, int]:
    """How near the candidate's source comes to the gate, as a sort key.

    The first part is the position in names of the nearest name the source
    uses, or len(names).  The second part breaks ties: minus the number of
    distinct names from names and extra that the source uses.  A death test
    of a contract names the words of the contract's condition."""
    if not names:
        return 0, 0
    if candidate.source not in cache:
        try:
            cache[candidate.source] = Path(candidate.source).read_text(errors="replace")
        except OSError:
            cache[candidate.source] = ""
    words = names + [word for word in extra if word not in names]
    pattern = re.compile(r"\b(" + "|".join(re.escape(word) for word in words) + r")\b")
    used = {m.group(1) for m in pattern.finditer(cache[candidate.source])}
    position = {name: index for index, name in enumerate(names)}
    return min((position[word] for word in used if word in position), default=len(names)), -len(used)


def _build_and_run(batch: list[Candidate], jobs: int) -> list[tuple[Candidate, bool]]:
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
        _run([NINJA, "-C", build_dir, f"-j{jobs}", "-k", "0"] + sorted({c.target for c in group}),
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
FIRST_ERROR = re.compile(r"\berror: (.*)$", re.MULTILINE)


def probe_verdict(output: str) -> str:
    """The verdict on a mutant whose header alone no longer compiles, read from the compiler's output.

    Only the first error decides, because each later error can follow
    from it.  A refusal by one of the header's own checks kills the mutant.
    A parse error anywhere, or a first error that no check makes, says the
    mutant is malformed."""
    first = FIRST_ERROR.search(output)
    if first is None or PARSE_ERROR.search(output):
        return "invalid"
    return "killed" if SELF_TEST_REFUSAL.search(first.group(1)) else "invalid"


def _probe(header_abs: Path, flags_from: Candidate | None, compiler_argv: list[str] | None) -> tuple[bool, str]:
    """Compile a TU that only includes the header, and say whether it compiled, with its whole output.

    The output is uncoloured and whole, because the verdict reads the first
    error in it, and a colour escape or a cut would hide the words."""
    with tempfile.TemporaryDirectory() as tmp:
        probe = Path(tmp) / "probe.cpp"
        probe.write_text(f'#include "{header_abs}"\n')
        if compiler_argv is not None:
            argv = compiler_argv + ["-fsyntax-only", "-fdiagnostics-color=never", str(probe)]
            cwd = tmp
        else:
            assert flags_from is not None
            argv = [a for a in flags_from.compile_argv if Path(a).resolve() != Path(flags_from.source)
                    and not a.endswith(".cpp")]
            argv = argv + ["-fsyntax-only", "-fdiagnostics-color=never", str(probe)]
            cwd = flags_from.cwd if flags_from.kind == "syntax" else str(Path(flags_from.source).parent)
        try:
            proc = subprocess.run(argv, cwd=cwd, capture_output=True, text=True, timeout=TIMEOUT_S, check=False)
        except subprocess.TimeoutExpired:
            return False, "timeout"
        return proc.returncode == 0, proc.stdout + proc.stderr


def run_header(header: str, src_root: Path, candidates: list[Candidate], jobs: int, neg_cap: int,
               syntax_cap: int, run_cap: int = 4, probe_argv: list[str] | None = None,
               only_lines: set[int] | None = None) -> list[Outcome]:
    """Every mutant of one header, each with its verdict.

    With only_lines, only the gates on those lines run: this re-checks the
    survivors of an earlier run with larger caps.  The header in the source
    root is restored after each mutant, also when the run stops on an error."""
    header_abs = src_root / header
    original = header_abs.read_bytes()
    found = gate_finder.find_gates(header_abs, src_root)
    found = [gate_finder.Gate(header, g.kind, g.line, g.start, g.end, g.replacement, g.entity, g.original)
             for g in found if only_lines is None or g.line in only_lines]
    users = gate_finder.entity_users(header_abs)
    reach = [c for c in candidates if header in c.headers]
    text_cache: dict[str, str] = {}

    def plan(gate: gate_finder.Gate) -> tuple[list[Candidate], list[Candidate], bool]:
        """The compile checks and the run checks for one gate, and whether a cap cut them."""
        names = _closure(gate.entity, users)
        extra = [word for word in re.findall(r"[A-Za-z_]\w{3,}", gate.original) if word not in KEYWORDS]

        def ranked(kind: str) -> list[tuple[tuple[int, int], Candidate]]:
            rows = [(_rank(c, names, extra, text_cache), c) for c in reach if c.kind == kind]
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
                                                            jobs)})
    probe_from = next((c for c in reach if c.kind == "syntax" and c.compile_argv), None) or next(
        (c for c in reach if c.compile_argv), None)
    base_probe = _probe(header_abs, probe_from, probe_argv)[0] if (probe_from or probe_argv) else False
    if (probe_from or probe_argv) and not base_probe:
        # Without a probe that passes on the original, a mutant that does
        # not parse fails every check and would count as killed.
        raise SystemExit(f"mutation: {header} does not compile on its own, so the run cannot tell a mutant that "
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
            probe_ok, probe_out = _probe(header_abs, probe_from, probe_argv) if base_probe else (True, "")
            if not probe_ok:
                # The header alone no longer compiles.  A refusal by one of
                # the header's own checks is a witness of the gate.
                outcome.status = probe_verdict(probe_out)
                if outcome.status == "killed":
                    outcome.killer = "header-self-test"
            else:
                batches = [(chosen[start:start + jobs], check_all) for start in range(0, len(chosen), jobs)]
                if runs:
                    batches.append((runs, lambda batch: _build_and_run(batch, jobs)))
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


def gate_key(header: str, kind: str, entity: str, text: str) -> str:
    """The key of one gate in a reviewed list: header, kind, entity and the gate text, whitespace squeezed."""
    return " | ".join((header, kind, entity, " ".join(text.split())))


def load_exemptions(path: Path) -> set[str]:
    """The gate keys of a reviewed list whose rows are `header | kind | entity | text | reason`.

    A row with no reason is refused, because an exemption that says nothing
    cannot be reviewed.  Complexity: linear in the size of the file."""
    keys: set[str] = set()
    for number, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        fields = [field.strip() for field in line.split(" | ")]
        if len(fields) < 5 or not fields[-1]:
            raise SystemExit(f"{path}:{number}: a row needs five fields separated by ' | ', the last a reason")
        keys.add(gate_key(fields[0], fields[1], fields[2], " | ".join(fields[3:-1])))
    return keys


def unexempted_invalid(outcomes: list[Outcome], exempt: set[str]) -> list[Outcome]:
    """The invalid outcomes whose gate the reviewed list does not name."""
    return [o for o in outcomes if o.status == "invalid" and gate_key(o.header, o.kind, o.entity, o.original)
            not in exempt]


def cmd_mutate(args: argparse.Namespace) -> int:
    """Run the mutants of each named header and append one JSON line per mutant.

    The run stops after a header that holds an invalid mutant the reviewed
    list does not name, because that gate went untested."""
    build = Path(args.build).resolve()
    src_root = Path(args.src).resolve()
    out = Path(args.out)
    deps = json.loads((out / "deps.json").read_text())
    candidates = load_candidates(build, deps)
    exempt = load_exemptions(INVALID_EXEMPTIONS)
    only_lines = {int(line) for line in args.lines.split(",")} if args.lines else None
    with (out / args.results).open("a") as sink:
        for header in args.header:
            outcomes = run_header(header, src_root, candidates, args.jobs, args.neg_cap, args.syntax_cap,
                                  run_cap=args.run_cap, only_lines=only_lines)
            for outcome in outcomes:
                sink.write(json.dumps(asdict(outcome)) + "\n")
                sink.flush()
            untested = unexempted_invalid(outcomes, exempt)
            for outcome in untested:
                print(f"mutation: INVALID MUTANT {outcome.header}:{outcome.line} {outcome.kind} {outcome.entity}: "
                      f"{outcome.original!r} does not parse, so the gate went untested.  Fix the mutant in "
                      f"tools/mutation/gates.py, or list the gate in {INVALID_EXEMPTIONS.name} with a reason.",
                      file=sys.stderr)
            if untested:
                return 1
    return 0


# ── the self-test ──────────────────────────────────────────────────────

PLANTED = """#pragma once
#include <concepts>
#include <type_traits>
namespace planted {
template <class T> concept Small = sizeof(T) <= 4;
template <class T> requires Small<T> constexpr int take(T) { return 1; }
template <class T> requires std::integral<T> constexpr int count(T) { return 2; }
template <class T> requires(std::is_integral_v<T>) constexpr int widen(T) { return 3; }
inline int half(int n) { contract_assert(n % 2 == 0); return n / 2; }
}
"""
FIXTURE = """#include "planted.h"
int main() { return planted::take(1.0L); }
"""
# The gate of widen is written `requires(` with no space, so a mutant that
# writes `true` flush against it reads `requirestrue` and does not parse.
WIDEN_FIXTURE = """#include "planted.h"
int main() { return planted::widen(1.5); }
"""
USER = """#include "planted.h"
static_assert(planted::take(1) == 1 && planted::count(1) == 2);
int main() {}
"""
# A death test in miniature: it passes only when the contract of half stops the call.
RUNNER = """#include <contracts>
#include <cstdlib>
#include "planted.h"
void handle_contract_violation(const std::contracts::contract_violation&) noexcept { std::_Exit(0); }
int main() { (void)planted::half(3); return 1; }
"""
NINJA_FILE = """rule cxx
  command = {compiler} -std=c++26 -fcontracts -Iinclude $in -o $out
build run_half: cxx run_half.cpp | include/planted.h
"""


def cmd_selftest(args: argparse.Namespace) -> int:
    """Plant a header with four witnessed gates and one bare gate, and check each verdict.

    One witness is a negative fixture, one a syntax-only compile, and one a
    test that ninja builds and the run executes, so each tier is proved.  A
    fourth gate is written `requires(` with no space, which proves that its
    mutant parses.  The check of the invalid outcomes is then proved on a
    planted invalid outcome, with and without an exemption."""
    global NINJA
    NINJA = args.ninja or NINJA
    with tempfile.TemporaryDirectory() as tmp_name:
        tmp = Path(tmp_name)
        (tmp / "include").mkdir()
        (tmp / "include" / "planted.h").write_text(PLANTED)
        (tmp / "neg_take_large.cpp").write_text(FIXTURE)
        (tmp / "neg_widen_double.cpp").write_text(WIDEN_FIXTURE)
        (tmp / "user.cpp").write_text(USER)
        (tmp / "run_half.cpp").write_text(RUNNER)
        (tmp / "build.ninja").write_text(NINJA_FILE.format(compiler=args.compiler))
        base = [args.compiler, "-std=c++26", "-fcontracts", f"-I{tmp / 'include'}"]
        rows = [{"directory": str(tmp), "file": str(tmp / name), "arguments": base + ["-c", str(tmp / name),
                 "-o", str(tmp / (name + ".o"))]}
                for name in ("neg_take_large.cpp", "neg_widen_double.cpp", "user.cpp")]
        (tmp / "compile_commands.json").write_text(json.dumps(rows))
        driver = REPO_ROOT / "test" / "neg_compile_driver.py"
        headers = frozenset({"include/planted.h"})
        candidates = [
            Candidate("neg_take_large", "neg",
                      [sys.executable, str(driver), str(tmp), str(tmp / "neg_take_large.cpp"), "neg_take_large",
                       "constraints not satisfied", "Small"],
                      str(tmp), {}, str(tmp / "neg_take_large.cpp"), headers),
            Candidate("neg_widen_double", "neg",
                      [sys.executable, str(driver), str(tmp), str(tmp / "neg_widen_double.cpp"), "neg_widen_double",
                       "constraints not satisfied", "is_integral_v"],
                      str(tmp), {}, str(tmp / "neg_widen_double.cpp"), headers),
            Candidate("syntax:user.cpp", "syntax", base + ["-fsyntax-only", str(tmp / "user.cpp")],
                      str(tmp), {}, str(tmp / "user.cpp"), headers),
            Candidate("run:run_half", "run", [str(tmp / "run_half")], str(tmp), {}, str(tmp / "run_half.cpp"),
                      headers, target="run_half", build_dir=str(tmp)),
        ]
        try:
            outcomes = run_header("include/planted.h", tmp, candidates, 2, 100, 100, probe_argv=base)
        except gate_finder.tsast.KitMissing as missing:
            print(f"mutation selftest: SKIP, {missing}", file=sys.stderr)
            return 3
        verdicts = {(o.kind, o.entity): o.status for o in outcomes}
        killers = {(o.kind, o.entity): o.killer for o in outcomes}
        expected = {("concept", "Small"): "killed", ("requires", "take"): "killed",
                    ("requires", "count"): "survived", ("requires", "widen"): "killed",
                    ("contract", "half"): "killed"}
        failures = [f"{k}: expected {v}, got {verdicts.get(k)}" for k, v in expected.items() if verdicts.get(k) != v]
        if killers.get(("contract", "half")) != "run:run_half":
            failures.append(f"the contract of half must fall to the run tier, not {killers.get(('contract', 'half'))!r}")
        if killers.get(("requires", "widen")) != "neg_widen_double":
            failures.append(f"the gate of widen must fall to its fixture, not {killers.get(('requires', 'widen'))!r}")
        if len(outcomes) != len(expected):
            failures.append(f"expected {len(expected)} mutants, got {len(outcomes)}: {sorted(verdicts)}")
        # An invalid outcome stops the run unless an exemption names its gate.
        planted_invalid = Outcome("k", "include/planted.h", "requires", 1, "fused", "(x)", "invalid")
        if unexempted_invalid([planted_invalid], set()) != [planted_invalid]:
            failures.append("an invalid outcome with no exemption was not reported")
        exemption = tmp / "exemptions.txt"
        exemption.write_text("include/planted.h | requires | fused | (x) | a planted reason\n")
        if unexempted_invalid([planted_invalid], load_exemptions(exemption)):
            failures.append("an invalid outcome that an exemption names was still reported")
        # Only a refusal by a check of the header kills a mutant at the probe.
        probe_cases = {"planted.h:3:1: error: static assertion failed: small": "killed",
                       "planted.h:3:1: error: call of overloaded 'take(int)' is ambiguous": "killed",
                       "planted.h:3:1: error: 'requirestrue' does not name a type": "invalid",
                       "planted.h:3:1: error: expected ';' before '}' token": "invalid",
                       "planted.h:3:1: error: redeclaration of 'template<class T>  requires  true struct S' with "
                       "different constraints\nplanted.h:9:1: error: static assertion failed": "invalid"}
        for text, verdict in probe_cases.items():
            if probe_verdict(text) != verdict:
                failures.append(f"the probe output {text!r} must read as {verdict}, not {probe_verdict(text)}")
        if (tmp / "include" / "planted.h").read_text() != PLANTED:
            failures.append("the planted header was not restored")
        for line in failures:
            print(f"mutation selftest: FAIL {line}", file=sys.stderr)
        if not failures:
            print("mutation selftest: one survivor and four kills, as planted, and the invalid check holds")
        return 1 if failures else 0


def _exit_on_term(signum: int, _frame: object) -> None:
    """Turn SIGTERM into SystemExit, so the finally block restores the mutated header."""
    raise SystemExit(128 + signum)


def main() -> int:
    """Parse the command line and run one mode."""
    signal.signal(signal.SIGTERM, _exit_on_term)
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="mode", required=True)
    deps = sub.add_parser("deps")
    deps.add_argument("--src", required=True)
    deps.add_argument("--build", required=True)
    deps.add_argument("--out", required=True)
    deps.add_argument("--jobs", type=int, default=6)
    mutate = sub.add_parser("mutate")
    mutate.add_argument("--src", required=True)
    mutate.add_argument("--build", required=True)
    mutate.add_argument("--out", required=True)
    mutate.add_argument("--header", nargs="+", required=True)
    mutate.add_argument("--jobs", type=int, default=6)
    mutate.add_argument("--neg-cap", type=int, default=80)
    mutate.add_argument("--syntax-cap", type=int, default=12)
    mutate.add_argument("--run-cap", type=int, default=4, help="test executables built and run per mutant")
    mutate.add_argument("--lines", default="", help="comma-separated gate lines to re-check")
    mutate.add_argument("--results", default="results.jsonl", help="the file under --out to append to")
    selftest = sub.add_parser("selftest")
    selftest.add_argument("--compiler", required=True)
    selftest.add_argument("--ninja", default="", help="the ninja binary, when it is not on PATH")
    args = parser.parse_args()
    return {"deps": cmd_deps, "mutate": cmd_mutate, "selftest": cmd_selftest}[args.mode](args)


if __name__ == "__main__":
    raise SystemExit(main())
