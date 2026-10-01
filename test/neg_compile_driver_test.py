#!/usr/bin/env python3
"""Test the result store of test/neg_compile_driver.py.

The test makes a small tree of headers, fixtures and a compile database in a
temporary directory, with a store of its own.  It runs the driver as CTest does,
and it reads the lines that the driver writes about the store.  Each check
compiles with the compiler that `--cxx` names.  The exit code is 0 when each
check passes, 1 when a check fails, and 3 when the compiler cannot run.

The store refuses a result while an input of the compile is less than one
second old.  Each check that adds, deletes or edits a file waits for that period
before the next check.
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
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import neg_compile_driver as driver  # noqa: E402

DRIVER = Path(driver.__file__).resolve()
SIZE = ("static assertion failed", "the size of A is one")
CONVERT = ("conversion from", "non-scalar type")
PROBE = ("undeclared_name", "was not declared")

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
    """The checks of the result store, in one temporary tree."""

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
        shadow = self.root / "shadow/a"
        shadow.mkdir()
        (shadow / "A.h").write_text("#pragma once\nstruct A { char field; };\n")
        shadowed = self.run("neg_size", *SIZE)
        self.expect(shadowed[0] == 1 and "compiled successfully" in shadowed[2],
                    f"the shadow header makes the fixture compile: {shadowed[0]}")
        self.expect(self.has(shadowed[3], f"compiled (a search path changed: {self.root / 'shadow'}"),
                    f"the store sees the new header in the search list: {shadowed[3]}")
        (shadow / "A.h").unlink()
        shadow.rmdir()

    def check_precompiled_header(self) -> None:
        """A new precompiled header beside a found header compiles the fixture again."""
        stored = self.run("neg_convert", *CONVERT)
        self.expect(stored[0] == 0, f"the convert fixture passes: {stored[3]}")
        planted = self.root / "include/b/B.h.gch"
        planted.write_text("not a precompiled header\n")
        changed = self.run("neg_convert", *CONVERT)
        self.expect(self.has(changed[3], f"compiled (a search path changed: {planted})"),
                    f"the store sees the new precompiled header: {changed[3]}")
        planted.unlink()

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
        other.unlink()

    def check_has_include(self) -> None:
        """A new file that a `__has_include` probe names compiles the fixture again."""
        stored = self.run("neg_probe", *PROBE)
        self.expect(stored[0] == 0 and self.has(stored[3], "stored"), f"the probe fixture stores: {stored[3]}")
        late = self.root / "include/sub/late"
        late.mkdir()
        (late / "L.h").write_text("#pragma once\n")
        changed = self.run("neg_probe", *PROBE)
        self.expect(changed[0] == 1 and self.has(changed[3], f"compiled (a search path changed: {late / 'L.h'})"),
                    f"a new probed file compiles the fixture again: {changed[0]} {changed[3]}")
        self.settle()
        found = self.run("neg_probe", *PROBE)
        self.expect(found[0] == 1 and self.has(found[3], "stored"), f"the found probe stores: {found[3]}")
        (late / "L.h").unlink()
        removed = self.run("neg_probe", *PROBE)
        self.expect(removed[0] == 0 and self.has(removed[3], f"compiled (a search path changed: {late / 'L.h'})"),
                    f"a removed probed file compiles the fixture again: {removed[0]} {removed[3]}")

    def check_refusals(self) -> None:
        """A fatal error and a macro include give no entry, and CRUCIBLE_NEG_CACHE=0 turns the store off."""
        missing = self.run("neg_missing", "nope/none.h", "compilation terminated")
        self.expect(missing[0] == 0 and self.has(missing[3], "not stored (GCC wrote no dependency file)"),
                    f"the store refuses the result of a fatal error: {missing[3]}")
        computed = self.run("neg_computed", *CONVERT)
        self.expect(computed[0] == 0 and self.has(computed[3], "not stored (a directive or a probe names its file"),
                    f"the store refuses the result of a macro include: {computed[3]}")
        before = self.entry_files()
        off = self.run("neg_convert", *CONVERT, CRUCIBLE_NEG_CACHE="0")
        self.expect(off[0] == 0 and off[3] == ["compiled (the store is off)"],
                    f"CRUCIBLE_NEG_CACHE=0 compiles: {off[3]}")
        self.expect(self.entry_files() == before, "CRUCIBLE_NEG_CACHE=0 writes no entry")

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
        entry.unlink()

    def check_eviction(self) -> None:
        """A store above its limit removes the entries that it did not use for the longest time."""
        for path in self.entry_files():
            path.unlink()
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


def check_parsers(failures: list[str]) -> None:
    """Check the parse of a dependency file and the scan of the directives."""
    parsed = driver.parse_dependency_file("out.o: a\\ b.h c.h \\\n d$$.h\\#e.h\n")
    if parsed != ["a b.h", "c.h", "d$.h#e.h"]:
        failures.append(f"the dependency file parse gives {parsed}")
    if driver.parse_dependency_file("out.o: a.h\nb.h: c.h\n") is not None:
        failures.append("a dependency file with two rules is refused")
    scanned = driver.scan_file_directives(
        b'#include_next <stdlib.h>\n  #  include "local.h"\n#ifdef __has_include\n'
        b"#if __has_include(<x/y.h>)\n#endif\n#endif // __has_include\n"
        b"#include \\\n<spliced.h>\n/* a */ # /* b */ include /* c */ <commented.h>\n"
        b"%:include <digraph.h>\n   the end of a comment */ #include <after.h>\n"
        b"   #include's protect themselves\n#include\n#include <unterminated.h\n"
    )
    expected = [
        ("include_next", "<", "stdlib.h"), ("include", '"', "local.h"), ("include", "<", "spliced.h"),
        ("include", "<", "commented.h"), ("include", "<", "digraph.h"), ("include", "<", "after.h"),
        ("has_include", "<", "x/y.h"),
    ]
    if scanned != expected:
        failures.append(f"the directive scan gives {scanned}")
    for computed in (
        b"#include HEADER\n",
        b"#if __has_include(HEADER)\n#endif\n",
        b"#embed DATA\n",
        b"#define PROBE __has_include\n#if PROBE(<x.h>)\n#endif\n",
        b"   #include guards stop a second copy\n",
    ):
        if driver.scan_file_directives(computed) is not None:
            failures.append(f"the directive scan refuses {computed!r}")


def main(arguments: list[str]) -> int:
    """Run each check, and return 0 when each check passes."""
    cxx = arguments[2] if len(arguments) == 3 and arguments[1] == "--cxx" else ""
    if not cxx or shutil.which(cxx) is None:
        print("neg_compile_driver_test.py --cxx COMPILER: the compiler cannot run", file=sys.stderr)
        return 3
    failures: list[str] = []
    check_parsers(failures)
    with tempfile.TemporaryDirectory(prefix="neg-store-") as directory:
        test = StoreTest(Path(directory), cxx)
        for check in (
            test.check_hit_and_header_edit,
            test.check_shadow_header,
            test.check_precompiled_header,
            test.check_unrelated_file,
            test.check_has_include,
            test.check_refusals,
            test.check_stored_output_is_evaluated,
            test.check_eviction,
        ):
            test.settle()
            check()
        failures.extend(test.failures)
    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    print(f"neg_compile_driver_test.py: {len(failures)} failures", file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
