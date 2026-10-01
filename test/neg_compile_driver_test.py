#!/usr/bin/env python3
"""Test the result store of test/neg_compile_driver.py.

Each check makes a small tree of headers, fixtures and a compile database in a
temporary directory, with a store of its own.  It runs the driver as CTest does,
and it reads the lines that the driver writes about the store.  Each check
compiles with the compiler that `--cxx` names.  The exit code is 0 when each
check passes, 1 when a check fails, and 3 when the compiler cannot run or
strace cannot record a program.

With `--part K/N`, the test runs only the checks whose position modulo N is K.
CTest runs each part as one test, so the parts run at the same time.  Part 0
also checks the parse of a dependency file and of a strace record.

The store refuses a result while an input of the compile is less than one
second old.  Each check waits for that period after it makes its tree, and
after each check step that adds, deletes or edits a file that a later step
stores.
"""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from collections.abc import Callable
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import neg_compile_driver as driver  # noqa: E402

DRIVER = Path(driver.__file__).resolve()
SIZE = ("static assertion failed", "the size of A is one")
CONVERT = ("conversion from", "non-scalar type")
PROBE = ("undeclared_name", "was not declared")
MISSING = ("nope/none.h", "compilation terminated")

FILES = {
    "include/a/A.h": "#pragma once\nstruct A { int field; };\n",
    "include/b/B.h": "#pragma once\nstruct B {};\n",
    "include/sub/S.h": "#pragma once\n",
    "neg/neg_size.cpp": '#include <a/A.h>\nstatic_assert(sizeof(A) == 1, "the size of A is one");\n',
    "neg/neg_convert.cpp": "#include <b/B.h>\nB converted = 3;\n",
    "neg/neg_probe.cpp": (
        "#if __has_include(<sub/late/L.h>)\n#error late header found\n#else\nint value = undeclared_name;\n#endif\n"
    ),
    "neg/neg_missing.cpp": "#include <nope/none.h>\n",
    "neg/neg_computed.cpp": "#define HEADER <b/B.h>\n#include HEADER\nB converted = 3;\n",
}


class StoreTest:
    """The checks of the result store, each one in a temporary tree of its own."""

    def __init__(self, root: Path, cxx: str) -> None:
        """Make the tree of headers, fixtures and compile database in `root`."""
        self.root = root
        self.build = root / "build"
        self.store = root / "store"
        self.failures: list[str] = []
        for name, text in FILES.items():
            (root / name).parent.mkdir(parents=True, exist_ok=True)
            (root / name).write_text(text)
        (root / "shadow").mkdir()
        self.build.mkdir()
        rows = [
            {
                "directory": str(self.build),
                "file": str(root / name),
                "arguments": [
                    cxx, "-std=c++20", "-I", str(root / "shadow"), "-I", str(root / "include"),
                    "-fdiagnostics-color=never", "-c", str(root / name), "-o", str(self.build / f"{Path(name).stem}.o"),
                ],
            }
            for name in FILES
            if name.startswith("neg/")
        ]
        (self.build / "compile_commands.json").write_text(json.dumps(rows))

    def run(self, fixture: str, *regexes: str, **environment: str) -> tuple[int, str, str, list[str]]:
        """Run the driver on one fixture.

        The result is the exit code, the standard output, the standard error and
        the lines about the store.
        """
        env = {name: value for name, value in os.environ.items() if not name.startswith("CRUCIBLE_NEG_")}
        env["CRUCIBLE_NEG_CACHE_DIR"] = str(self.store)
        env.update(environment)
        proc = subprocess.run(
            [sys.executable, str(DRIVER), str(self.build), str(self.root / "neg" / f"{fixture}.cpp"), fixture, *regexes],
            env=env,
            text=True,
            capture_output=True,
            check=False,
        )
        prefix = f"neg-compile {fixture}: "
        notes = [
            line[len(prefix) :]
            for line in proc.stderr.splitlines()
            if line.startswith(prefix) and "error diagnostic" not in line
        ]
        return proc.returncode, proc.stdout, proc.stderr, notes

    def expect(self, is_true: bool, what: str) -> None:
        """Record the failure `what` when `is_true` is False."""
        if not is_true:
            self.failures.append(what)

    def entry_path(self, notes: list[str]) -> Path | None:
        """Return the path of the entry that a note names, or None."""
        for note in notes:
            found = re.search(r"\(entry ([0-9a-f]{64})\)", note)
            if found:
                return self.store / "entries" / found.group(1)[:2] / found.group(1)
        return None

    def entry_files(self) -> list[Path]:
        """Return each file in the entry directory of the store, in sorted sequence."""
        return sorted(path for path in (self.store / "entries").rglob("*") if path.is_file())

    @staticmethod
    def settle() -> None:
        """Wait until each file of the tree is older than the settle period of the store."""
        time.sleep(driver._SETTLE_NS / 1e9 + 0.2)

    @staticmethod
    def has(notes: list[str], text: str) -> bool:
        """Return True when a note starts with `text`."""
        return any(note.startswith(text) for note in notes)

    def check_hit_and_header_edit(self) -> None:
        """A second run uses the store, and a header edit compiles only the fixtures that read it."""
        first = self.run("neg_size", *SIZE)
        self.expect(first[0] == 0 and self.has(first[3], "compiled (no entry)") and self.has(first[3], "stored"),
                    f"the first run compiles and stores: {first[3]}")
        second = self.run("neg_size", *SIZE)
        self.expect(second[0] == 0 and self.has(second[3], "the result comes from the store"),
                    f"the second run uses the store: {second[3]}")
        self.expect(second[1] == first[1], "the stored output is the output of the compile")
        self.run("neg_convert", *CONVERT)
        header = self.root / "include/a/A.h"
        header.write_text(header.read_text() + "// an edit\n")
        edited = self.run("neg_size", *SIZE)
        self.expect(self.has(edited[3], f"compiled (a dependency changed: {header})"),
                    f"a header edit compiles the fixture that reads it: {edited[3]}")
        self.expect(self.has(edited[3], "not stored (a dependency changed less than one second"),
                    f"the store refuses a result while a dependency is new: {edited[3]}")
        other = self.run("neg_convert", *CONVERT)
        self.expect(self.has(other[3], "the result comes from the store"),
                    f"a header edit does not compile a fixture that does not read it: {other[3]}")
        self.settle()
        again = self.run("neg_size", *SIZE)
        self.expect(self.has(again[3], "stored"), f"the store takes the result after the settle period: {again[3]}")
        self.expect(self.has(self.run("neg_size", *SIZE)[3], "the result comes from the store"),
                    "the new entry is used")

    def check_shadow_header(self) -> None:
        """A new header earlier in the search list compiles the fixture again."""
        stored = self.run("neg_size", *SIZE)
        self.expect(stored[0] == 0 and self.has(stored[3], "stored"), f"the size fixture stores: {stored[3]}")
        shadow = self.root / "shadow/a"
        shadow.mkdir()
        (shadow / "A.h").write_text("#pragma once\nstruct A { char field; };\n")
        shadowed = self.run("neg_size", *SIZE)
        self.expect(shadowed[0] == 1 and "compiled successfully" in shadowed[2],
                    f"the shadow header makes the fixture compile: {shadowed[0]}")
        self.expect(self.has(shadowed[3], f"compiled (a search path changed: {self.root / 'shadow'}"),
                    f"the store sees the new header in the search list: {shadowed[3]}")

    def check_precompiled_header(self) -> None:
        """A new precompiled header beside a found header compiles the fixture again."""
        stored = self.run("neg_convert", *CONVERT)
        self.expect(stored[0] == 0 and self.has(stored[3], "stored"), f"the convert fixture stores: {stored[3]}")
        planted = self.root / "include/b/B.h.gch"
        planted.write_text("not a precompiled header\n")
        changed = self.run("neg_convert", *CONVERT)
        self.expect(self.has(changed[3], f"compiled (a search path changed: {planted})"),
                    f"the store sees the new precompiled header: {changed[3]}")
        self.expect(self.has(changed[3], f"not stored (a precompiled header is in the search list: {planted})"),
                    f"the store refuses a result with a precompiled header in the search list: {changed[3]}")

    def check_unrelated_file(self) -> None:
        """A new file that no search looks for changes no stored result."""
        stored = self.run("neg_convert", *CONVERT)
        self.expect(stored[0] == 0 and self.entry_path(stored[3]) is not None,
                    f"the convert fixture has an entry: {stored[3]}")
        other = self.root / "include/b/Other.h"
        other.write_text("#pragma once\n")
        again = self.run("neg_convert", *CONVERT)
        self.expect(self.has(again[3], "the result comes from the store"),
                    f"a file that no search looks for does not compile the fixture again: {again[3]}")

    def check_has_include(self) -> None:
        """A new file that a `__has_include` probe names compiles the fixture again, and so does its removal."""
        stored = self.run("neg_probe", *PROBE)
        self.expect(stored[0] == 0 and self.has(stored[3], "stored"), f"the probe fixture stores: {stored[3]}")
        late = self.root / "include/sub/late"
        late.mkdir()
        (late / "L.h").write_text("#pragma once\n")
        changed = self.run("neg_probe", *PROBE)
        self.expect(changed[0] == 1 and self.has(changed[3], f"compiled (a search path changed: {late / 'L.h'}"),
                    f"a new probed file compiles the fixture again: {changed[0]} {changed[3]}")
        self.expect(self.has(changed[3], f"not stored (a search path changed less than one second before the "
                                         f"compile: {late / 'L.h'}"),
                    f"the store refuses a result while a probed file is new: {changed[3]}")
        self.settle()
        found = self.run("neg_probe", *PROBE)
        self.expect(found[0] == 1 and self.has(found[3], "stored"), f"the found probe stores: {found[3]}")
        (late / "L.h").unlink()
        removed = self.run("neg_probe", *PROBE)
        self.expect(removed[0] == 0 and self.has(removed[3], f"compiled (a search path changed: {late / 'L.h'}"),
                    f"a removed probed file compiles the fixture again: {removed[0]} {removed[3]}")

    def check_fatal_error(self) -> None:
        """A fixture that stops with a fatal error stores, and a new file with the missing name compiles it again."""
        missing = self.run("neg_missing", *MISSING)
        self.expect(missing[0] == 0 and self.has(missing[3], "stored"),
                    f"the store takes the result of a fatal error: {missing[3]}")
        self.expect(self.has(self.run("neg_missing", *MISSING)[3], "the result comes from the store"),
                    "the entry of a fatal error is used")
        header = self.root / "include/nope/none.h"
        header.parent.mkdir()
        header.write_text("#pragma once\n")
        found = self.run("neg_missing", *MISSING)
        self.expect(found[0] == 1 and "compiled successfully" in found[2]
                    and self.has(found[3], f"compiled (a search path changed: {self.root / 'include/nope'}"),
                    f"a new file with the missing name compiles the fixture again: {found[0]} {found[3]}")

    def check_macro_include(self) -> None:
        """A fixture that names its header with a macro stores, and a shadow of that header compiles it again."""
        computed = self.run("neg_computed", *CONVERT)
        self.expect(computed[0] == 0 and self.has(computed[3], "stored"),
                    f"the store takes the result of a macro include: {computed[3]}")
        shadow = self.root / "shadow/b"
        shadow.mkdir()
        (shadow / "B.h").write_text("#pragma once\nstruct B { B(int) {} };\n")
        shadowed = self.run("neg_computed", *CONVERT)
        self.expect(shadowed[0] == 1 and "compiled successfully" in shadowed[2]
                    and self.has(shadowed[3], f"compiled (a search path changed: {self.root / 'shadow'}"),
                    f"a shadow of the header that a macro names compiles the fixture again: "
                    f"{shadowed[0]} {shadowed[3]}")

    def check_tracer(self) -> None:
        """Without strace, or with a strace that writes a message, the driver compiles and stores no result."""
        bin_dir = self.root / "bin"
        bin_dir.mkdir()
        before = self.entry_files()
        bare = self.run("neg_convert", *CONVERT, PATH=str(bin_dir))
        self.expect(bare[0] == 0 and self.has(bare[3], "not stored (strace is not in PATH"),
                    f"without strace the driver compiles and stores no result: {bare[0]} {bare[3]}")
        fake = bin_dir / "strace"
        fake.write_text('#!/bin/sh\necho "strace: a planted message" >&2\nwhile [ "$1" != "--" ]; do shift; done\n'
                        'shift\nexec "$@"\n')
        fake.chmod(0o755)
        noisy = self.run("neg_convert", *CONVERT, PATH=f"{bin_dir}{os.pathsep}{os.environ.get('PATH', '')}")
        self.expect(noisy[0] == 0 and self.has(noisy[3], "not stored (no strace record shows the start of")
                    and "strace: a planted message" not in noisy[1] + noisy[2],
                    f"a strace with no record gives the output of a compile without it: {noisy[0]} {noisy[3]}")
        off = self.run("neg_convert", *CONVERT, CRUCIBLE_NEG_CACHE="0")
        self.expect(off[0] == 0 and off[3] == ["compiled (the store is off)"],
                    f"CRUCIBLE_NEG_CACHE=0 compiles: {off[3]}")
        self.expect(self.entry_files() == before, "these runs write no entry")

    def check_stored_output_is_evaluated(self) -> None:
        """A damaged entry is a miss, and a changed entry fails as a fresh compile does."""
        first = self.run("neg_convert", *CONVERT)
        entry = self.entry_path(first[3])
        self.expect(entry is not None and entry.is_file(), f"the convert fixture has an entry: {first[3]}")
        if entry is None or not entry.is_file():
            return
        failing = (*CONVERT, "a text that the compiler does not write")
        stored_run = self.run("neg_convert", *failing)
        fresh_run = self.run("neg_convert", *failing, CRUCIBLE_NEG_CACHE="0")
        self.expect(self.has(stored_run[3], "the result comes from the store"), "the failing run uses the store")
        self.expect(stored_run[0] == fresh_run[0] == 1, "a missing regex fails with and without the store")
        self.expect(stored_run[1] == fresh_run[1], "the two runs print the same output")
        prefix = "neg-compile neg_convert: "

        def messages(text: str) -> list[str]:
            """Return the error lines of a run, without the lines about the store."""
            return [line for line in text.splitlines() if not line.startswith(prefix) or "error diagnostic" in line]

        self.expect(messages(stored_run[2]) == messages(fresh_run[2]), "the two runs print the same messages")
        blob = entry.read_bytes()
        entry.write_bytes(blob[:-1] + bytes([blob[-1] ^ 0xFF]))
        damaged = self.run("neg_convert", *CONVERT)
        self.expect(damaged[0] == 0 and self.has(damaged[3], "compiled (the entry is damaged)"),
                    f"a damaged entry is a miss: {damaged[3]}")
        planted = driver.decode_entry(entry.read_bytes())
        self.expect(planted is not None, "the compile after a damaged entry stores a correct entry")
        if planted is None:
            return
        planted["output"] = "planted output\n"
        entry.write_bytes(driver.encode_entry(planted))
        replaced = self.run("neg_convert", *CONVERT)
        self.expect(replaced[0] == 1 and "expected diagnostic not found for neg_convert" in replaced[2]
                    and replaced[1] == "planted output\n",
                    f"the regexes apply to the stored output: {replaced[0]} {replaced[3]}")
        planted["returncode"] = 0
        entry.write_bytes(driver.encode_entry(planted))
        compiled = self.run("neg_convert", *CONVERT)
        self.expect(compiled[0] == 1 and "compiled successfully" in compiled[2],
                    f"a stored exit code 0 fails the fixture: {compiled[0]}")

    def check_eviction(self) -> None:
        """A store above its limit removes the entries that it did not use for the longest time."""
        bucket = self.store / "entries" / "00"
        bucket.mkdir(parents=True, exist_ok=True)
        unused = []
        for index in range(4):
            path = bucket / f"{index:064x}"
            path.write_bytes(bytes(400 * 1024))
            moment = time.time() - 86400 + index
            os.utime(path, (moment, moment))
            unused.append(path)
        stored = self.run("neg_convert", *CONVERT, CRUCIBLE_NEG_CACHE_MAX_MB="1")
        entry = self.entry_path(stored[3])
        remaining = [path for path in unused if path.exists()]
        total = sum(path.stat().st_size for path in self.entry_files())
        self.expect(entry is not None and entry.is_file(), f"the new entry stays: {stored[3]}")
        self.expect(remaining == unused[2:] and total <= 1 << 20,
                    f"the two entries unused for the longest time go: {[path.name[-1] for path in remaining]}")


CHECKS: tuple[Callable[[StoreTest], None], ...] = (
    StoreTest.check_hit_and_header_edit,
    StoreTest.check_has_include,
    StoreTest.check_shadow_header,
    StoreTest.check_precompiled_header,
    StoreTest.check_unrelated_file,
    StoreTest.check_fatal_error,
    StoreTest.check_macro_include,
    StoreTest.check_tracer,
    StoreTest.check_stored_output_is_evaluated,
    StoreTest.check_eviction,
)


def _record(directory: Path, name: str, lines: list[str]) -> None:
    """Write one planted strace record, with the exit line at its end."""
    (directory / name).write_text("\n".join([*lines, "+++ exited with 1 +++"]) + "\n", encoding="latin-1")


def check_parsers(failures: list[str]) -> None:
    """Check the parse of a dependency file and of a strace record."""
    parsed = driver.parse_dependency_file("out.o: a\\ b.h c.h \\\n d$$.h\\#e.h\n")
    if parsed != ["a b.h", "c.h", "d$.h#e.h"]:
        failures.append(f"the dependency file parse gives {parsed}")
    if driver.parse_dependency_file("out.o: a.h\nb.h: c.h\n") is not None:
        failures.append("a dependency file with two rules is refused")
    with tempfile.TemporaryDirectory(prefix="neg-trace-") as scratch:
        directory = Path(scratch)
        prefix = directory / "fixture.trace"
        argv = ["/opt/gcc/bin/g++", "-c", "x.cpp"]
        _record(directory, "fixture.trace.100", [
            'execve("/opt/gcc/bin/g++", ["/opt/gcc/bin/g++", "-c", "x.cpp"], 0x7ff /* 3 vars */) = 0',
            'access("/tmp", R_OK|W_OK|X_OK) = 0',
            'openat(AT_FDCWD, "/tmp/cc1.s", O_RDWR|O_CREAT|O_EXCL, 0600) = 3',
            'unlink("/tmp/cc1.s") = 0',
        ])
        _record(directory, "fixture.trace.101", [
            'execve("/opt/gcc/libexec/cc1plus", ["cc1plus"], 0x7ff /* 3 vars */) = 0',
            'getcwd("/work", 4096) = 6',
            'openat(AT_FDCWD, "x.cpp", O_RDONLY|O_NOCTTY) = 3',
            'newfstatat(AT_FDCWD, "/inc/a/A.h.gch", 0x7ffd, 0) = -1 ENOENT (No such file or directory)',
            'openat(AT_FDCWD, "/inc/a/A.h", O_RDONLY|O_NOCTTY <unfinished ...>',
            '<... openat resumed>) = 4',
            'readlink("/inc", 0x7ffe, 1023) = -1 EINVAL (Invalid argument)',
            'openat(AT_FDCWD, "/inc/\\xc3\\xa9.h", O_RDONLY|O_NOCTTY) = -1 ENOTDIR (Not a directory)',
            'readlink("/proc/self/exe", "/opt/gcc/libexec/cc1plus", 4095) = 24',
            'openat(AT_FDCWD, "/tmp/cc1.s", O_WRONLY|O_CREAT|O_TRUNC, 0666) = 3',
            '--- SIGCHLD {si_signo=SIGCHLD} ---',
        ])
        record, reason = driver.read_trace(prefix, argv, directory)
        expected = {
            "/opt/gcc/bin/g++": {(True, True)}, "/opt/gcc/libexec/cc1plus": {(True, True)},
            "/tmp": {(True, True)}, str(directory / "x.cpp"): {(True, True)},
            "/inc/a/A.h.gch": {(True, False)}, "/inc/a/A.h": {(True, True)}, "/inc": {(False, True)},
            "/inc/é.h": {(True, False)},
        }
        if record is None or record.lookups != expected or record.refusal:
            failures.append(f"the strace record gives {record} ({reason})")
        elif record.sources != [str(directory / "x.cpp"), "/inc/a/A.h"]:
            failures.append(f"the strace record gives the sources {record.sources}")
        for refused, call in (
            ("a path relative to a directory descriptor", 'openat(3, "a/A.h", O_RDONLY|O_NOCTTY) = 4'),
            ("a call that the driver does not know", 'chroot("/jail") = 0'),
            ("an error other than no such file", 'openat(AT_FDCWD, "/inc/b.h", O_RDONLY) = -1 EACCES (Denied)'),
            ("a change of the working directory", 'chdir("/elsewhere") = 0'),
            ("a line that is not a call", 'openat(AT_FDCWD, "/inc/c.h", O_RDONLY|O_NOCTTY'),
        ):
            (directory / "fixture.trace.101").unlink()
            _record(directory, "fixture.trace.101", [call])
            record, reason = driver.read_trace(prefix, argv, directory)
            if record is None or not record.refusal:
                failures.append(f"the strace record does not refuse {refused}: {record} ({reason})")
        (directory / "fixture.trace.101").write_text('openat(AT_FDCWD, "x.cpp", O_RDONLY|O_NOCTTY) = 3\n')
        if driver.read_trace(prefix, argv, directory)[0] is not None:
            failures.append("a strace record with no exit line is refused")
        (directory / "fixture.trace.100").unlink()
        (directory / "fixture.trace.101").unlink()
        _record(directory, "fixture.trace.101", ['execve("/other/g++", ["g++"], 0x7ff /* 3 vars */) = 0'])
        if driver.read_trace(prefix, argv, directory)[0] is not None:
            failures.append("a strace record that does not start the compiler driver is refused")


def tracer_problem() -> str:
    """Return the reason that strace cannot record a program on this host, or an empty text."""
    tracer = shutil.which("strace")
    if tracer is None:
        return "strace is not in PATH"
    with tempfile.TemporaryDirectory(prefix="neg-tracer-") as scratch:
        prefix = Path(scratch) / "probe.trace"
        argv = [sys.executable, "-c", "pass"]
        proc = subprocess.run([tracer, *driver._TRACE_OPTIONS, "-o", str(prefix), "--", *argv],
                              text=True, capture_output=True, check=False)
        record, reason = driver.read_trace(prefix, argv, Path(scratch))
        if proc.returncode != 0 or proc.stderr or record is None:
            return f"strace cannot record a program: exit {proc.returncode}, {proc.stderr.strip()} {reason}".strip()
    return ""


def main(arguments: list[str]) -> int:
    """Run the checks of one part, and return 0 when each check passes."""
    cxx = arguments[2] if len(arguments) >= 3 and arguments[1] == "--cxx" else ""
    index, count = 0, 1
    if len(arguments) == 5 and arguments[3] == "--part":
        text_index, slash, text_count = arguments[4].partition("/")
        if not (slash and text_index.isdigit() and text_count.isdigit() and int(text_index) < int(text_count)):
            print(f"neg_compile_driver_test.py: --part {arguments[4]} is not K/N with 0 <= K < N", file=sys.stderr)
            return 2
        index, count = int(text_index), int(text_count)
    elif len(arguments) != 3:
        print("usage: neg_compile_driver_test.py --cxx COMPILER [--part K/N]", file=sys.stderr)
        return 2
    if not cxx or shutil.which(cxx) is None:
        print("neg_compile_driver_test.py --cxx COMPILER: the compiler cannot run", file=sys.stderr)
        return 3
    problem = tracer_problem()
    if problem:
        print(f"neg_compile_driver_test.py: {problem}.  The store needs strace.  Install strace, and let a "
              f"process trace its children (kernel.yama.ptrace_scope at most 1).", file=sys.stderr)
        return 3
    failures: list[str] = []
    if index == 0:
        check_parsers(failures)
    for check in CHECKS[index::count]:
        with tempfile.TemporaryDirectory(prefix="neg-store-") as directory:
            test = StoreTest(Path(directory), cxx)
            test.settle()
            check(test)
            failures.extend(f"{check.__name__}: {failure}" for failure in test.failures)
    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    print(f"neg_compile_driver_test.py part {index}/{count}: {len(failures)} failures", file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
