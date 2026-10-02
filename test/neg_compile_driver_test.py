#!/usr/bin/env python3
"""Test the result store and the cost budget of test/neg_compile_driver.py.

Each check makes a small tree of headers, fixtures and a compile database in a
temporary directory, with a store of its own.  It runs the driver as CTest does,
and it reads the lines that the driver writes about the store.  Each check
compiles with the compiler that `--cxx` names.  The exit code is 0 when each
check passes, 1 when a check fails, and 3 when the compiler cannot run or when
the host cannot give the condition of each check of the part.

With `--part K/N`, the test runs only the checks whose position modulo N is K.
CTest runs each part as one test, so the parts run at the same time.  Part 0
also checks the parse of a dependency file, of a search list and of a list of
the loader, the ELF interpreter, the search roots, the compile environment,
and the CPU budget of a compile with no user time.

The store refuses a result while an input of the compile is less than one
second old.  Each check waits for that period after it makes its tree, and
after each check step that adds, deletes or edits a file that a later step
stores.  A wait can only make a file older, so a slow host cannot change the
verdict of a step after a wait.  A step that expects a refusal for a new file
gives the driver the change time of that file as its clock
(CRUCIBLE_NEG_NOW_NS).  Then the change is in the settle period also when the
driver starts more than one second after the change.
"""

from __future__ import annotations

import contextlib
import io
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
import neg_compile_store as store  # noqa: E402

DRIVER = Path(__file__).resolve().with_name("neg_compile_driver.py")
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
    "neg/neg_quote.cpp": '#include "b/B.h"\nB converted = 3;\n',
    "neg/neg_parent.cpp": '#include "../b/B.h"\nB converted = 3;\n',
}
# The search list of each fixture is `-I shadow -I include`.  neg_parent also
# searches include/sub, where its name `../b/B.h` reaches include/b/B.h.
EXTRA_SEARCH = {"neg/neg_parent.cpp": "include/sub"}
CONVERTING_B = "#pragma once\nstruct B { B(int) {} };\n"


class StoreTest:
    """The checks of the result store, each one in a temporary tree of its own."""

    def __init__(self, root: Path, cxx: str) -> None:
        """Make the tree of headers, fixtures, compile database and CMakeCache.txt in `root`.

        The tree uses the real path of `root`, because the driver names each
        search directory by its real path.  The store is in the build
        directory: a new directory in `root` changes the time that the store
        reads for each path in `root` that does not exist.  CMakeCache.txt
        names `root` as the source root and the build directory as the build
        root, as CMake does.
        """
        root.mkdir(parents=True, exist_ok=True)
        self.root = root.resolve()
        self.cxx = cxx
        self.build = self.root / "build"
        self.caches = self.build / "caches"
        self.store = self.caches / "neg"
        self.failures: list[str] = []
        self.skipped = ""
        for name, text in FILES.items():
            (self.root / name).parent.mkdir(parents=True, exist_ok=True)
            (self.root / name).write_text(text)
        (self.root / "shadow").mkdir()
        self.build.mkdir()
        (self.build / "CMakeCache.txt").write_text(f"CMAKE_HOME_DIRECTORY:INTERNAL={self.root}\n"
                                                   f"CMAKE_CACHEFILE_DIR:INTERNAL={self.build}\n")
        rows = []
        for name in FILES:
            if not name.startswith("neg/"):
                continue
            extra = ["-I", str(self.root / EXTRA_SEARCH[name])] if name in EXTRA_SEARCH else []
            rows.append({
                "directory": str(self.build),
                "file": str(self.root / name),
                "arguments": [
                    cxx, "-std=c++20", "-I", str(self.root / "shadow"), "-I", str(self.root / "include"), *extra,
                    "-fdiagnostics-color=never", "-c", str(self.root / name), "-o",
                    str(self.build / f"{Path(name).stem}.o"),
                ],
            })
        (self.build / "compile_commands.json").write_text(json.dumps(rows))

    def run(self, fixture: str, *regexes: str, warnings_dir: Path | None = None,
            **environment: str) -> tuple[int, str, str, list[str]]:
        """Run the driver on one fixture.

        The result is the exit code, the standard output, the standard error and
        the lines about the store.
        """
        # A check that needs a CI runner sets GITHUB_ACTIONS itself, so each
        # verdict is the same on a CI runner and on the build host.
        env = {name: value for name, value in os.environ.items()
               if not name.startswith("CRUCIBLE_NEG_") and name != "GITHUB_ACTIONS"}
        env["CRUCIBLE_CACHE_DIR"] = str(self.caches)
        env.update(environment)
        options = ["--warnings-dir", str(warnings_dir)] if warnings_dir is not None else []
        proc = subprocess.run(
            [sys.executable, str(DRIVER), *options, str(self.build), str(self.root / "neg" / f"{fixture}.cpp"), fixture,
             *regexes],
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

    def skip(self, reason: str) -> None:
        """Record that the host cannot give the condition of the check."""
        self.skipped = reason

    def entry_path(self, notes: list[str]) -> Path | None:
        """Return the path of the entry that a note names, or None."""
        for note in notes:
            found = re.search(r"\(entry ([0-9a-f]{64})\)", note)
            if found:
                return self.store / "entries" / found.group(1)[:2] / found.group(1)
        return None

    @staticmethod
    def results_of(entry: Path | None) -> list[dict[str, object]]:
        """Return the results of the entry at `entry`, newest first, or no result when it is not a correct entry."""
        decoded = store.decode_entry(entry.read_bytes()) if entry is not None and entry.is_file() else None
        return store.unpack_results(decoded) if decoded is not None else []

    def newest_result(self, entry: Path | None) -> dict[str, object] | None:
        """Return the newest result of the entry at `entry`, or None."""
        results = self.results_of(entry)
        return results[0] if results else None

    def entry_files(self) -> list[Path]:
        """Return each file in the entry directory of the store, in sorted sequence."""
        return sorted(path for path in (self.store / "entries").rglob("*") if path.is_file())

    @staticmethod
    def settle() -> None:
        """Wait until each file of the tree is older than the settle period of the store."""
        time.sleep(store._SETTLE_NS / 1e9 + 0.2)

    @staticmethod
    def started_at(path: Path) -> dict[str, str]:
        """Return the environment that gives the driver the change time of `path` as its clock.

        The driver then reads the change of `path` as a change in the settle
        period before the compile, however late the run starts.  Each file
        that the step did not change is older than the period, because the
        check waited after it made its tree.
        """
        return {"CRUCIBLE_NEG_NOW_NS": str(path.stat().st_ctime_ns)}

    @staticmethod
    def has(notes: list[str], text: str) -> bool:
        """Return True when a note starts with `text`."""
        return any(note.startswith(text) for note in notes)

    def twin(self) -> StoreTest:
        """Return a second tree beside this one, a work tree of the same commit, with the store of this tree."""
        other = StoreTest(self.root.parent / "twin", self.cxx)
        other.caches = self.caches
        other.store = self.store
        return other

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
        edited = self.run("neg_size", *SIZE, **self.started_at(header))
        self.expect(self.has(edited[3], f"compiled (a dependency changed: {header})"),
                    f"a header edit compiles the fixture that reads it: {edited[3]}")
        self.expect(self.has(edited[3], f"not stored (a dependency changed less than one second before the compile: "
                                        f"{header})"),
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
        self.expect(self.has(shadowed[3], f"compiled (a search directory changed: {self.root / 'shadow'})"),
                    f"the store sees the new header in the search list: {shadowed[3]}")

    def check_precompiled_header(self) -> None:
        """A new precompiled header beside a found header compiles the fixture again, and the store refuses it."""
        stored = self.run("neg_convert", *CONVERT)
        self.expect(stored[0] == 0 and self.has(stored[3], "stored"), f"the convert fixture stores: {stored[3]}")
        planted = self.root / "include/b/B.h.gch"
        planted.write_text("not a precompiled header\n")
        changed = self.run("neg_convert", *CONVERT)
        self.expect(self.has(changed[3], f"compiled (a search directory changed: {self.root / 'include'})"),
                    f"the store sees the new precompiled header: {changed[3]}")
        self.expect(self.has(changed[3], f"not stored (a precompiled header is in a search directory: {planted})"),
                    f"the store refuses a result with a precompiled header in a search directory: {changed[3]}")
        self.settle()
        settled = self.run("neg_convert", *CONVERT)
        self.expect(self.has(settled[3], f"not stored (a precompiled header is in a search directory: {planted})"),
                    f"the store refuses the result also after the settle period: {settled[3]}")

    def check_unrelated_file(self) -> None:
        """A new file outside each search root changes no stored result, and a new file in a root does."""
        stored = self.run("neg_convert", *CONVERT)
        self.expect(stored[0] == 0 and self.entry_path(stored[3]) is not None,
                    f"the convert fixture has an entry: {stored[3]}")
        unrelated = self.root / "other/Other.h"
        unrelated.parent.mkdir()
        unrelated.write_text("#pragma once\n")
        again = self.run("neg_convert", *CONVERT)
        self.expect(self.has(again[3], "the result comes from the store"),
                    f"a file outside each search root does not compile the fixture again: {again[3]}")
        (self.root / "include/b/Other.h").write_text("#pragma once\n")
        rooted = self.run("neg_convert", *CONVERT)
        self.expect(self.has(rooted[3], f"compiled (a search directory changed: {self.root / 'include'})"),
                    f"a new file in a search directory compiles the fixture again: {rooted[3]}")

    def check_has_include(self) -> None:
        """A new file that a `__has_include` probe names compiles the fixture again, and so does its removal."""
        stored = self.run("neg_probe", *PROBE)
        self.expect(stored[0] == 0 and self.has(stored[3], "stored"), f"the probe fixture stores: {stored[3]}")
        late = self.root / "include/sub/late"
        late.mkdir()
        (late / "L.h").write_text("#pragma once\n")
        include = self.root / "include"
        changed = self.run("neg_probe", *PROBE, **self.started_at(late))
        self.expect(changed[0] == 1 and self.has(changed[3], f"compiled (a search directory changed: {include})"),
                    f"a new probed file compiles the fixture again: {changed[0]} {changed[3]}")
        self.expect(self.has(changed[3], f"not stored (a search directory changed less than one second before the "
                                         f"compile: {include})"),
                    f"the store refuses a result while a probed file is new: {changed[3]}")
        self.settle()
        found = self.run("neg_probe", *PROBE)
        self.expect(found[0] == 1 and self.has(found[3], "stored"), f"the found probe stores: {found[3]}")
        (late / "L.h").unlink()
        removed = self.run("neg_probe", *PROBE)
        self.expect(removed[0] == 0 and self.has(removed[3], f"compiled (a search directory changed: {include})"),
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
                    and self.has(found[3], f"compiled (a search directory changed: {self.root / 'include'})"),
                    f"a new file with the missing name compiles the fixture again: {found[0]} {found[3]}")

    def check_macro_include(self) -> None:
        """A fixture that names its header with a macro stores, and a shadow of that header compiles it again."""
        computed = self.run("neg_computed", *CONVERT)
        self.expect(computed[0] == 0 and self.has(computed[3], "stored"),
                    f"the store takes the result of a macro include: {computed[3]}")
        shadow = self.root / "shadow/b"
        shadow.mkdir()
        (shadow / "B.h").write_text(CONVERTING_B)
        shadowed = self.run("neg_computed", *CONVERT)
        self.expect(shadowed[0] == 1 and "compiled successfully" in shadowed[2]
                    and self.has(shadowed[3], f"compiled (a search directory changed: {self.root / 'shadow'})"),
                    f"a shadow of the header that a macro names compiles the fixture again: "
                    f"{shadowed[0]} {shadowed[3]}")

    def check_quote_directory(self) -> None:
        """A new header in the directory of a fixture shadows its quote include, and compiles it again."""
        stored = self.run("neg_quote", *CONVERT)
        self.expect(stored[0] == 0 and self.has(stored[3], "stored"), f"the quote fixture stores: {stored[3]}")
        local = self.root / "neg/b/B.h"
        local.parent.mkdir()
        local.write_text(CONVERTING_B)
        shadowed = self.run("neg_quote", *CONVERT)
        self.expect(shadowed[0] == 1 and "compiled successfully" in shadowed[2]
                    and self.has(shadowed[3], f"compiled (a search directory changed: {self.root / 'neg'})"),
                    f"a header beside the fixture shadows its quote include: {shadowed[0]} {shadowed[3]}")

    def check_parent_name(self) -> None:
        """A new header at an earlier path of a name with `..` compiles the fixture again.

        The name `../b/B.h` reaches the directory above the tree from each
        directory before include/sub, and no search root holds that path.
        """
        stored = self.run("neg_parent", *CONVERT)
        self.expect(stored[0] == 0 and self.has(stored[3], "stored"), f"the parent fixture stores: {stored[3]}")
        earlier = self.root / "b/B.h"
        earlier.parent.mkdir()
        earlier.write_text(CONVERTING_B)
        shadowed = self.run("neg_parent", *CONVERT)
        self.expect(shadowed[0] == 1 and "compiled successfully" in shadowed[2]
                    and self.has(shadowed[3], f"compiled (a probed path changed: {self.root / 'shadow'}/../b/B.h)"),
                    f"a header at an earlier path of a name with .. compiles the fixture again: {shadowed[0]} "
                    f"{shadowed[3]}")

    def check_loader(self) -> None:
        """A shared object of cc1plus in a directory of LD_LIBRARY_PATH changes the key of the compile."""
        cc1plus = subprocess.run([self.cxx, "-print-prog-name=cc1plus"], text=True, capture_output=True,
                                 check=False).stdout.strip()
        interpreter = store.elf_interpreter(cc1plus) if os.path.isfile(cc1plus) else None
        if interpreter is None:
            self.skip(f"cc1plus ({cc1plus}) has no program interpreter")
            return
        listed = subprocess.run([interpreter, "--list", cc1plus], text=True, capture_output=True, check=False)
        parsed = store.parse_loader_list(listed.stdout, "", self.root)
        system = [path for path in (parsed[0] if parsed else []) if path.startswith(("/lib", "/usr/lib"))
                  and not os.path.basename(path).startswith("ld-linux")]
        if not system:
            self.skip(f"cc1plus maps no shared object from a system directory: {listed.stdout.strip()}")
            return
        chosen = next((path for path in system if os.path.basename(path).startswith("libz")), system[0])
        library = self.root / "lib"
        library.mkdir()
        stored = self.run("neg_convert", *CONVERT, LD_LIBRARY_PATH=str(library))
        self.expect(stored[0] == 0 and self.has(stored[3], "stored"),
                    f"the convert fixture stores with an empty LD_LIBRARY_PATH directory: {stored[3]}")
        shutil.copyfile(os.path.realpath(chosen), library / os.path.basename(chosen))
        self.settle()
        moved = self.run("neg_convert", *CONVERT, LD_LIBRARY_PATH=str(library))
        self.expect(moved[0] == 0 and self.has(moved[3], "compiled (no entry)") and self.has(moved[3], "stored"),
                    f"a copy of {chosen} in LD_LIBRARY_PATH gives a new key: {moved[0]} {moved[3]}")

    def check_message_language(self) -> None:
        """The driver compiles with LC_MESSAGES=C, so a host language with a GCC catalog gives English diagnostics."""
        source = self.root / "neg/neg_convert.cpp"
        base = {name: value for name, value in os.environ.items() if name not in ("LC_ALL", "LC_MESSAGES")}
        chosen = ""
        for candidate in ("en_US.UTF-8", "en_US.utf8", "en_GB.UTF-8", "de_DE.UTF-8"):
            raw = subprocess.run([self.cxx, "-fsyntax-only", "-I", str(self.root / "include"), str(source)],
                                 env={**base, "LC_ALL": candidate, "LANGUAGE": "de"}, text=True, capture_output=True,
                                 check=False)
            if raw.returncode == 1 and CONVERT[0] not in raw.stderr:
                chosen = candidate
                break
        if not chosen:
            self.skip("no locale of the host makes the compiler translate its diagnostics with LANGUAGE=de")
            return
        stored = self.run("neg_convert", *CONVERT, LC_ALL=chosen, LANGUAGE="de")
        self.expect(stored[0] == 0 and self.has(stored[3], "stored") and CONVERT[0] in stored[1],
                    f"with LC_ALL={chosen} and LANGUAGE=de the diagnostics are English: {stored[0]} {stored[3]}")

    def check_identity_change(self) -> None:
        """A compiler that changes during the compile stores no result.

        A wrapper compiler adds a comment line to its own script in each compile
        with `-c`.  The script is the compiler driver of the key, and it is no
        dependency, so only the key that the driver computes again after the
        compile sees the change.
        """
        wrapper = self.root / "wrapper.sh"
        wrapper.write_text(f'#!/bin/sh\ncase " $* " in *" -c "*) echo "# a change" >> "$0";; esac\n'
                           f'exec "{self.cxx}" "$@"\n')
        wrapper.chmod(0o755)
        rows = json.loads((self.build / "compile_commands.json").read_text())
        for row in rows:
            row["arguments"] = [str(wrapper), *row["arguments"][1:]]
        (self.build / "compile_commands.json").write_text(json.dumps(rows))
        self.settle()
        changed = self.run("neg_convert", *CONVERT)
        self.expect(changed[0] == 0 and self.has(changed[3], "not stored (the compiler identity changed during the "
                                                             "compile"),
                    f"a compiler that changes during the compile stores no result: {changed[0]} {changed[3]}")

    def check_store_off(self) -> None:
        """With CRUCIBLE_NEG_CACHE=0 or CRUCIBLE_CACHE_DIR=off, the driver compiles and writes no entry."""
        off = self.run("neg_convert", *CONVERT, CRUCIBLE_NEG_CACHE="0")
        self.expect(off[0] == 0 and off[3] == ["compiled (the store is off)"],
                    f"CRUCIBLE_NEG_CACHE=0 compiles: {off[3]}")
        self.expect(self.entry_files() == [], "a run with the store off writes no entry")
        every = self.run("neg_convert", *CONVERT, CRUCIBLE_CACHE_DIR="off")
        self.expect(every[0] == 0 and every[3] == ["compiled (CRUCIBLE_CACHE_DIR turns every cache off)"],
                    f"CRUCIBLE_CACHE_DIR=off compiles: {every[3]}")
        self.expect(self.entry_files() == [], "a run with every cache off writes no entry")

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
        planted = store.decode_entry(entry.read_bytes())
        self.expect(planted is not None, "the compile after a damaged entry stores a correct entry")
        if planted is None:
            return
        planted["results"][0]["output"] = "planted output\n"
        entry.write_bytes(store.encode_entry(planted))
        replaced = self.run("neg_convert", *CONVERT)
        self.expect(replaced[0] == 1 and "expected diagnostic not found for neg_convert" in replaced[2]
                    and replaced[1] == "planted output\n",
                    f"the regexes apply to the stored output: {replaced[0]} {replaced[3]}")
        planted["results"][0]["returncode"] = 0
        entry.write_bytes(store.encode_entry(planted))
        compiled = self.run("neg_convert", *CONVERT)
        self.expect(compiled[0] == 1 and "compiled successfully" in compiled[2],
                    f"a stored exit code 0 fails the fixture: {compiled[0]}")

    def check_cpu_budget(self) -> None:
        """With no instruction count, the driver judges the CPU time of a compile with the row fixture-cpu.

        CRUCIBLE_COUNT_INSTRUCTIONS=0 gives each compile no count, as a host
        with no counter does.  The entry keeps the CPU time.
        """
        table = self.root / "budgets.txt"
        ledger = self.root / "fixture-cpu-ledger.txt"
        count_ledger = self.root / "fixture-instructions-ledger.txt"
        warnings = self.root / "warnings"
        ledger.write_text("# no row\n")
        count_ledger.write_text("# no row\n")
        env = {"CRUCIBLE_NEG_BUDGETS": str(table), "CRUCIBLE_NEG_CPU_LEDGER": str(ledger),
               "CRUCIBLE_NEG_INSTRUCTIONS_LEDGER": str(count_ledger), "CRUCIBLE_COUNT_INSTRUCTIONS": "0"}

        def budget(warn: float, error: float) -> None:
            """Write a budget table with the row fixture-cpu and a quiet row fixture-instructions."""
            table.write_text(f"fixture-cpu | {warn} | {error} | s | the CPU time of one fixture compile\n"
                             f"fixture-instructions | 0 | 0 | G | a row that a compile with no count does not read\n")

        def finding(text: str, level: str) -> str | None:
            """Return the first finding line of the level in a standard output, or None."""
            return next((line for line in text.splitlines() if f": {level}: [fixture-cpu] " in line), None)

        budget(1000, 2000)
        quiet = self.run("neg_convert", *CONVERT, warnings_dir=warnings, **env)
        entry = self.entry_path(quiet[3])
        stored = self.newest_result(entry)
        self.expect(quiet[0] == 0 and finding(quiet[1], "warning") is None and stored is not None
                    and stored["user_s"] + stored["system_s"] > 0 and stored["instructions"] is None,
                    f"a compile under the budget gives no finding and stores its CPU time and no count: "
                    f"{quiet[0]} {quiet[3]}")
        budget(0, 2000)
        warned = self.run("neg_size", *SIZE, warnings_dir=warnings, **env)
        own_file = warnings / "fixture-cpu.neg_size.txt"
        line = finding(warned[1], "warning")
        self.expect(warned[0] == 0 and line is not None and "the compile of the fixture neg_size" in line
                    and own_file.is_file() and own_file.read_text().strip() == line,
                    f"a compile above the warning threshold warns in the file of its fixture: {warned[0]} {line}")
        hit = self.run("neg_size", *SIZE, warnings_dir=warnings, **env)
        line = finding(hit[1], "warning")
        self.expect(hit[0] == 0 and self.has(hit[3], "the result comes from the store") and line is not None
                    and "the stored compile of the fixture neg_size" in line and own_file.is_file(),
                    f"a result from the store warns with the time of its compile: {hit[0]} {hit[3]}")
        budget(1000, 2000)
        again = self.run("neg_size", *SIZE, warnings_dir=warnings, **env)
        self.expect(again[0] == 0 and not own_file.exists(), "a run under the budget removes the file of its fixture")
        budget(0, 0)
        failed = self.run("neg_convert", *CONVERT, warnings_dir=warnings, **env)
        line = finding(failed[1], "error")
        self.expect(failed[0] == 1 and line is not None and "more than the error threshold 0 s" in line
                    and self.has(failed[3], "compiled (the stored compile took more CPU time than the error threshold")
                    and self.has(failed[3], "not stored (the compile took more CPU time than the error threshold"),
                    f"a compile above the error threshold fails, and the driver measures it again: {failed[0]} "
                    f"{failed[3]}")
        demoted = self.run("neg_convert", *CONVERT, warnings_dir=warnings, GITHUB_ACTIONS="true", **env)
        line = finding(demoted[1], "warning")
        self.expect(demoted[0] == 0 and line is not None and "more than the error threshold 0 s" in line
                    and "demoted" in line and finding(demoted[1], "error") is None,
                    f"on a CI runner, a compile above the error threshold passes with a warning that says that the "
                    f"error was demoted: {demoted[0]} {line}")
        ledger.write_text("neg_convert | a planted reason\n")
        listed = self.run("neg_convert", *CONVERT, warnings_dir=warnings, **env)
        line = finding(listed[1], "warning")
        self.expect(listed[0] == 0 and line is not None and "a planted reason" in line,
                    f"a fixture with a ledger row above the error threshold warns: {listed[0]} {line}")
        budget(1000, 2000)
        stale = self.run("neg_convert", *CONVERT, warnings_dir=warnings, **env)
        line = finding(stale[1], "error")
        self.expect(stale[0] == 1 and line is not None and "Remove its row" in line,
                    f"a ledger row of a fixture at or below the error threshold fails: {stale[0]} {line}")
        ledger.write_text("neg_convert\n")
        malformed = self.run("neg_convert", *CONVERT, **env)
        line = finding(malformed[1], "error")
        self.expect(malformed[0] == 1 and line is not None and "a row is `fixture | note`" in line,
                    f"a malformed ledger row fails: {malformed[0]} {line}")
        ledger.write_text("# no row\n")
        table.write_text("other-check | 1 | 2 | s | another row\n")
        missing = self.run("neg_convert", *CONVERT, **env)
        line = finding(missing[1], "error")
        self.expect(missing[0] == 1 and line is not None and "has no row fixture-cpu" in line,
                    f"a budget table with no row fixture-cpu fails: {missing[0]} {line}")
        missing = self.run("neg_convert", *CONVERT, GITHUB_ACTIONS="true", **env)
        line = finding(missing[1], "error")
        self.expect(missing[0] == 1 and line is not None and "has no row fixture-cpu" in line,
                    f"on a CI runner, a budget that cannot be read still fails: {missing[0]} {line}")

    def check_instruction_budget(self) -> None:
        """With an instruction count, the row fixture-instructions holds the error level, and the CPU time warns only."""
        if store._cost_meter_module().open_instruction_counter() is None:
            self.skip("the host gives no exact instruction counter (utils/scripts/cost_meter.py)")
            return
        table = self.root / "budgets.txt"
        cpu_ledger = self.root / "fixture-cpu-ledger.txt"
        ledger = self.root / "fixture-instructions-ledger.txt"
        warnings = self.root / "warnings"
        cpu_ledger.write_text("# no row\n")
        ledger.write_text("# no row\n")
        env = {"CRUCIBLE_NEG_BUDGETS": str(table), "CRUCIBLE_NEG_CPU_LEDGER": str(cpu_ledger),
               "CRUCIBLE_NEG_INSTRUCTIONS_LEDGER": str(ledger)}

        def budget(cpu: tuple[float, float], count: tuple[float, float]) -> None:
            """Write a budget table with the rows fixture-cpu and fixture-instructions."""
            table.write_text(f"fixture-cpu | {cpu[0]} | {cpu[1]} | s | the CPU time of one fixture compile\n"
                             f"fixture-instructions | {count[0]} | {count[1]} | G | the instructions of one compile\n")

        def finding(text: str, level: str, check: str) -> str | None:
            """Return the first finding line of the level and the check in a standard output, or None."""
            return next((line for line in text.splitlines() if f": {level}: [{check}] " in line), None)

        budget((0, 0), (1000, 2000))
        loaded = self.run("neg_convert", *CONVERT, warnings_dir=warnings, **env)
        entry = self.entry_path(loaded[3])
        stored = self.newest_result(entry)
        line = finding(loaded[1], "warning", "fixture-cpu")
        self.expect(loaded[0] == 0 and line is not None and "fixture-instructions holds the error level" in line
                    and finding(loaded[1], "error", "fixture-cpu") is None and stored is not None
                    and isinstance(stored["instructions"], int) and stored["instructions"] > 0,
                    f"a compile with a count over the CPU error threshold passes with a warning, and its entry keeps "
                    f"the count: {loaded[0]} {line} {loaded[3]}")
        budget((1000, 2000), (0, 2000))
        warned = self.run("neg_convert", *CONVERT, warnings_dir=warnings, **env)
        own_file = warnings / "fixture-instructions.neg_convert.txt"
        line = finding(warned[1], "warning", "fixture-instructions")
        self.expect(warned[0] == 0 and self.has(warned[3], "the result comes from the store") and line is not None
                    and "the stored compile of the fixture neg_convert ran" in line and own_file.is_file()
                    and own_file.read_text().strip() == line,
                    f"a stored count above the warning threshold warns in the file of its fixture: {warned[0]} {line}")
        budget((1000, 2000), (0, 0))
        failed = self.run("neg_convert", *CONVERT, warnings_dir=warnings, **env)
        line = finding(failed[1], "error", "fixture-instructions")
        self.expect(failed[0] == 1 and line is not None and "more than the error threshold 0 G" in line
                    and self.has(failed[3], "compiled (the stored compile ran more user instructions than the error")
                    and self.has(failed[3], "not stored (the compile ran more user instructions than the error"),
                    f"a count above the error threshold fails, and the driver does not store it: {failed[0]} "
                    f"{failed[3]}")
        ci_run = self.run("neg_convert", *CONVERT, warnings_dir=warnings, GITHUB_ACTIONS="true", **env)
        self.expect(ci_run[0] == 1 and finding(ci_run[1], "error", "fixture-instructions") is not None,
                    f"on a CI runner, a count above the error threshold still fails: {ci_run[0]}")
        ledger.write_text("neg_convert | a planted reason\n")
        listed = self.run("neg_convert", *CONVERT, warnings_dir=warnings, **env)
        line = finding(listed[1], "warning", "fixture-instructions")
        self.expect(listed[0] == 0 and line is not None and "a planted reason" in line,
                    f"a fixture with a ledger row above the error threshold warns: {listed[0]} {line}")
        budget((1000, 2000), (1000, 2000))
        stale = self.run("neg_convert", *CONVERT, warnings_dir=warnings, **env)
        line = finding(stale[1], "error", "fixture-instructions")
        self.expect(stale[0] == 1 and line is not None and "Remove its row from utils/scripts/fixture-instructions"
                    in line, f"a ledger row of a fixture at or below the error threshold fails: {stale[0]} {line}")
        ledger.write_text("# no row\n")
        table.write_text("fixture-cpu | 1000 | 2000 | s | the CPU time of one fixture compile\n")
        missing = self.run("neg_convert", *CONVERT, **env)
        line = finding(missing[1], "error", "fixture-instructions")
        self.expect(missing[0] == 1 and line is not None and "has no row fixture-instructions" in line,
                    f"a budget table with no row fixture-instructions fails: {missing[0]} {line}")

    def check_inputs_record(self) -> None:
        """Each run writes NAME.inputs: its result, its cost and each file that its compile read."""

        def record(fixture: str) -> tuple[dict[str, object], list[str]]:
            """Return the header and the paths of the record of one fixture, or an empty header."""
            path = self.build / "neg-compile" / fixture / f"{fixture}.inputs"
            try:
                first, *paths = path.read_text().splitlines()
                return json.loads(first), paths
            except (OSError, ValueError):
                return {}, []

        source = str(self.root / "neg/neg_size.cpp")
        header_file = str(self.root / "include/a/A.h")
        compiled = self.run("neg_size", *SIZE)
        head, paths = record("neg_size")
        self.expect(compiled[0] == 0 and head.get("result") == "compiled" and head.get("has_inputs") is True
                    and head.get("format") == 1 and float(head.get("user_s", -1)) > 0
                    and source in paths and header_file in paths and len(paths) == len(set(paths)),
                    f"a compile writes a record with its cost and its inputs: {head} {paths}")
        stored = self.run("neg_size", *SIZE)
        stored_head, stored_paths = record("neg_size")
        self.expect(self.has(stored[3], "the result comes from the store") and stored_head.get("result") == "stored"
                    and stored_paths == paths and stored_head.get("user_s") == head.get("user_s")
                    and stored_head.get("instructions") == head.get("instructions"),
                    f"a result from the store writes the inputs and the cost of the compile that made it: "
                    f"{stored_head} {stored_paths}")
        missing = self.run("neg_missing", *MISSING, CRUCIBLE_NEG_CACHE="0")
        fatal_head, fatal_paths = record("neg_missing")
        self.expect(missing[0] == 0 and fatal_head.get("has_inputs") is True
                    and str(self.root / "neg/neg_missing.cpp") in fatal_paths,
                    f"a fatal error with the store off writes the inputs of the -M -MG pass: {fatal_head} "
                    f"{fatal_paths}")

    def check_several_results(self) -> None:
        """An entry keeps the result of earlier inputs, so the undo of an edit uses the store."""
        header = self.root / "include/a/A.h"
        original = header.read_text()
        first = self.run("neg_size", *SIZE)
        entry = self.entry_path(first[3])
        self.expect(self.has(first[3], "stored") and len(self.results_of(entry)) == 1,
                    f"the first compile stores one result: {first[3]}")
        header.write_text(original + "// an edit\n")
        self.settle()
        edited = self.run("neg_size", *SIZE)
        self.expect(self.has(edited[3], f"compiled (a dependency changed: {header})") and self.has(edited[3], "stored")
                    and len(self.results_of(entry)) == 2,
                    f"an edit adds a second result to the entry: {edited[3]} {len(self.results_of(entry))}")
        header.write_text(original)
        undone = self.run("neg_size", *SIZE)
        self.expect(undone[0] == 0 and self.has(undone[3], "the result comes from the store") and undone[1] == first[1],
                    f"the undo of the edit uses the earlier result: {undone[3]}")
        self.expect(len(self.results_of(entry)) == 2, "a result from the store writes no result")
        header.write_text(original + "// an edit\n")
        redone = self.run("neg_size", *SIZE)
        self.expect(self.has(redone[3], "the result comes from the store"),
                    f"the edit again uses the newest result: {redone[3]}")

    def check_command_memo(self) -> None:
        """The driver keeps the command of a fixture, and uses it only while the compile database is the same."""
        memo = self.build / "neg-compile" / "neg_size" / f"neg_size{store._COMMAND_SUFFIX}"
        database = self.build / "compile_commands.json"
        fresh = self.run("neg_size", *SIZE, **self.started_at(database))
        self.expect(fresh[0] == 0 and not memo.exists(),
                    f"the driver keeps no command while the compile database is new: {fresh[0]} {fresh[3]}")
        first = self.run("neg_size", *SIZE)
        try:
            recorded = json.loads(memo.read_text())
        except (OSError, ValueError):
            recorded = {}
        command = recorded.get("command")
        self.expect(first[0] == 0 and isinstance(command, list), f"the driver keeps the command: {first[0]} {recorded}")
        if not isinstance(command, list):
            return
        recorded["command"] = [*command, "-Dint=char"]
        memo.write_text(json.dumps(recorded))
        planted = self.run("neg_size", *SIZE)
        self.expect(planted[0] == 1 and "compiled successfully" in planted[2],
                    f"the driver uses the kept command while the compile database is the same: {planted[0]} "
                    f"{planted[3]}")
        database.write_text(database.read_text())
        rewritten = self.run("neg_size", *SIZE)
        self.expect(rewritten[0] == 0 and self.has(rewritten[3], "the result comes from the store"),
                    f"a rewritten compile database replaces the kept command: {rewritten[0]} {rewritten[3]}")

    def check_driver_answers(self) -> None:
        """A new assembler on a path that the compiler driver searches gives a new key, also with a memo of answers.

        COMPILER_PATH puts an empty directory first in the program search of
        the driver.  The first run keeps the answers of the driver in a memo,
        and a new program `as` in that directory changes the answer for the
        assembler.
        """
        programs = self.root / "programs"
        programs.mkdir()
        env = {"COMPILER_PATH": str(programs)}
        stored = self.run("neg_convert", *CONVERT, **env)
        memos = sorted((self.store / "memo").glob("answers-*.json"))
        self.expect(stored[0] == 0 and self.has(stored[3], "stored") and len(memos) == 1,
                    f"the first run stores and keeps the answers of the driver: {stored[3]} {memos}")
        again = self.run("neg_convert", *CONVERT, **env)
        self.expect(self.has(again[3], "the result comes from the store"),
                    f"the answers of the memo give the same key: {again[3]}")
        assembler = programs / "as"
        assembler.write_text('#!/bin/sh\nexec as "$@"\n')
        assembler.chmod(0o755)
        planted = self.run("neg_convert", *CONVERT, **env)
        self.expect(planted[0] == 0 and self.has(planted[3], "compiled (no entry)"),
                    f"a new assembler in COMPILER_PATH gives a new key: {planted[3]}")

    def check_digest_memo(self) -> None:
        """The digest memo of a fixture gives a kept hash only while the stat fields of the file are the same.

        An edit that keeps the size of a header changes its change time, so
        the driver reads the header again.
        """
        header = self.root / "include/a/A.h"
        memo = self.build / "neg-compile" / "neg_size" / f"neg_size{store._DIGESTS_SUFFIX}"
        self.run("neg_size", *SIZE)
        second = self.run("neg_size", *SIZE)
        try:
            kept = json.loads(memo.read_text())["files"]
        except (OSError, ValueError, KeyError):
            kept = {}
        self.expect(self.has(second[3], "the result comes from the store") and str(header) in kept,
                    f"a result from the store keeps the hash of each dependency: {second[3]} {sorted(kept)}")
        text = header.read_text()
        same_size = text.replace("int field;", "char fiel;")
        self.expect(len(same_size) == len(text), "the edit keeps the size of the header")
        header.write_text(same_size)
        edited = self.run("neg_size", *SIZE)
        self.expect(edited[0] == 1 and "compiled successfully" in edited[2]
                    and self.has(edited[3], f"compiled (a dependency changed: {header})"),
                    f"an edit of a header that keeps its size compiles the fixture again: {edited[0]} {edited[3]}")

    def check_budget_memo(self) -> None:
        """The budget memo gives the rows only while the table and the two ledgers have the same stat fields."""
        table = self.root / "budgets.txt"
        cpu_ledger = self.root / "fixture-cpu-ledger.txt"
        count_ledger = self.root / "fixture-instructions-ledger.txt"
        cpu_ledger.write_text("# no row\n")
        count_ledger.write_text("# no row\n")
        env = {"CRUCIBLE_NEG_BUDGETS": str(table), "CRUCIBLE_NEG_CPU_LEDGER": str(cpu_ledger),
               "CRUCIBLE_NEG_INSTRUCTIONS_LEDGER": str(count_ledger)}

        def budget(warn: float) -> None:
            """Write a budget table whose two rows have the warning threshold `warn` and a high error threshold."""
            table.write_text(f"fixture-cpu | {warn} | 2000 | s | the CPU time of one fixture compile\n"
                             f"fixture-instructions | {warn} | 2000 | G | the instructions of one compile\n")

        def warning(text: str) -> str | None:
            """Return the first warning line of a fixture budget in a standard output, or None."""
            return next((line for line in text.splitlines() if ": warning: [fixture-" in line), None)

        budget(1000)
        self.settle()
        quiet = self.run("neg_convert", *CONVERT, **env)
        memos = sorted((self.store / "memo").glob("budget-*.json"))
        self.expect(quiet[0] == 0 and warning(quiet[1]) is None and len(memos) == 1,
                    f"a run under the budget gives no warning and keeps the settled budget: {quiet[0]} {memos}")
        budget(0)
        warned = self.run("neg_convert", *CONVERT, **env)
        self.expect(warned[0] == 0 and warning(warned[1]) is not None,
                    f"a new budget table replaces the kept budget: {warned[0]} {warned[3]}")

    def check_cross_root_hit(self) -> None:
        """A second work tree of the same files uses the result of the first, and its output names its own paths."""
        twin = self.twin()
        self.settle()
        first = self.run("neg_size", *SIZE)
        own_source = re.escape(str(twin.root / "neg" / "neg_size.cpp"))
        shared = twin.run("neg_size", *SIZE, own_source)
        self.expect(self.has(first[3], "stored") and self.has(shared[3], "the result comes from the store")
                    and shared[0] == 0 and self.entry_path(first[3]) == twin.entry_path(shared[3]),
                    f"the second tree uses the entry of the first: {first[3]} {shared[0]} {shared[3]}")
        self.expect(str(twin.root / "neg") in shared[1] and f"{self.root}/" not in shared[1],
                    f"the stored output names the paths of the second tree: {shared[1]}")
        foreign = twin.run("neg_size", *SIZE, re.escape(str(self.root / "neg" / "neg_size.cpp")))
        self.expect(foreign[0] == 1 and "expected diagnostic not found" in foreign[2],
                    f"a regex sees the paths of the tree that runs only: {foreign[0]} {foreign[3]}")

    def check_cross_root_text(self) -> None:
        """A dependency that holds the text of the root of the reading tree gives no result from a different root.

        The header A.h of both trees compares its own __FILE__ with its path in
        the second tree.  The first tree stores its result.  In the second tree
        the assertion of the header fails, so the stored output is stale there.
        """
        twin = self.twin()
        header = ("#pragma once\n"
                  "constexpr bool same_text(const char* left, const char* right) {\n"
                  "    while (*left != '\\0' && *left == *right) { ++left; ++right; }\n"
                  "    return *left == *right;\n"
                  "}\n"
                  f'static_assert(!same_text(__FILE__, "{twin.root}/include/a/A.h"),\n'
                  '              "the header is in the second tree");\n'
                  "struct A { int field; };\n")
        for tree in (self, twin):
            (tree.root / "include/a/A.h").write_text(header)
        self.settle()
        first = self.run("neg_size", *SIZE)
        self.expect(first[0] == 0 and self.has(first[3], "stored")
                    and "the header is in the second tree" not in first[1],
                    f"the first tree stores its result: {first[0]} {first[3]}")
        twin_header = twin.root / "include/a/A.h"
        second = twin.run("neg_size", *SIZE, "the header is in the second tree")
        self.expect(second[0] == 0 and twin.has(second[3], f"compiled (a dependency holds the text of a root: "
                                                           f"{twin_header})")
                    and twin.has(second[3], f"not stored (a dependency holds the text of a root: {twin_header})"),
                    f"the second tree compiles, and its header fails: {second[0]} {second[3]}")

    def check_cross_root_edit(self) -> None:
        """A second tree with a different header, or a new name in a search root, takes no result of the first tree."""
        twin = self.twin()
        twin_header = twin.root / "include/a/A.h"
        twin_header.write_text("#pragma once\nstruct A { char field; };\n")
        self.settle()
        self.run("neg_size", *SIZE)
        edited = twin.run("neg_size", *SIZE)
        self.expect(edited[0] == 1 and "compiled successfully" in edited[2]
                    and twin.has(edited[3], f"compiled (a dependency changed: {twin_header})"),
                    f"a different header in the second tree compiles the fixture there: {edited[0]} {edited[3]}")
        self.run("neg_convert", *CONVERT)
        shared = twin.run("neg_convert", *CONVERT)
        self.expect(twin.has(shared[3], "the result comes from the store"),
                    f"the second tree takes the result of a fixture whose inputs are the same: {shared[3]}")
        (twin.root / "include/b/Other.h").write_text("#pragma once\n")
        rooted = twin.run("neg_convert", *CONVERT)
        self.expect(rooted[0] == 0
                    and twin.has(rooted[3], f"compiled (a search directory changed: {twin.root / 'include'})"),
                    f"a new name in a search directory of the second tree compiles the fixture there: {rooted[3]}")

    def check_eviction(self) -> None:
        """A write into a bucket above its share removes the entries of that bucket unused for the longest time.

        With a limit of 4 MB, the entries use 15/16 of it, so each of the 256
        buckets has a share of 15360 bytes on the disk.  The bucket of the entry
        gets four planted entries of 8192 bytes, and a different bucket gets one
        planted entry larger than the full limit.  The write reads only its own
        bucket, so the large entry stays.
        """
        first = self.run("neg_convert", *CONVERT)
        entry = self.entry_path(first[3])
        self.expect(entry is not None and entry.is_file(), f"the convert fixture has an entry: {first[3]}")
        if entry is None or not entry.is_file():
            return
        entry.unlink()
        share = int((4 << 20) * (1 - store._MEMO_PART)) // store._BUCKETS
        unused = []
        for index in range(4):
            path = entry.parent / f"{index:064x}"
            path.write_bytes(bytes(8192))
            moment = time.time() - 86400 + index
            os.utime(path, (moment, moment))
            unused.append(path)
        other = self.store / "entries" / ("01" if entry.parent.name == "00" else "00") / f"{9:064x}"
        other.parent.mkdir(parents=True, exist_ok=True)
        other.write_bytes(bytes(8 << 20))
        moment = time.time() - 2 * 86400
        os.utime(other, (moment, moment))
        stored = self.run("neg_convert", *CONVERT, CRUCIBLE_NEG_CACHE_MAX_MB="4")
        remaining = [path for path in unused if path.exists()]
        bucket_total = sum(path.stat().st_blocks * 512 for path in entry.parent.iterdir())
        self.expect(self.has(stored[3], "stored") and entry.is_file(), f"the new entry stays: {stored[3]}")
        self.expect(len(remaining) <= 1 and remaining == unused[len(unused) - len(remaining):]
                    and (bucket_total <= share or not remaining),
                    f"the entries of the bucket unused for the longest time go: {[path.name[-1] for path in remaining]}")
        self.expect(other.is_file(), "a write does not remove an entry of a different bucket")

    def check_memo_sweep(self) -> None:
        """A write sweeps the memos when they use more than their part of the limit, the oldest write first.

        With a limit of 4 MB, the memos can use 256 KB.  The check plants 64
        memos of 8192 bytes with an old write, 512 KB on the disk, and a sweep
        stamp older than the sweep interval.  A write then removes the oldest
        memos until the memos use at most 90 percent of their part.
        """
        (self.store / "memo").mkdir(parents=True, exist_ok=True)
        planted = []
        for index in range(64):
            path = self.store / "memo" / f"file-{index:032x}.json"
            path.write_bytes(bytes(8192))
            moment = time.time() - 86400 + index
            os.utime(path, (moment, moment))
            planted.append(path)
        stamp = self.store / store._MEMO_SWEEP_STAMP
        stamp.touch()
        moment = time.time() - 2 * store._MEMO_SWEEP_SECONDS
        os.utime(stamp, (moment, moment))
        stored = self.run("neg_convert", *CONVERT, CRUCIBLE_NEG_CACHE_MAX_MB="4")
        part = int((4 << 20) * store._MEMO_PART)
        memo_total = sum(path.stat().st_blocks * 512 for path in (self.store / "memo").iterdir())
        remaining = [path for path in planted if path.exists()]
        self.expect(self.has(stored[3], "stored"), f"the convert fixture stores its result: {stored[3]}")
        self.expect(memo_total <= part and remaining == planted[len(planted) - len(remaining):],
                    f"the memos with the oldest write go until the memos fit their part: {memo_total} bytes on the "
                    f"disk for a part of {part}, {len(remaining)} planted memos remain")


CHECKS: tuple[Callable[[StoreTest], None], ...] = (
    StoreTest.check_hit_and_header_edit,
    StoreTest.check_has_include,
    StoreTest.check_shadow_header,
    StoreTest.check_precompiled_header,
    StoreTest.check_unrelated_file,
    StoreTest.check_fatal_error,
    StoreTest.check_macro_include,
    StoreTest.check_quote_directory,
    StoreTest.check_parent_name,
    StoreTest.check_loader,
    StoreTest.check_message_language,
    StoreTest.check_identity_change,
    StoreTest.check_store_off,
    StoreTest.check_stored_output_is_evaluated,
    StoreTest.check_eviction,
    StoreTest.check_memo_sweep,
    StoreTest.check_cpu_budget,
    StoreTest.check_instruction_budget,
    StoreTest.check_inputs_record,
    StoreTest.check_command_memo,
    StoreTest.check_several_results,
    StoreTest.check_driver_answers,
    StoreTest.check_digest_memo,
    StoreTest.check_budget_memo,
    StoreTest.check_cross_root_hit,
    StoreTest.check_cross_root_text,
    StoreTest.check_cross_root_edit,
)

SEARCH_LIST = """\
ignoring nonexistent directory "/opt/gcc/x86_64/include"
ignoring duplicate directory "/opt/inc/./"
  as it is a non-system directory that duplicates a system directory
#include "..." search starts here:
 /work/quote
#include <...> search starts here:
 /work/include
 /opt/gcc/include/c++
 /Library/Frameworks (framework directory)
End of search list.
"""
LOADER_OUTPUT = """\
\tlinux-vdso.so.1 (0x00007ffd)
\tlibz.so.1 => /lib64/libz.so.1 (0x00007f01)
\t/lib64/ld-linux-x86-64.so.2 (0x00007f02)
"""
LOADER_DEBUG = """\
     1234:\tfind library=libz.so.1 [0]; searching
     1234:\t search path=/opt/lib:glibc-hwcaps/x86-64-v3:\t\t(LD_LIBRARY_PATH)
     1234:\t  trying file=/opt/lib/libz.so.1
     1234:\t  trying file=glibc-hwcaps/x86-64-v3/libz.so.1
     1234:\t search cache=/etc/ld.so.cache
     1234:\t  trying file=/lib64/libz.so.1
"""


def check_parsers(failures: list[str]) -> None:
    """Check the parsers and the pure functions of the store on planted input."""
    parsed = store.parse_dependency_file("out.o: a\\ b.h c.h \\\n d$$.h\\#e.h\n")
    if parsed != ["a b.h", "c.h", "d$.h#e.h"]:
        failures.append(f"the dependency file parse gives {parsed}")
    if store.parse_dependency_file("out.o: a.h\nb.h: c.h\n") is not None:
        failures.append("a dependency file with two rules is refused")
    named = store.parse_search_list(SEARCH_LIST)
    expected_named = ["/opt/gcc/x86_64/include", "/opt/inc/./", "/work/quote", "/work/include", "/opt/gcc/include/c++",
                      "/Library/Frameworks"]
    if named != expected_named:
        failures.append(f"the search list parse gives {named}")
    if store.parse_search_list(SEARCH_LIST.replace("End of search list.\n", "")) is not None:
        failures.append("a search list with no end line is refused")
    loader = store.parse_loader_list(LOADER_OUTPUT, LOADER_DEBUG, Path("/work"))
    expected_loader = (["/lib64/libz.so.1", "/lib64/ld-linux-x86-64.so.2"],
                       ["/opt/lib/libz.so.1", "/work/glibc-hwcaps/x86-64-v3/libz.so.1", "/lib64/libz.so.1"],
                       ["/etc/ld.so.cache"])
    if loader != expected_loader:
        failures.append(f"the loader list parse gives {loader}")
    if store.parse_loader_list("\tlibq.so.1 => not found\n", "", Path("/work")) is not None:
        failures.append("a loader list with an object that the loader does not find is refused")
    interpreter = store.elf_interpreter(os.path.realpath(sys.executable))
    if interpreter is None or not os.path.isfile(interpreter):
        failures.append(f"the interpreter of {sys.executable} is {interpreter}")
    if store.elf_interpreter(str(DRIVER)) is not None:
        failures.append("a file that is not ELF has no interpreter")
    roots, probes = store.search_roots(["/s/sub"], ["/n/x.cpp", "/s/sub/../b/B.h"], ["/abs/m.h", "../m.h", "m.h"])
    expected_probes = [path for reach in ("/s/sub/../b/B.h", "/n/../b/B.h", "/s/sub/../b/../b/B.h", "/s/sub/../m.h",
                                          "/n/../m.h", "/s/sub/../b/../m.h", "/abs/m.h")
                       for path in (reach, reach + ".gch")]
    if roots != ["/s/sub", "/n", "/s/sub/../b"] or probes != expected_probes:
        failures.append(f"the search roots are {roots} and the probes are {probes}")
    env = store.compile_environment({"LC_ALL": "de_DE.UTF-8", "LANG": "en_US.UTF-8", "LANGUAGE": "de"})
    if env != {"LC_CTYPE": "de_DE.UTF-8", "LANG": "en_US.UTF-8", "LANGUAGE": "de", "LC_MESSAGES": "C"}:
        failures.append(f"the compile environment is {env}")
    report = store._report_module()
    if store._FIXTURE_NAME.pattern != report.WRITER_KEY.pattern:
        failures.append(f"the fixture name pattern {store._FIXTURE_NAME.pattern} is not WRITER_KEY of check_report.py")
    with tempfile.TemporaryDirectory(prefix="neg-warnings-") as scratch:
        warnings_dir = Path(scratch)
        report.write_warnings(warnings_dir, "fixture-cpu", [report.Finding("warning", "x.cpp", 0, "fixture-cpu", "m")],
                              "neg_one")
        if not store._warnings_file(warnings_dir, "fixture-cpu", "neg_one").is_file():
            failures.append("the warnings file of a fixture has the name that check_report.write_warnings gives it")
    check_roots(failures)
    check_entry_format(failures)
    check_kernel_time(failures)


def check_kernel_time(failures: list[str]) -> None:
    """Check that the CPU budget reads the user and the system time, so a compile with no user time still counts.

    The kernel divides the exact CPU time of a process between the user time
    and the system time from the samples of the timer tick.  A process of a
    few milliseconds whose samples all fall in the kernel gets a user time of
    0 s: `dd if=/dev/zero of=/dev/null bs=1M count=400` gave 0 s of user time
    in 42 of 50 runs on the build host.  The steps of check_cpu_budget plant a
    threshold of 0 s, so such a compile failed them under load.
    """
    def budget(warn: float, error: float) -> store.CostBudget:
        """Return a budget whose row fixture-cpu has the two thresholds, and whose count row is quiet."""
        return store.CostBudget({
            "fixture-cpu": store.BudgetRow("fixture-cpu", warn, error, {}, "fixture-cpu-ledger.txt"),
            "fixture-instructions": store.BudgetRow("fixture-instructions", 1000.0, 2000.0, {},
                                                    "fixture-instructions-ledger.txt"),
        }, {})

    kernel_only = store.CompileResult(1, "", 0.0, 0.004, None)
    source = Path("/w/neg/neg_kernel.cpp")
    with contextlib.redirect_stdout(io.StringIO()) as printed:
        status = store.report_cost("neg_kernel", source, kernel_only, False, budget(0.0, 2000.0), None)
    line = next((text for text in printed.getvalue().splitlines() if ": warning: [fixture-cpu] " in text), None)
    if status != 0 or line is None or "0.00 s of user time and 0.00 s of system time" not in line:
        failures.append(f"a compile with 0 s of user time and 4 ms of system time warns over a threshold of 0 s: "
                        f"{status} {line}")
    with contextlib.redirect_stdout(io.StringIO()) as printed:
        status = store.report_cost("neg_kernel", source, kernel_only, False, budget(0.0, 0.0), None)
    if status != 1 or ": error: [fixture-cpu] " not in printed.getvalue():
        failures.append(f"a compile with 0 s of user time fails an error threshold of 0 s: {status}")
    if not budget(0.0, 0.0).is_over_error("neg_kernel", kernel_only):
        failures.append("the store measures a compile with 0 s of user time again over an error threshold of 0 s")


def check_roots(failures: list[str]) -> None:
    """Check the marks of the roots, the roots of a CMakeCache.txt and the refusal of an output with a NUL character."""
    with tempfile.TemporaryDirectory(prefix="neg-roots-") as scratch:
        base = Path(scratch).resolve()
        source, build = f"{base}/src", f"{base}/src/build"
        roots = store.Roots(source, build)
        text = f"{build}/x.o: {source}/include/a.h, {source}x and {base}/other"
        relabeled = roots.relabel(text)
        expected = f"\x00build root\x00/x.o: \x00source root\x00/include/a.h, \x00source root\x00x and {base}/other"
        if relabeled != expected or roots.restore(relabeled) != text:
            failures.append(f"the marks of a build root inside the source root give {relabeled!r}")
        if store.Roots("/", "/tmp").identity():
            failures.append("a root of fewer than two components takes no part in the marks")
        if not roots.holds_root(f"// {source}/x.h\n".encode()) or roots.holds_root(f"// {base}/srd\n".encode()):
            failures.append("a byte search finds the text of a root, and only that text")
        (base / "CMakeCache.txt").write_text(f"OTHER:STRING=x\nCMAKE_HOME_DIRECTORY:INTERNAL={source}\n"
                                             f"CMAKE_CACHEFILE_DIR:INTERNAL={build}\n")
        if store.build_roots(base).identity() != [build, source]:
            failures.append(f"the roots of a CMakeCache.txt are {store.build_roots(base).identity()}")
        if store.build_roots(base / "src").identity():
            failures.append("a build directory with no CMakeCache.txt has no root")
        dependency = base / "dependency.h"
        dependency.write_text("int value;\n")
        (base / "store" / "memo").mkdir(parents=True)
        result_store = store.ResultStore(base / "store", 1 << 20)
        is_stored, why = result_store.record("0" * 64, store.CompileResult(1, "a\x00b\n", 0.1, 0.0, None),
                                             [str(dependency)], [], [str(base)], time.time_ns() + 2 * store._SETTLE_NS,
                                             roots)
        if is_stored or "NUL" not in why:
            failures.append(f"the store refuses an output with a NUL character: {is_stored} {why}")


def planted_result(digest: str, output: str = "planted output\n") -> dict[str, object]:
    """Return a result whose first dependency has the hash `digest`."""
    return {"dependencies": [["/w/a.h", digest], ["/w/b.h", "b" * 32]], "roots": [["/w", ["dir", "/w"]]],
            "probes": [], "listings": [["/w", "c" * 32]], "output": output, "returncode": 1, "user_s": 0.5,
            "system_s": 0.1, "instructions": None}


def check_entry_format(failures: list[str]) -> None:
    """Check the packed entry, its decode and the merge of a new result on planted results."""
    count = store._RESULTS_PER_ENTRY
    results = [planted_result(f"{index:032x}") for index in range(count)]
    entry = store.pack_entry(results)
    if entry["paths"] != ["/w/a.h", "/w/b.h"] or store.unpack_results(entry) != results:
        failures.append(f"an entry keeps each path one time and gives its results back: {entry['paths']}")
    if store.decode_entry(store.encode_entry(entry)) != entry:
        failures.append("a packed entry decodes")
    too_many = store.pack_entry([*results, planted_result("f" * 32)])
    if store.decode_entry(store.encode_entry(too_many)) is not None:
        failures.append(f"an entry with more than {count} results is refused")
    outside = store.pack_entry(results[:1])
    outside["results"][0]["dependencies"][0][0] = 2
    if store.decode_entry(store.encode_entry(outside)) is not None:
        failures.append("a dependency index outside the path list is refused")
    newest = planted_result("e" * 32, "newest output\n")
    if store.merge_results(newest, results) != [newest, *results[: count - 1]]:
        failures.append("a new result goes first, and the oldest result goes from a full entry")
    same = planted_result(f"{1:032x}", "the same inputs\n")
    if store.merge_results(same, results[:3]) != [same, results[0], results[2]]:
        failures.append("a new result replaces the earlier result with the same inputs")


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
    # A time error becomes a warning when GITHUB_ACTIONS is true (cost_meter.ci_verdict).  The checks in this
    # process expect the levels of the build host.  Remove the variable, as run() does for each run of the driver.
    os.environ.pop("GITHUB_ACTIONS", None)
    failures: list[str] = []
    skips: list[str] = []
    if index == 0:
        check_parsers(failures)
    checks = CHECKS[index::count]
    for check in checks:
        with tempfile.TemporaryDirectory(prefix="neg-store-") as directory:
            test = StoreTest(Path(directory) / "tree", cxx)
            test.settle()
            check(test)
            failures.extend(f"{check.__name__}: {failure}" for failure in test.failures)
            if test.skipped:
                skips.append(f"{check.__name__}: {test.skipped}")
    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    for skip in skips:
        print(f"SKIP: {skip}", file=sys.stderr)
    print(f"neg_compile_driver_test.py part {index}/{count}: {len(failures)} failures, {len(skips)} skips",
          file=sys.stderr)
    if failures:
        return 1
    return 3 if checks and len(skips) == len(checks) and index != 0 else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
