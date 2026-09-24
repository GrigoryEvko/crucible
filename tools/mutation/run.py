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
    1. each negative fixture whose compile reaches the header, the fixtures
       that name the gated entity first
    2. each test source that reaches the header and names the entity,
       compiled with -fsyntax-only, so a static_assert in a test counts
The run records a baseline of every candidate on the original header first.
A candidate that fails on the original header can never kill a mutant.

A mutant whose header does not compile on its own is recorded as invalid and
never as killed, because every dependent test would then fail for a reason
that says nothing about the gate.

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
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from dataclasses import asdict, dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gates as gate_finder  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[2]
DEP_FLAGS_WITH_VALUE = ("-o", "-MF", "-MT", "-MQ")
DEP_FLAGS = ("-c", "-MD", "-MMD")
TIMEOUT_S = 600


@dataclass
class Candidate:
    """One check that can witness a gate: a negative fixture or a syntax-only compile."""

    name: str
    kind: str                 # "neg" or "syntax"
    argv: list[str]
    cwd: str
    env: dict[str, str] = field(default_factory=dict)
    source: str = ""          # the file whose text is searched for the entity name
    headers: frozenset[str] = frozenset()
    compile_argv: list[str] = field(default_factory=list)  # the source's compile, without output flags


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


def _run(argv: list[str], cwd: str, env: dict[str, str]) -> tuple[bool, str]:
    """Run one check and say whether it passed, with the tail of its output."""
    full_env = dict(os.environ)
    full_env.update(env)
    try:
        proc = subprocess.run(argv, cwd=cwd, env=full_env, capture_output=True, text=True, timeout=TIMEOUT_S)
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


def load_candidates(build: Path, deps: dict[str, list[str]]) -> list[Candidate]:
    """Every negative fixture and every test source of the configured build, as candidates."""
    tests = json.loads(subprocess.run(["ctest", "--show-only=json-v1"], cwd=build, capture_output=True,
                                      text=True, check=True).stdout)["tests"]
    entries = json.loads((build / "compile_commands.json").read_text())
    by_file = {str(Path(entry["file"]).resolve()): entry for entry in entries}
    by_target: dict[str, list[dict]] = {}
    for entry in entries:
        match = re.search(r"CMakeFiles/([^/]+)\.dir/", entry.get("output", "") or entry.get("command", ""))
        if match:
            by_target.setdefault(match.group(1), []).append(entry)
    candidates: list[Candidate] = []
    for test in tests:
        command = test.get("command") or []
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
            for entry in by_target[Path(command[0]).name]:
                source = str(Path(entry["file"]).resolve())
                if "/test/" not in source:
                    continue
                compile_argv = _strip_output(_entry_argv(entry))
                candidates.append(Candidate(f"syntax:{Path(source).name}", "syntax",
                                            compile_argv + ["-fsyntax-only"], entry["directory"],
                                            {}, source, frozenset(deps.get(source, [])), compile_argv))
    unique: dict[str, Candidate] = {}
    for candidate in candidates:
        unique.setdefault(candidate.name, candidate)
    return list(unique.values())


def _mentions(candidate: Candidate, entity: str, cache: dict[str, str]) -> bool:
    """Say whether the candidate's source names the entity as a whole word."""
    if not entity:
        return False
    if candidate.source not in cache:
        try:
            cache[candidate.source] = Path(candidate.source).read_text(errors="replace")
        except OSError:
            cache[candidate.source] = ""
    return re.search(rf"\b{re.escape(entity)}\b", cache[candidate.source]) is not None


# ── one header ─────────────────────────────────────────────────────────


PARSE_ERROR = re.compile(r"expected [^\n]* before|expected primary-expression|expected unqualified-id|stray ")


def _probe(header_abs: Path, flags_from: Candidate | None, compiler_argv: list[str] | None) -> tuple[bool, str]:
    """Compile a TU that only includes the header, and say whether it compiled, with its output."""
    with tempfile.TemporaryDirectory() as tmp:
        probe = Path(tmp) / "probe.cpp"
        probe.write_text(f'#include "{header_abs}"\n')
        if compiler_argv is not None:
            argv = compiler_argv + ["-fsyntax-only", str(probe)]
            cwd = tmp
        else:
            assert flags_from is not None
            argv = [a for a in flags_from.compile_argv if Path(a).resolve() != Path(flags_from.source)
                    and not a.endswith(".cpp")]
            argv = argv + ["-fsyntax-only", str(probe)]
            cwd = flags_from.cwd if flags_from.kind == "syntax" else str(Path(flags_from.source).parent)
        return _run(argv, cwd, {})


def run_header(header: str, src_root: Path, candidates: list[Candidate], jobs: int, neg_cap: int,
               syntax_cap: int, probe_argv: list[str] | None = None,
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
    reach = [c for c in candidates if header in c.headers]
    text_cache: dict[str, str] = {}

    def plan(entity: str) -> tuple[list[Candidate], bool]:
        negs = [c for c in reach if c.kind == "neg"]
        named = [c for c in negs if _mentions(c, entity, text_cache)]
        others = [c for c in negs if c not in named]
        syntax = [c for c in reach if c.kind == "syntax" and _mentions(c, entity, text_cache)]
        chosen = named + others[:neg_cap] + syntax[:syntax_cap]
        return chosen, len(others) > neg_cap or len(syntax) > syntax_cap

    everything: dict[str, Candidate] = {}
    for gate in found:
        for candidate in plan(gate.entity)[0]:
            everything[candidate.name] = candidate

    def check_all(batch: list[Candidate]) -> list[tuple[Candidate, bool]]:
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            return list(zip(batch, pool.map(lambda c: _run(c.argv, c.cwd, c.env)[0], batch)))

    baseline = {c.name: ok for c, ok in check_all(list(everything.values()))}
    probe_from = next((c for c in reach if c.kind == "syntax" and c.compile_argv), None) or next(
        (c for c in reach if c.compile_argv), None)
    base_probe = _probe(header_abs, probe_from, probe_argv)[0] if (probe_from or probe_argv) else False

    outcomes: list[Outcome] = []
    try:
        for gate in found:
            chosen, capped = plan(gate.entity)
            chosen = [c for c in chosen if baseline.get(c.name)]
            outcome = Outcome(gate.key, header, gate.kind, gate.line, gate.entity, gate.original[:200],
                              "survived", candidates=len(chosen))
            header_abs.write_bytes(gate_finder.apply(original, gate))
            probe_ok, probe_out = _probe(header_abs, probe_from, probe_argv) if base_probe else (True, "")
            if not probe_ok:
                # The header alone no longer compiles.  A parse error says the
                # mutant is malformed.  Any other error comes from the header's
                # own checks, and that self-test is a witness of the gate.
                if PARSE_ERROR.search(probe_out):
                    outcome.status = "invalid"
                else:
                    outcome.status, outcome.killer = "killed", "header-self-test"
            else:
                for start in range(0, len(chosen), jobs):
                    results = check_all(chosen[start:start + jobs])
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


def cmd_mutate(args: argparse.Namespace) -> int:
    """Run the mutants of each named header and append one JSON line per mutant."""
    build = Path(args.build).resolve()
    src_root = Path(args.src).resolve()
    out = Path(args.out)
    deps = json.loads((out / "deps.json").read_text())
    candidates = load_candidates(build, deps)
    only_lines = {int(line) for line in args.lines.split(",")} if args.lines else None
    with (out / args.results).open("a") as sink:
        for header in args.header:
            for outcome in run_header(header, src_root, candidates, args.jobs, args.neg_cap, args.syntax_cap,
                                      only_lines=only_lines):
                sink.write(json.dumps(asdict(outcome)) + "\n")
                sink.flush()
    return 0


# ── the self-test ──────────────────────────────────────────────────────

PLANTED = """#pragma once
#include <concepts>
namespace planted {
template <class T> concept Small = sizeof(T) <= 4;
template <class T> requires Small<T> constexpr int take(T) { return 1; }
template <class T> requires std::integral<T> constexpr int count(T) { return 2; }
}
"""
FIXTURE = """#include "planted.h"
int main() { return planted::take(1.0L); }
"""
USER = """#include "planted.h"
static_assert(planted::take(1) == 1 && planted::count(1) == 2);
int main() {}
"""


def cmd_selftest(args: argparse.Namespace) -> int:
    """Plant a header with one witnessed gate and one bare gate, and check each verdict."""
    with tempfile.TemporaryDirectory() as tmp_name:
        tmp = Path(tmp_name)
        (tmp / "include").mkdir()
        (tmp / "include" / "planted.h").write_text(PLANTED)
        (tmp / "neg_take_large.cpp").write_text(FIXTURE)
        (tmp / "user.cpp").write_text(USER)
        base = [args.compiler, "-std=c++26", f"-I{tmp / 'include'}"]
        rows = [{"directory": str(tmp), "file": str(tmp / name), "arguments": base + ["-c", str(tmp / name),
                 "-o", str(tmp / (name + ".o"))]} for name in ("neg_take_large.cpp", "user.cpp")]
        (tmp / "compile_commands.json").write_text(json.dumps(rows))
        driver = REPO_ROOT / "test" / "neg_compile_driver.py"
        headers = frozenset({"include/planted.h"})
        candidates = [
            Candidate("neg_take_large", "neg",
                      [sys.executable, str(driver), str(tmp), str(tmp / "neg_take_large.cpp"), "neg_take_large",
                       "constraints not satisfied", "Small"],
                      str(tmp), {}, str(tmp / "neg_take_large.cpp"), headers),
            Candidate("syntax:user.cpp", "syntax", base + ["-fsyntax-only", str(tmp / "user.cpp")],
                      str(tmp), {}, str(tmp / "user.cpp"), headers),
        ]
        try:
            outcomes = run_header("include/planted.h", tmp, candidates, 2, 100, 100, probe_argv=base)
        except gate_finder.tsast.KitMissing as missing:
            print(f"mutation selftest: SKIP, {missing}", file=sys.stderr)
            return 3
        verdicts = {(o.kind, o.entity): o.status for o in outcomes}
        expected = {("concept", "Small"): "killed", ("requires", "take"): "killed",
                    ("requires", "count"): "survived"}
        failures = [f"{k}: expected {v}, got {verdicts.get(k)}" for k, v in expected.items() if verdicts.get(k) != v]
        if len(outcomes) != len(expected):
            failures.append(f"expected {len(expected)} mutants, got {len(outcomes)}: {sorted(verdicts)}")
        if (tmp / "include" / "planted.h").read_text() != PLANTED:
            failures.append("the planted header was not restored")
        for line in failures:
            print(f"mutation selftest: FAIL {line}", file=sys.stderr)
        if not failures:
            print("mutation selftest: one survivor and two kills, as planted")
        return 1 if failures else 0


def main() -> int:
    """Parse the command line and run one mode."""
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
    mutate.add_argument("--lines", default="", help="comma-separated gate lines to re-check")
    mutate.add_argument("--results", default="results.jsonl", help="the file under --out to append to")
    selftest = sub.add_parser("selftest")
    selftest.add_argument("--compiler", required=True)
    args = parser.parse_args()
    return {"deps": cmd_deps, "mutate": cmd_mutate, "selftest": cmd_selftest}[args.mode](args)


if __name__ == "__main__":
    raise SystemExit(main())
