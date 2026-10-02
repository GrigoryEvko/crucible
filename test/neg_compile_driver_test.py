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
the loader, the ELF interpreter, the search roots and the compile environment.

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
        """Make the tree of headers, fixtures and compile database in `root`.

        The tree uses the real path of `root`, because the driver names each
        search directory by its real path.  The store is in the build
        directory: a new directory in `root` changes the time that the store
        reads for each path in `root` that does not exist.
        """
        self.root = root.resolve()
        self.cxx = cxx
        self.build = self.root / "build"
        self.store = self.build / "store"
        self.failures: list[str] = []
        self.skipped = ""
        for name, text in FILES.items():
            (self.root / name).parent.mkdir(parents=True, exist_ok=True)
            (self.root / name).write_text(text)
        (self.root / "shadow").mkdir()
        self.build.mkdir()
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
        env["CRUCIBLE_NEG_CACHE_DIR"] = str(self.store)
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

    def entry_files(self) -> list[Path]:
        """Return each file in the entry directory of the store, in sorted sequence."""
        return sorted(path for path in (self.store / "entries").rglob("*") if path.is_file())

    @staticmethod
    def settle() -> None:
        """Wait until each file of the tree is older than the settle period of the store."""
        time.sleep(driver._SETTLE_NS / 1e9 + 0.2)

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
        interpreter = driver.elf_interpreter(cc1plus) if os.path.isfile(cc1plus) else None
        if interpreter is None:
            self.skip(f"cc1plus ({cc1plus}) has no program interpreter")
            return
        listed = subprocess.run([interpreter, "--list", cc1plus], text=True, capture_output=True, check=False)
        parsed = driver.parse_loader_list(listed.stdout, "", self.root)
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
        """With CRUCIBLE_NEG_CACHE=0, the driver compiles and writes no entry."""
        off = self.run("neg_convert", *CONVERT, CRUCIBLE_NEG_CACHE="0")
        self.expect(off[0] == 0 and off[3] == ["compiled (the store is off)"],
                    f"CRUCIBLE_NEG_CACHE=0 compiles: {off[3]}")
        self.expect(self.entry_files() == [], "a run with the store off writes no entry")

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
        stored = driver.decode_entry(entry.read_bytes()) if entry is not None and entry.is_file() else None
        self.expect(quiet[0] == 0 and finding(quiet[1], "warning") is None and stored is not None
                    and stored["user_s"] > 0 and stored["instructions"] is None,
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
        if driver._cost_meter_module().open_instruction_counter() is None:
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
        stored = driver.decode_entry(entry.read_bytes()) if entry is not None and entry.is_file() else None
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

    def check_eviction(self) -> None:
        """A write into a bucket above its share removes the entries of that bucket unused for the longest time.

        With a limit of 1 MB, each of the 256 buckets has a share of 4096
        bytes.  The bucket of the entry gets four planted entries of 2048 bytes,
        and a different bucket gets one planted entry larger than the full
        limit.  The write reads only its own bucket, so the large entry stays.
        """
        first = self.run("neg_convert", *CONVERT)
        entry = self.entry_path(first[3])
        self.expect(entry is not None and entry.is_file(), f"the convert fixture has an entry: {first[3]}")
        if entry is None or not entry.is_file():
            return
        entry.unlink()
        share = (1 << 20) // driver._BUCKETS
        unused = []
        for index in range(4):
            path = entry.parent / f"{index:064x}"
            path.write_bytes(bytes(share // 2))
            moment = time.time() - 86400 + index
            os.utime(path, (moment, moment))
            unused.append(path)
        other = self.store / "entries" / ("01" if entry.parent.name == "00" else "00") / f"{9:064x}"
        other.parent.mkdir(parents=True, exist_ok=True)
        other.write_bytes(bytes(2 << 20))
        moment = time.time() - 2 * 86400
        os.utime(other, (moment, moment))
        stored = self.run("neg_convert", *CONVERT, CRUCIBLE_NEG_CACHE_MAX_MB="1")
        remaining = [path for path in unused if path.exists()]
        bucket_total = sum(path.stat().st_size for path in entry.parent.iterdir())
        self.expect(self.has(stored[3], "stored") and entry.is_file(), f"the new entry stays: {stored[3]}")
        self.expect(len(remaining) <= 1 and remaining == unused[len(unused) - len(remaining):]
                    and (bucket_total <= share or not remaining),
                    f"the entries of the bucket unused for the longest time go: {[path.name[-1] for path in remaining]}")
        self.expect(other.is_file(), "a write does not remove an entry of a different bucket")


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
    StoreTest.check_cpu_budget,
    StoreTest.check_instruction_budget,
    StoreTest.check_inputs_record,
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
    parsed = driver.parse_dependency_file("out.o: a\\ b.h c.h \\\n d$$.h\\#e.h\n")
    if parsed != ["a b.h", "c.h", "d$.h#e.h"]:
        failures.append(f"the dependency file parse gives {parsed}")
    if driver.parse_dependency_file("out.o: a.h\nb.h: c.h\n") is not None:
        failures.append("a dependency file with two rules is refused")
    named = driver.parse_search_list(SEARCH_LIST)
    expected_named = ["/opt/gcc/x86_64/include", "/opt/inc/./", "/work/quote", "/work/include", "/opt/gcc/include/c++",
                      "/Library/Frameworks"]
    if named != expected_named:
        failures.append(f"the search list parse gives {named}")
    if driver.parse_search_list(SEARCH_LIST.replace("End of search list.\n", "")) is not None:
        failures.append("a search list with no end line is refused")
    loader = driver.parse_loader_list(LOADER_OUTPUT, LOADER_DEBUG, Path("/work"))
    expected_loader = (["/lib64/libz.so.1", "/lib64/ld-linux-x86-64.so.2"],
                       ["/opt/lib/libz.so.1", "/work/glibc-hwcaps/x86-64-v3/libz.so.1", "/lib64/libz.so.1"],
                       ["/etc/ld.so.cache"])
    if loader != expected_loader:
        failures.append(f"the loader list parse gives {loader}")
    if driver.parse_loader_list("\tlibq.so.1 => not found\n", "", Path("/work")) is not None:
        failures.append("a loader list with an object that the loader does not find is refused")
    interpreter = driver.elf_interpreter(os.path.realpath(sys.executable))
    if interpreter is None or not os.path.isfile(interpreter):
        failures.append(f"the interpreter of {sys.executable} is {interpreter}")
    if driver.elf_interpreter(str(DRIVER)) is not None:
        failures.append("a file that is not ELF has no interpreter")
    roots, probes = driver.search_roots(["/s/sub"], ["/n/x.cpp", "/s/sub/../b/B.h"], ["/abs/m.h", "../m.h", "m.h"])
    expected_probes = [path for reach in ("/s/sub/../b/B.h", "/n/../b/B.h", "/s/sub/../b/../b/B.h", "/s/sub/../m.h",
                                          "/n/../m.h", "/s/sub/../b/../m.h", "/abs/m.h")
                       for path in (reach, reach + ".gch")]
    if roots != ["/s/sub", "/n", "/s/sub/../b"] or probes != expected_probes:
        failures.append(f"the search roots are {roots} and the probes are {probes}")
    env = driver.compile_environment({"LC_ALL": "de_DE.UTF-8", "LANG": "en_US.UTF-8", "LANGUAGE": "de"})
    if env != {"LC_CTYPE": "de_DE.UTF-8", "LANG": "en_US.UTF-8", "LANGUAGE": "de", "LC_MESSAGES": "C"}:
        failures.append(f"the compile environment is {env}")


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
    failures: list[str] = []
    skips: list[str] = []
    if index == 0:
        check_parsers(failures)
    checks = CHECKS[index::count]
    for check in checks:
        with tempfile.TemporaryDirectory(prefix="neg-store-") as directory:
            test = StoreTest(Path(directory), cxx)
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
