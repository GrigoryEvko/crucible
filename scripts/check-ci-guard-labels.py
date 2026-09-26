#!/usr/bin/env python3
"""check-ci-guard-labels — every ctest entry that runs a script under scripts/ carries the ci_guard label.

`ctest -L ci_guard` is the local pre-merge gate.  A guard registered without
the label is invisible to that command, so the gate reports a pass and never
runs the guard.  On 2026-05 twelve of 42 script-backed entries had no label.

THE ENGINE
    The guard reads the registered tests from `ctest --show-only=json-v1` over
    a configured build directory.  CMake has already evaluated every function,
    loop, subdirectory and property call, so the command line and the LABELS
    property of each test are the values ctest itself uses.  A test made inside
    a function, a label added with `set_property(... APPEND ...)`, a later call
    that replaces LABELS, and a test in a subdirectory are all read correctly.
    The guard therefore runs in a build leg, as a ctest entry, and not in a
    job without a build.

THE RULE
    A test counts as a script test when one argument of its command is a path
    under <source>/scripts/.  Each script test must carry the label ci_guard.
    A compiled test binary is not a script test.

Exit 0 clean, 1 on an unlabelled script test, 2 on a usage error, a ctest
failure or a failed self-test.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
LABEL = "ci_guard"


class CtestError(RuntimeError):
    """ctest could not list the tests of the build directory."""


def registered_tests(ctest: str, build_dir: Path) -> list[dict]:
    """Return the test records that `ctest --show-only=json-v1` gives for BUILD_DIR."""
    result = subprocess.run([ctest, "--show-only=json-v1", "--test-dir", str(build_dir)],
                            capture_output=True, text=True)
    if result.returncode != 0:
        raise CtestError(f"ctest exited {result.returncode}: {result.stderr.strip()}")
    try:
        listing = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise CtestError(f"ctest gave output that is not JSON: {error}") from error
    tests = listing.get("tests")
    if not isinstance(tests, list):
        raise CtestError("the ctest JSON holds no 'tests' array")
    return tests


def labels_of(test: dict) -> list[str]:
    """Return the LABELS property of one ctest record (empty when it has none)."""
    for prop in test.get("properties", []):
        if prop.get("name") == "LABELS":
            value = prop.get("value", [])
            return value if isinstance(value, list) else [value]
    return []


def script_of(test: dict, scripts_dir: Path) -> str | None:
    """Return the first command argument that is a path under SCRIPTS_DIR, relative to it, or None."""
    for argument in test.get("command", []) or []:
        path = Path(argument)
        if not path.is_absolute():
            continue
        path = path.resolve()
        if path.is_relative_to(scripts_dir):
            return str(path.relative_to(scripts_dir))
    return None


def unlabelled(tests: list[dict], source_dir: Path) -> tuple[int, list[str]]:
    """Return the number of script tests and one line for each script test without the label.

    Complexity: linear in the number of tests and their arguments.
    """
    scripts_dir = source_dir.resolve() / "scripts"
    script_tests = 0
    missing: list[str] = []
    for test in tests:
        script = script_of(test, scripts_dir)
        if script is None:
            continue
        script_tests += 1
        if LABEL not in labels_of(test):
            missing.append(f"{test.get('name', '<unnamed>')} -> scripts/{script}")
    return script_tests, sorted(missing)


def check(ctest: str, build_dir: Path, source_dir: Path) -> int:
    """Scan the tests of BUILD_DIR and report each unlabelled script test."""
    try:
        tests = registered_tests(ctest, build_dir)
    except (CtestError, OSError) as error:
        print(f"check-ci-guard-labels: {error}", file=sys.stderr)
        return 2
    script_tests, missing = unlabelled(tests, source_dir)
    if script_tests == 0:
        print(f"check-ci-guard-labels: {build_dir} registers no test that runs a script under "
              f"{source_dir}/scripts; the build directory or the source directory is wrong.",
              file=sys.stderr)
        return 2
    if missing:
        print("check-ci-guard-labels: these script tests do not carry the ci_guard label:\n", file=sys.stderr)
        for line in missing:
            print(f"  {line}", file=sys.stderr)
        print("\n`ctest -L ci_guard` does not run them.  Add the label next to the add_test() call:\n"
              "  set_tests_properties(<name> [<name>_self_test] PROPERTIES LABELS \"ci_guard\")",
              file=sys.stderr)
        return 1
    print(f"check-ci-guard-labels: clean, all {script_tests} script tests carry the ci_guard label.",
          file=sys.stderr)
    return 0


PLANTED_ROOT = """\
cmake_minimum_required(VERSION 3.28)
project(planted NONE)
enable_testing()
add_test(NAME planted_compiled_binary COMMAND planted_compiled_binary)
add_test(NAME planted_labelled COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-a.sh)
set_tests_properties(planted_labelled PROPERTIES LABELS "ci_guard;other")
add_test(NAME planted_other_label COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-b.sh)
set_tests_properties(planted_other_label PROPERTIES TIMEOUT 30 LABELS "slow")
add_test(NAME planted_multi_a COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-c.sh --self-test)
add_test(NAME planted_multi_b COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-c.sh)
set_tests_properties(planted_multi_a planted_multi_b PROPERTIES LABELS "ci_guard")
function(planted_guard test_name)
  add_test(NAME ${test_name} COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-d.py)
endfunction()
planted_guard(planted_from_function)
add_test(NAME planted_append COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-e.sh)
set_property(TEST planted_append APPEND PROPERTY LABELS ci_guard)
add_test(NAME planted_overridden COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-f.sh)
set_tests_properties(planted_overridden PROPERTIES LABELS "ci_guard")
set_tests_properties(planted_overridden PROPERTIES LABELS "slow")
add_test(NAME planted_not_a_script COMMAND bash ${CMAKE_SOURCE_DIR}/tools/run.sh scripts/check-a.sh)
add_subdirectory(sub)
"""

PLANTED_SUB = """\
add_test(NAME planted_in_subdir COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-g.sh)
"""

# (test name, the guard must report it)
EXPECTED = (
    ("planted_compiled_binary", False),
    ("planted_labelled", False),
    ("planted_other_label", True),
    ("planted_multi_a", False),
    ("planted_multi_b", False),
    ("planted_from_function", True),
    ("planted_append", False),
    ("planted_overridden", True),
    ("planted_not_a_script", False),
    ("planted_in_subdir", True),
)


def self_test(cmake: str, ctest: str) -> int:
    """Configure a planted CMake project and check each verdict of the guard on its tests."""
    with tempfile.TemporaryDirectory(prefix="ci-guard-labels-") as tmp:
        source = Path(tmp) / "src"
        build = Path(tmp) / "build"
        (source / "sub").mkdir(parents=True)
        (source / "scripts").mkdir()
        (source / "CMakeLists.txt").write_text(PLANTED_ROOT)
        (source / "sub" / "CMakeLists.txt").write_text(PLANTED_SUB)
        configured = subprocess.run([cmake, "-S", str(source), "-B", str(build)],
                                    capture_output=True, text=True)
        if configured.returncode != 0:
            print(f"check-ci-guard-labels: SELF-TEST FAILED, the planted project does not configure:\n"
                  f"{configured.stderr}", file=sys.stderr)
            return 2
        try:
            tests = registered_tests(ctest, build)
        except (CtestError, OSError) as error:
            print(f"check-ci-guard-labels: SELF-TEST FAILED, {error}", file=sys.stderr)
            return 2
        script_tests, missing = unlabelled(tests, source)
        reported = {line.split(" -> ", 1)[0] for line in missing}
        failures = [f"{name}: expected {'a report' if must_report else 'no report'}"
                    for name, must_report in EXPECTED if (name in reported) != must_report]
        if script_tests != 8:
            failures.append(f"expected 8 script tests, counted {script_tests}")
        if check(ctest, build, source) != 1:
            failures.append("the scan of the planted project did not exit 1")
        if failures:
            print("check-ci-guard-labels: SELF-TEST FAILED:\n  " + "\n  ".join(failures), file=sys.stderr)
            return 2
    print(f"check-ci-guard-labels: self-test passed, {len(EXPECTED)} planted tests judged correctly.",
          file=sys.stderr)
    return 0


def main(argv: list[str]) -> int:
    """Parse the arguments and run the scan or the self-test."""
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--self-test", action="store_true", help="configure a planted project and check the verdicts")
    parser.add_argument("--build-dir", type=Path, help="the configured build directory to scan")
    parser.add_argument("--source-dir", type=Path, default=REPO_ROOT, help="the source root (default: this repository)")
    parser.add_argument("--ctest", default="ctest", help="the ctest executable")
    parser.add_argument("--cmake", default="cmake", help="the cmake executable (self-test only)")
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test(args.cmake, args.ctest)
    if args.build_dir is None:
        parser.print_usage(sys.stderr)
        print("check-ci-guard-labels: --build-dir is necessary for a scan", file=sys.stderr)
        return 2
    return check(args.ctest, args.build_dir, args.source_dir)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
