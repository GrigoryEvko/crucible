"""pgo — collect a profile for profile-guided optimization on the hardware tier of this host (`loop pgo`).

THE COLLECTION
    1. The command configures the preset pgo-generate in ROOT/build-pgo-generate.
       It writes the query files of the CMake file API first, so CMake writes
       its cache and its code model as JSON, and step 2 reads what CMake
       decided and not the text of a list file.  CMake chooses the profile
       directory from the name that the compiler gives this processor, so
       the same command collects a znver5 profile on the EPYC host and a
       sapphirerapids profile on the Xeon host.
    2. It reads the profile directory and the tier from the cache, and each
       bench from the code model: an executable whose FOLDER property is
       crucible_bench, which the function crucible_bench() of
       bench/CMakeLists.txt sets.  It prints the input set before it runs
       anything.
    3. It builds the instrumented benches with the cmake that configured the
       build directory (`setup check`, utils/assay/setup/pins.py).
    4. It removes each counter file (*.gcda) of the profile directory.  A
       rebuilt object replaces its counter file, and a bench that runs again
       adds to it, so this removes only what an older collection left for
       objects that no longer exist.
    5. It runs each bench, with its output in BUILD_DIR/pgo-run/NAME.log.  A
       bench that runs for longer than its time limit gets SIGTERM.
    6. It writes the record COLLECTION beside the counters: the commit, the
       state of the tree, the time, the host, the tier, the benches of each
       outcome and the count of counter files.  CMakeLists.txt reads the
       commit, the tree and the time of the record.

    When the command completes, build with the profile:
        cmake --preset pgo && cmake --build --preset pgo

    The benches inherit the CPU affinity of this process.  Run the command
    under taskset to keep them off reserved cores.  A bench that fails or
    runs too long gives no counters, and COLLECTION names it.  The command
    fails only when no counter file exists at the end.

THE ENVIRONMENT
    CRUCIBLE_PGO_DIR            The profile root.  The preset value is ROOT/pgo
    CRUCIBLE_PGO_JOBS           The build parallelism.  The default is 96
    CRUCIBLE_PGO_BENCH_TIMEOUT  The time limit of each bench, in seconds.  The default is 900
    CRUCIBLE_PGO_SKIP           Bench names to skip: shell patterns of fnmatch, with a comma between two patterns

Usage
    loop pgo
    guard selftest loop.pgo

Exit 0 when the profile directory holds counters, 1 when a step failed or no counter was written, 2 on a usage error
or a failed self-test.
"""

from __future__ import annotations

import fnmatch
import json
import os
import socket
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path

from assay.core.paths import REPO_ROOT
from assay.loop import history as loop_history
from assay.setup import pins as cmake_pin

PRESET = "pgo-generate"
BUILD_DIR_NAME = "build-pgo-generate"
DEFAULT_JOBS = 96
DEFAULT_BENCH_TIMEOUT_S = 900
# The objects of the CMake file API that the collection asks for.
QUERY_KINDS = ("cache-v2", "codemodel-v2")
BENCH_FOLDER = "crucible_bench"
COUNTER_SUFFIX = ".gcda"
RECORD_NAME = "COLLECTION"
RUN_LOGS = "pgo-run"
# The time that a bench gets to end after SIGTERM, in seconds.
TERMINATE_GRACE_S = 60.0


class PgoError(Exception):
    """A step of the collection cannot go on: a reply that cannot be read, or a cache with no profile directory."""


@dataclass(frozen=True, slots=True)
class Facts:
    """What CMake decided for the collection: the profile directory, the tier, and the benches in sorted order."""

    profile_dir: Path
    tier: str
    benches: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class Outcomes:
    """The benches of each outcome of the runs."""

    ok: tuple[str, ...]
    failed: tuple[str, ...]
    timed_out: tuple[str, ...]


# ── The file API ───────────────────────────────────────────────────────────


def request_replies(build_dir: Path) -> None:
    """Write the query files that ask CMake to write its cache and its code model as JSON at the next configure.

    Args:
        build_dir: The build directory
    """
    query = build_dir / ".cmake" / "api" / "v1" / "query"
    query.mkdir(parents=True, exist_ok=True)
    for kind in QUERY_KINDS:
        (query / kind).write_bytes(b"")


def read_facts(build_dir: Path) -> Facts:
    """Read the profile directory, the tier and the benches from the newest reply of the CMake file API.

    The index whose name is the largest is the newest one.  Complexity: linear in the size of the reply.

    Args:
        build_dir: The configured build directory

    Returns:
        The facts

    Raises:
        PgoError: If the reply cannot be read, or the cache has no profile directory or no tier
    """
    reply = build_dir / ".cmake" / "api" / "v1" / "reply"
    try:
        index = json.loads(max(reply.glob("index-*.json")).read_text(encoding="utf-8"))
        objects = {entry["kind"]: entry for entry in index["objects"]}
        cache = json.loads((reply / objects["cache"]["jsonFile"]).read_text(encoding="utf-8"))
        values = {entry["name"]: entry["value"] for entry in cache["entries"]}
        model = json.loads((reply / objects["codemodel"]["jsonFile"]).read_text(encoding="utf-8"))
        benches = []
        for target in model["configurations"][0]["targets"]:
            detail = json.loads((reply / target["jsonFile"]).read_text(encoding="utf-8"))
            if detail.get("type") == "EXECUTABLE" and detail.get("folder", {}).get("name") == BENCH_FOLDER:
                benches.append(detail["name"])
    except (OSError, ValueError, KeyError, IndexError, TypeError) as problem:
        raise PgoError(f"the CMake file API reply in {build_dir} could not be read: {problem}") from None
    profile_dir = str(values.get("CRUCIBLE_PGO_PROFILE_DIR", ""))
    tier = str(values.get("CRUCIBLE_PGO_TIER", ""))
    if not profile_dir or not tier:
        raise PgoError(f"the CMake cache of {build_dir} has no CRUCIBLE_PGO_PROFILE_DIR or CRUCIBLE_PGO_TIER.")
    return Facts(Path(profile_dir), tier, tuple(sorted(benches)))


# ── The benches ────────────────────────────────────────────────────────────


def skip_patterns(text: str) -> tuple[str, ...]:
    """Return the patterns of CRUCIBLE_PGO_SKIP: the parts between the commas, with no empty part."""
    return tuple(part.strip() for part in text.split(",") if part.strip())


def select(benches: tuple[str, ...], patterns: tuple[str, ...]) -> tuple[tuple[str, ...], tuple[str, ...]]:
    """Return the benches to run and the benches to skip: a bench that a pattern matches is skipped.

    Complexity: O(b * p) for b benches and p patterns.
    """
    skipped = tuple(name for name in benches if any(fnmatch.fnmatchcase(name, pattern) for pattern in patterns))
    return tuple(name for name in benches if name not in skipped), skipped


def run_bench(command: list[str], log: Path, timeout_s: float) -> int | None:
    """Run one bench with its output in a log, and send SIGTERM when it runs longer than its limit.

    Args:
        command: The bench and its arguments
        log: The log of its standard output and standard error
        timeout_s: The time limit, in seconds

    Returns:
        The exit status, or None when the bench ran too long
    """
    with log.open("wb") as output:
        process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        try:
            return process.wait(timeout=timeout_s)
        except subprocess.TimeoutExpired:
            process.terminate()
            try:
                process.wait(timeout=TERMINATE_GRACE_S)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            return None


def run_benches(binaries: Path, benches: tuple[str, ...], logs: Path, timeout_s: float) -> Outcomes:
    """Run each bench of a build in turn, and sort them by outcome.

    Args:
        binaries: The directory of the bench executables
        benches: The benches, in the order of the runs
        logs: The directory of the logs
        timeout_s: The time limit of each bench, in seconds

    Returns:
        The outcomes
    """
    logs.mkdir(parents=True, exist_ok=True)
    ok: list[str] = []
    failed: list[str] = []
    timed_out: list[str] = []
    for name in benches:
        print(f"loop pgo: {name} ... ", end="", flush=True)
        status = run_bench([str(binaries / name)], logs / f"{name}.log", timeout_s)
        if status == 0:
            ok.append(name)
            print("ok")
        elif status is None:
            timed_out.append(name)
            print(f"stopped after {timeout_s:g}s")
        else:
            failed.append(name)
            print(f"exit {status}, log in {logs / f'{name}.log'}")
    return Outcomes(tuple(ok), tuple(failed), tuple(timed_out))


def counter_files(profile_dir: Path) -> list[Path]:
    """Return each counter file under the profile directory."""
    return sorted(profile_dir.rglob(f"*{COUNTER_SUFFIX}"))


def collection_text(commit: str, tree: str, collected: str, host: str, tier: str, outcomes: Outcomes,
                    skipped: tuple[str, ...], counters: int) -> str:
    """Return the text of the record COLLECTION, one field on each line.

    Args:
        commit: The commit of the tree
        tree: "clean" or "dirty"
        collected: The time of the collection, ISO 8601 in UTC
        host: The name of the host
        tier: The tier that CMake resolved
        outcomes: The benches of each outcome
        skipped: The benches that CRUCIBLE_PGO_SKIP skipped
        counters: The count of counter files

    Returns:
        The text
    """
    return "".join(f"{name}={value}\n" for name, value in (
        ("commit", commit), ("tree", tree), ("collected", collected), ("host", host), ("tier", tier),
        ("benches_ok", " ".join(outcomes.ok)), ("benches_failed", " ".join(outcomes.failed)),
        ("benches_timed_out", " ".join(outcomes.timed_out)), ("benches_skipped", " ".join(skipped)),
        ("gcda_files", counters)))


# ── The command ────────────────────────────────────────────────────────────


def positive_number(name: str, default: int) -> int:
    """Return the value of an environment variable that holds a positive whole number, or the default.

    Raises:
        PgoError: If the variable holds something else
    """
    text = os.environ.get(name, "")
    if not text:
        return default
    if not text.isdigit() or int(text) == 0:
        raise PgoError(f"{name} must be a positive whole number, not '{text}'.")
    return int(text)


def collect(root: Path) -> int:
    """Configure, build, run and record, as THE COLLECTION tells.

    Args:
        root: The source tree

    Returns:
        The exit status
    """
    jobs = positive_number("CRUCIBLE_PGO_JOBS", DEFAULT_JOBS)
    timeout_s = positive_number("CRUCIBLE_PGO_BENCH_TIMEOUT", DEFAULT_BENCH_TIMEOUT_S)
    patterns = skip_patterns(os.environ.get("CRUCIBLE_PGO_SKIP", ""))
    build_dir = root / BUILD_DIR_NAME
    request_replies(build_dir)
    configure = ["cmake", "--preset", PRESET]
    if os.environ.get("CRUCIBLE_PGO_DIR"):
        configure.append(f"-DCRUCIBLE_PGO_DIR={os.environ['CRUCIBLE_PGO_DIR']}")
    if subprocess.run(configure, cwd=root, check=False).returncode != 0:
        print(f"loop pgo: the configure of the preset {PRESET} failed.", file=sys.stderr)
        return 1
    facts = read_facts(build_dir)
    benches, skipped = select(facts.benches, patterns)
    print(f"loop pgo: tier {facts.tier}, profile directory {facts.profile_dir}")
    print(f"loop pgo: {len(facts.benches)} benches registered in bench/CMakeLists.txt, {len(benches)} to run, "
          f"{len(skipped)} skipped")
    for name in benches:
        print(f"  {name}")
    if not benches:
        print("loop pgo: nothing to run.", file=sys.stderr)
        return 1
    build_cmake = cmake_pin.configured_program(build_dir, "cmake")
    if subprocess.run([build_cmake, "--build", str(build_dir), "-j", str(jobs), "--target", *benches], cwd=root,
                      check=False).returncode != 0:
        print("loop pgo: the build of the instrumented benches failed.", file=sys.stderr)
        return 1
    facts.profile_dir.mkdir(parents=True, exist_ok=True)
    for counter in counter_files(facts.profile_dir):
        counter.unlink()
    logs = build_dir / RUN_LOGS
    outcomes = run_benches(build_dir / "bench", benches, logs, timeout_s)
    counters = len(counter_files(facts.profile_dir))
    state = loop_history.git_state(root)
    if state["commit"] is None:
        raise PgoError(f"git cannot read the commit and the state of {root}, so the record cannot name them.")
    record = collection_text(str(state["commit"]), str(state["tree"]),
                             time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), socket.gethostname(), facts.tier,
                             outcomes, skipped, counters)
    (facts.profile_dir / RECORD_NAME).write_text(record, encoding="utf-8")
    print(f"loop pgo: {counters} counter files in {facts.profile_dir} ({len(outcomes.ok)} ok, {len(outcomes.failed)} "
          f"failed, {len(outcomes.timed_out)} stopped)")
    if counters == 0:
        print(f"loop pgo: no counters were written.  The logs are in {logs}.", file=sys.stderr)
        return 1
    print("loop pgo: next, cmake --preset pgo && cmake --build --preset pgo")
    return 0


# ── The self-test ──────────────────────────────────────────────────────────


def write_reply(build_dir: Path, values: dict[str, str], targets: dict[str, tuple[str, str]]) -> None:
    """Write a planted reply of the CMake file API: a cache with the values, and a code model with the targets.

    Args:
        build_dir: The planted build directory
        values: The entries of the cache
        targets: The type and the folder of each target, by its name
    """
    reply = build_dir / ".cmake" / "api" / "v1" / "reply"
    reply.mkdir(parents=True)
    (reply / "cache-v2-1.json").write_text(json.dumps(
        {"entries": [{"name": name, "value": value} for name, value in values.items()]}), encoding="utf-8")
    listed = []
    for number, (name, (kind, folder)) in enumerate(sorted(targets.items())):
        detail: dict[str, object] = {"name": name, "type": kind}
        if folder:
            detail["folder"] = {"name": folder}
        (reply / f"target-{number}.json").write_text(json.dumps(detail), encoding="utf-8")
        listed.append({"name": name, "jsonFile": f"target-{number}.json"})
    (reply / "codemodel-v2-1.json").write_text(json.dumps({"configurations": [{"targets": listed}]}),
                                               encoding="utf-8")
    objects = [{"kind": "cache", "jsonFile": "cache-v2-1.json"}, {"kind": "codemodel", "jsonFile": "codemodel-v2-1.json"}]
    # An older index with no cache: the newest index is the one that counts.
    (reply / "index-2026-01-01T00-00-00-0000.json").write_text(json.dumps({"objects": []}), encoding="utf-8")
    (reply / "index-2026-10-08T00-00-00-0000.json").write_text(json.dumps({"objects": objects}), encoding="utf-8")


def self_test() -> int:
    """Check the reply, the selection, the runs and the record on planted input.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    with tempfile.TemporaryDirectory(prefix="loop-pgo-") as work:
        root = Path(work)
        build = root / "build"
        request_replies(build)
        expect("the query asks for the cache and the code model",
               sorted(path.name for path in (build / ".cmake" / "api" / "v1" / "query").iterdir())
               == sorted(QUERY_KINDS))
        write_reply(build, {"CRUCIBLE_PGO_PROFILE_DIR": str(root / "profile"), "CRUCIBLE_PGO_TIER": "znver5"},
                    {"bench_b": ("EXECUTABLE", BENCH_FOLDER), "bench_a": ("EXECUTABLE", BENCH_FOLDER),
                     "test_c": ("EXECUTABLE", ""), "bench_lib": ("STATIC_LIBRARY", BENCH_FOLDER)})
        facts = read_facts(build)
        expect("the reply gives the profile directory, the tier and only the bench executables, in sorted order",
               facts == Facts(root / "profile", "znver5", ("bench_a", "bench_b")))
        empty = root / "empty"
        write_reply(empty, {"CRUCIBLE_PGO_TIER": "znver5"}, {})
        try:
            read_facts(empty)
            expect("a cache with no profile directory stops the collection", False)
        except PgoError:
            expect("a cache with no profile directory stops the collection", True)
        try:
            read_facts(root / "absent")
            expect("a build with no reply stops the collection", False)
        except PgoError:
            expect("a build with no reply stops the collection", True)

        run, skipped = select(("bench_arena", "bench_philox", "bench_ring"), skip_patterns(" bench_p*, ,bench_ring"))
        expect("a pattern of CRUCIBLE_PGO_SKIP skips each bench that it matches",
               run == ("bench_arena",) and skipped == ("bench_philox", "bench_ring"))
        expect("no pattern skips no bench", select(("bench_a",), skip_patterns("")) == (("bench_a",), ()))

        binaries = root / "bin"
        binaries.mkdir()
        for name, body in (("passes", "exit 0"), ("fails", "exit 3"), ("hangs", "exec sleep 30")):
            script = binaries / name
            script.write_text(f"#!/bin/sh\n{body}\n", encoding="utf-8")
            script.chmod(0o755)
        started = time.monotonic()
        outcomes = run_benches(binaries, ("passes", "fails", "hangs"), root / "logs", 0.5)
        expect("each bench gets its outcome, and a bench that runs too long gets SIGTERM",
               outcomes == Outcomes(("passes",), ("fails",), ("hangs",)) and time.monotonic() - started < 20)
        expect("each bench writes its log", sorted(path.name for path in (root / "logs").iterdir())
               == ["fails.log", "hangs.log", "passes.log"])

        record = collection_text("abc", "clean", "2026-10-08T00:00:00Z", "host", "znver5", outcomes,
                                 ("bench_x",), 7)
        expect("the record gives one field on each line, and CMake reads commit, tree and collected",
               record.splitlines() == ["commit=abc", "tree=clean", "collected=2026-10-08T00:00:00Z", "host=host",
                                       "tier=znver5", "benches_ok=passes", "benches_failed=fails",
                                       "benches_timed_out=hangs", "benches_skipped=bench_x", "gcda_files=7"])
        profile = root / "profile" / "nested"
        profile.mkdir(parents=True)
        (profile / "one.gcda").write_bytes(b"")
        (profile / "two.gcno").write_bytes(b"")
        expect("the counter files are the .gcda files under the profile directory",
               counter_files(root / "profile") == [profile / "one.gcda"])

    if failures:
        print(f"loop pgo --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("loop pgo --self-test: every case holds.")
    return 0


def main(argv: list[str]) -> int:
    """Collect the profile, or run the self-test.

    Args:
        argv: The words after `loop pgo`

    Returns:
        The exit status
    """
    if argv == ["--self-test"]:
        return self_test()
    if argv:
        print("usage: loop pgo  (the environment variables of THE ENVIRONMENT in utils/assay/loop/pgo.py set the "
              "options)", file=sys.stderr)
        return 2
    try:
        return collect(REPO_ROOT)
    except (PgoError, cmake_pin.PinError) as problem:
        print(f"loop pgo: {problem}", file=sys.stderr)
        return 1
