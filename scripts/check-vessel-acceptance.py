#!/usr/bin/env python3
"""check-vessel-acceptance — the acceptance gate of the PyTorch vessel under vessel/torch.

The gate accepts the vessel of one build only when four facts hold:

    1. The build compiles the vessel against a PyTorch that has
       DispatchKey::Crucible.  Each vessel translation unit has -Werror on its
       compile line, the gate builds the two vessel libraries without an
       error, and neither library carries a sanitizer runtime.
    2. No vessel source names the old substrate.  scripts/check-flip-list.py
       reads the tree, and the gate refuses each vessel path that it reports.
    3. Each vessel Python test (vessel/torch/test_*.py) exits 0.
    4. Replay gives the bytes that PyTorch alone gives.
       vessel/torch/test_replay_equivalence.py is one of the tests of item 3.

The gate fails closed.  A missing PyTorch, a PyTorch without the key, a build
without the vessel, a build without -Werror, a vessel that does not compile, a
library with a sanitizer runtime and an absent parser kit each make the gate
refuse.  The gate never reports a pass or a skip for them.

The gate derives its lists from the tree: each vessel/torch/*.cpp must be in
the compile database with -Werror, and each vessel/torch/test_*.py must exit 0.
A new source or a new test joins the gate with no edit here.

Usage:
    check-vessel-acceptance.py --build-dir DIR --torch-dir DIR --python EXE
    check-vessel-acceptance.py --self-test

Exit 0 when the gate accepts, 1 when it refuses, 2 on a usage error or a
failed self-test.
"""

from __future__ import annotations

import argparse
import json
import os
import shlex
import stat
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
VESSEL_DIR = "vessel/torch"
LIBRARIES = ("libcrucible_vessel.so", "libcrucible_dispatch.so")
TARGETS = ("crucible_vessel", "crucible_dispatch")
TEST_GLOB = "test_*.py"
TEST_TIMEOUT_SECONDS = 1800
# register.cpp alone compiles for about 10 minutes in a Release build.
BUILD_TIMEOUT_SECONDS = 2400
FAILURE_TAIL_LINES = 40


def vessel_sources(root: Path) -> list[Path]:
    """Return each C++ source of the vessel, sorted."""
    return sorted((root / VESSEL_DIR).glob("*.cpp"))


def vessel_tests(root: Path) -> list[Path]:
    """Return each Python test of the vessel, sorted."""
    return sorted((root / VESSEL_DIR).glob(TEST_GLOB))


def same_path(left: str | Path, right: str | Path) -> bool:
    """Return True when two paths name one directory after resolution."""
    return Path(left).resolve() == Path(right).resolve()


def read_cmake_cache(build_dir: Path) -> dict[str, str]:
    """Return the NAME -> VALUE map of CMakeCache.txt, or an empty map when it is absent."""
    cache = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        return {}
    values: dict[str, str] = {}
    for line in cache.read_text(errors="replace").splitlines():
        if not line or line.startswith(("#", "//")) or "=" not in line or ":" not in line.split("=", 1)[0]:
            continue
        key, value = line.split("=", 1)
        values[key.split(":", 1)[0]] = value
    return values


def is_cmake_true(value: str) -> bool:
    """Return True for a CMake value that CMake reads as true."""
    return value.upper() in {"1", "ON", "YES", "TRUE", "Y"}


def check_torch(torch_dir: str, python: str) -> list[str]:
    """Refuse a PyTorch that is absent, has no key, or is not the one the interpreter imports.

    Returns:
        One line for each problem, or an empty list
    """
    if not torch_dir:
        return ["TORCH_DIR is empty.  Configure the build with -DTORCH_DIR=<site-packages of the PyTorch fork>."]
    if not (Path(torch_dir) / "torch" / "include").is_dir():
        return [f"{torch_dir}/torch/include does not exist, so TORCH_DIR names no PyTorch tree."]
    probe = ("import os, torch\n"
             "print(os.path.dirname(os.path.dirname(torch.__file__)))\n"
             "print(hasattr(torch._C.DispatchKey, 'Crucible'))\n")
    try:
        done = subprocess.run([python, "-c", probe], capture_output=True, text=True, timeout=300)
    except OSError as exc:
        return [f"the interpreter {python} does not start: {exc}"]
    if done.returncode != 0:
        return [f"the interpreter {python} cannot import torch: {done.stderr.strip()[-400:]}"]
    lines = done.stdout.strip().splitlines()
    problems: list[str] = []
    if not lines or not same_path(lines[0], torch_dir):
        problems.append(f"the interpreter {python} imports torch from {lines[0] if lines else '(nothing)'}, and the "
                        f"build compiles against {torch_dir}.  Give the gate the interpreter of that PyTorch.")
    if len(lines) < 2 or lines[1] != "True":
        problems.append(f"the PyTorch that {python} imports has no DispatchKey::Crucible.  Build the fork.")
    return problems


def werror_problems(build_dir: Path, root: Path) -> list[str]:
    """Refuse a vessel source that is absent from the compile database or compiles without -Werror."""
    database = build_dir / "compile_commands.json"
    if not database.is_file():
        return [f"{database} does not exist, so the gate cannot read the compile line of the vessel."]
    arguments: dict[Path, list[str]] = {}
    for entry in json.loads(database.read_text()):
        source = Path(entry.get("directory", "."), entry["file"]).resolve()
        arguments[source] = entry["arguments"] if "arguments" in entry else shlex.split(entry["command"])
    problems: list[str] = []
    for source in vessel_sources(root):
        line = arguments.get(source.resolve())
        relative = source.relative_to(root)
        if line is None:
            problems.append(f"{relative} is not in {database}, so the build does not compile it.")
        elif "-Werror" not in line:
            problems.append(f"{relative} compiles without -Werror.")
    return problems


def check_build(build_dir: Path, torch_dir: str, root: Path) -> list[str]:
    """Refuse a build that has no vessel or has no -Werror, then build the vessel targets.

    The gate builds the two vessel targets itself.  A build that the source
    has moved past compiles again here, and a compile error refuses the gate.
    A dry run cannot answer the question: this project uses CONFIGURE_DEPENDS
    globs, and a dry run stops at the step that checks them.

    Returns:
        One line for each problem, or an empty list
    """
    cache = read_cmake_cache(build_dir)
    if not cache:
        return [f"{build_dir}/CMakeCache.txt does not exist, so {build_dir} is not a configured build."]
    problems: list[str] = []
    if not torch_dir or not same_path(cache.get("TORCH_DIR", ""), torch_dir):
        problems.append(f"the build compiles against TORCH_DIR={cache.get('TORCH_DIR', '')!r}, and the gate "
                        f"received {torch_dir!r}.")
    if not is_cmake_true(cache.get("CRUCIBLE_WERROR", "")):
        problems.append("the build sets CRUCIBLE_WERROR to OFF.  The gate accepts only a -Werror build.")
    problems += werror_problems(build_dir, root)
    if problems:
        return problems
    cmake = cache.get("CMAKE_COMMAND") or "cmake"
    try:
        done = subprocess.run([cmake, "--build", str(build_dir), "--target", *TARGETS], capture_output=True,
                              text=True, timeout=BUILD_TIMEOUT_SECONDS)
    except (OSError, subprocess.TimeoutExpired) as exc:
        return [f"the build of the targets {' '.join(TARGETS)} does not finish: {exc}"]
    if done.returncode != 0:
        tail = "\n".join((done.stdout + done.stderr).splitlines()[-FAILURE_TAIL_LINES:])
        return [f"the build of the targets {' '.join(TARGETS)} fails:\n{tail}"]
    for library in LIBRARIES:
        if not (build_dir / "lib" / library).is_file():
            problems.append(f"the build of the targets {' '.join(TARGETS)} makes no {build_dir}/lib/{library}.")
    return problems


def check_uninstrumented(build_dir: Path, python: str, root: Path) -> list[str]:
    """Refuse a vessel library that carries a sanitizer runtime.

    crucible_native.sanitizer_runtime reads the dynamic symbols of the library,
    so the gate and the loader cannot disagree about a library.
    """
    probe = ("import sys\n"
             f"sys.path.insert(0, {str(root / VESSEL_DIR)!r})\n"
             "import crucible_native\n"
             "for path in sys.argv[1:]:\n"
             "    print(crucible_native.sanitizer_runtime(path))\n")
    paths = [str(build_dir / "lib" / library) for library in LIBRARIES]
    try:
        done = subprocess.run([python, "-c", probe, *paths], capture_output=True, text=True, timeout=300)
    except OSError as exc:
        return [f"the interpreter {python} does not start: {exc}"]
    if done.returncode != 0:
        return [f"the sanitizer probe of the vessel libraries failed: {done.stderr.strip()[-400:]}"]
    problems: list[str] = []
    for path, verdict in zip(paths, done.stdout.strip().splitlines()):
        if verdict != "None":
            problems.append(f"{path} carries the runtime {verdict}.  Python loads only a library without a "
                            f"sanitizer runtime.  Use a build of the release or bench preset.")
    return problems


def vessel_consumers(scan_output: str) -> list[str]:
    """Return each line of a flip-list scan that names a vessel path."""
    return [line for line in scan_output.splitlines() if f"{VESSEL_DIR}/" in line]


def check_drain(root: Path) -> list[str]:
    """Refuse a vessel source that names the old substrate, as check-flip-list.py reads it."""
    guard = root / "scripts" / "check-flip-list.py"
    done = subprocess.run([sys.executable, str(guard), "--scan"], capture_output=True, text=True, timeout=900)
    if done.returncode == 3:
        return ["the tree-sitter kit is absent, so the gate cannot read the drain.  Install it with "
                "scripts/install-tree-sitter.sh."]
    if done.returncode != 0:
        return [f"check-flip-list.py --scan exits {done.returncode}: {(done.stdout + done.stderr).strip()[-400:]}"]
    return [f"the vessel still names the old substrate: {line}" for line in vessel_consumers(done.stdout)]


def run_tests(build_dir: Path, python: str, root: Path) -> list[str]:
    """Operate each vessel test against the libraries of the build, and refuse each that does not exit 0."""
    environment = dict(os.environ, CRUCIBLE_BUILD_DIR=str(build_dir))
    problems: list[str] = []
    for test in vessel_tests(root):
        started = time.monotonic()
        try:
            done = subprocess.run([python, str(test)], cwd=root / VESSEL_DIR, env=environment, capture_output=True,
                                  text=True, timeout=TEST_TIMEOUT_SECONDS)
            code, output = done.returncode, done.stdout + done.stderr
        except subprocess.TimeoutExpired as exc:
            code, output = None, f"{exc.stdout or ''}{exc.stderr or ''}"
        elapsed = time.monotonic() - started
        if code == 0:
            print(f"  ok    {test.name} ({elapsed:.1f} s)")
            continue
        verdict = f"exits {code}" if code is not None else f"does not finish in {TEST_TIMEOUT_SECONDS} s"
        print(f"  FAIL  {test.name} {verdict} ({elapsed:.1f} s).  The last lines of its output:")
        for line in str(output).splitlines()[-FAILURE_TAIL_LINES:]:
            print(f"        {line}")
        problems.append(f"{test.name} {verdict}.")
    if not problems and not vessel_tests(root):
        problems.append(f"{VESSEL_DIR} holds no {TEST_GLOB}, so the gate has nothing to operate.")
    return problems


def gate(build_dir: Path, torch_dir: str, python: str, root: Path) -> int:
    """Operate the four items, report each problem, and return the exit code."""
    problems: list[str] = []
    print("vessel acceptance: 1. PyTorch and the build")
    prerequisites = check_torch(torch_dir, python) + check_build(build_dir, torch_dir, root)
    if not prerequisites:
        prerequisites = check_uninstrumented(build_dir, python, root)
    problems += prerequisites
    print("vessel acceptance: 2. the drain onto the new tree")
    problems += check_drain(root)
    print("vessel acceptance: 3 and 4. the vessel tests, replay against PyTorch alone among them")
    if prerequisites:
        print("  not operated, because item 1 refuses the build")
    else:
        problems += run_tests(build_dir, python, root)
    for line in problems:
        print(f"REFUSED  {line}")
    if problems:
        print(f"check-vessel-acceptance: the gate refuses the vessel of {build_dir}, {len(problems)} problem(s).")
        return 1
    print(f"check-vessel-acceptance: the gate accepts the vessel of {build_dir}.")
    return 0


def self_test() -> int:
    """Plant builds and scan output, and prove that each refusal fires and a clean plant passes.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []
    negatives = 0

    def expect(name: str, problems: list[str], needle: str | None) -> None:
        """Record one case: a refusal that contains needle, or no refusal when needle is None."""
        nonlocal negatives
        negatives += needle is not None
        holds = (not problems) if needle is None else any(needle in line for line in problems)
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(f"{name}: expected {'no refusal' if needle is None else repr(needle)}, got {problems}")

    with tempfile.TemporaryDirectory() as work:
        base = Path(work)
        torch_dir = base / "site-packages"
        (torch_dir / "torch" / "include").mkdir(parents=True)
        build = base / "build"
        (build / "lib").mkdir(parents=True)
        # Planted cmake programs: one builds with no error, one reports a
        # compile error, and one exits 0 with nothing built.
        builds = base / "cmake-builds"
        builds.write_text("#!/bin/sh\nexit 0\n")
        fails = base / "cmake-fails"
        fails.write_text("#!/bin/sh\necho 'register.cpp:1:1: error: expected declaration'\nexit 1\n")
        for script in (builds, fails):
            script.chmod(script.stat().st_mode | stat.S_IXUSR)

        def plant(werror: str = "ON", cmake: Path = builds, flag: str = "-Werror",
                  libraries: tuple[str, ...] = LIBRARIES) -> None:
            """Write one planted build."""
            (build / "CMakeCache.txt").write_text(
                f"TORCH_DIR:PATH={torch_dir}\nCRUCIBLE_WERROR:BOOL={werror}\nCMAKE_COMMAND:INTERNAL={cmake}\n")
            entries = [{"directory": str(build), "file": str(source), "command": f"g++ {flag} -c {source}"}
                       for source in vessel_sources(REPO_ROOT)]
            (build / "compile_commands.json").write_text(json.dumps(entries))
            for library in LIBRARIES:
                (build / "lib" / library).unlink(missing_ok=True)
            for library in libraries:
                (build / "lib" / library).write_bytes(b"")

        expect("an empty TORCH_DIR", check_torch("", sys.executable), "TORCH_DIR is empty")
        expect("a TORCH_DIR with no torch/include", check_torch(str(base / "absent"), sys.executable),
               "names no PyTorch tree")
        expect("a directory that is not a build", check_build(base / "absent", str(torch_dir), REPO_ROOT),
               "not a configured build")
        plant()
        expect("a clean planted build", check_build(build, str(torch_dir), REPO_ROOT), None)
        expect("a gate given another TORCH_DIR", check_build(build, str(base), REPO_ROOT), "the gate received")
        plant(werror="OFF")
        expect("a build with CRUCIBLE_WERROR off", check_build(build, str(torch_dir), REPO_ROOT),
               "CRUCIBLE_WERROR to OFF")
        plant(flag="-Wall")
        expect("a vessel source without -Werror", check_build(build, str(torch_dir), REPO_ROOT),
               "compiles without -Werror")
        plant(libraries=LIBRARIES[:1])
        expect("a build that makes no vessel library", check_build(build, str(torch_dir), REPO_ROOT),
               LIBRARIES[1])
        plant(cmake=fails)
        expect("a vessel that does not compile", check_build(build, str(torch_dir), REPO_ROOT),
               "error: expected declaration")
        plant()
        (build / "compile_commands.json").write_text("[]")
        expect("a vessel source absent from the compile database", check_build(build, str(torch_dir), REPO_ROOT),
               "is not in")

    expect("a scan that names a vessel source",
           [f"the vessel still names the old substrate: {line}"
            for line in vessel_consumers("src/Old.cpp\nvessel/torch/record_kernel.h\n")],
           "vessel/torch/record_kernel.h")
    expect("a scan that names no vessel source", vessel_consumers("src/Old.cpp\nbench/bench_x.cpp\n"), None)

    for failure in failures:
        print(f"check-vessel-acceptance --self-test: FAIL — {failure}", file=sys.stderr)
    if failures:
        return 2
    print(f"check-vessel-acceptance --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Parse the arguments and operate the gate or its self-test."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--build-dir")
    parser.add_argument("--torch-dir", default="")
    parser.add_argument("--python")
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    if not args.build_dir or not args.python:
        print("usage: check-vessel-acceptance.py --build-dir DIR --torch-dir DIR --python EXE | --self-test",
              file=sys.stderr)
        return 2
    return gate(Path(args.build_dir).resolve(), args.torch_dir, args.python, REPO_ROOT)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
