#!/usr/bin/env python3
"""quarantine_stamps — write the inputs of the quarantine plugin that the build system tracks.

utils/tools/quarantine/Quarantine.cmake runs this script at configure time.
It reads the rule table through layer_rules.py and writes these files under
the directory OUT:

    facts.txt     The rows that decide a finding: each layer, allow, door,
                  admit, quarantine and language row, with the restrictions of
                  each admit row and with no reason, no comment and no `until`
                  family.
                  The lines are sorted.  The stamp of each compile command
                  holds the hash of this file, so a change of a fact compiles
                  each unit again, and a change of a reason compiles nothing.
    enforce.txt   The enforce rows, sorted.  ccache hashes this file
                  (extra_files_to_hash), so a cache hit never gives the object
                  of a compile under different enforce modes.
    modes/PATH    One mode stamp for each source file that a layer row or a
                  quarantine row holds: "error" or "report", the enforce mode
                  of the file.  The plugin names the mode stamp of each file
                  with a finding in the dependencies of the unit.  So a change
                  of the mode of a file compiles again only the units with a
                  finding in that file.  For a file with no mode stamp, such as
                  a file that the tree got after the last configure, the plugin
                  names enforce.txt.

The script writes a file only when its content changes, so a configure run
with no change of a fact or a mode makes no unit dirty.  It removes the mode
stamp of a file that no row holds any more, or that does not exist any more.

Usage:
    quarantine_stamps.py --root ROOT --rules TABLE --out DIR [--build BUILD]
    quarantine_stamps.py --self-test

Exit 0 on success, 1 when the table does not obey its format or a file cannot
be written.
"""

from __future__ import annotations

import argparse
import os
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import layer_rules  # noqa: E402

# The suffixes of a file that a C++ unit can read.  A finding in a file with a
# different suffix gets enforce.txt as its dependency.
SOURCE_SUFFIXES = frozenset((".c", ".cc", ".cpp", ".cxx", ".def", ".h", ".hh", ".hpp", ".hxx", ".inc", ".inl",
                             ".ipp", ".tcc"))
MODES_DIRECTORY = "modes"


def facts_text(table: layer_rules.RuleTable) -> str:
    """Return the canonical text of the rows of TABLE that decide a finding.

    The order of the rows, the order of the paths in a row, a reason, a comment
    and the family of an `until` restriction change nothing in the text.  A row
    with `until` gives each use of its entry a finding, so the text holds the
    word `until` of such a row.
    """
    lines = [f"layer {layer.name} {layer.rank} {' '.join(sorted(layer.paths))}" for layer in table.layers]
    lines += [f"allow {layer} {header}" for layer, header in table.allows]
    lines += [f"door {header} {owner}" for header, owner in table.doors]
    for admit in table.admits:
        words = [admit.entry]
        if admit.until:
            words.append("until")
        if admit.arity >= 0:
            words.append(f"arity {admit.arity}")
        if admit.concepts:
            words.append("concepts")
        if admit.parameters:
            words.append("parameters")
        if admit.plain_result:
            words.append("plain-result")
        if admit.unless:
            words.append(f"unless {admit.unless}")
        if admit.in_paths:
            words.append(f"in {' '.join(sorted(admit.in_paths))}")
        lines.append(f"admit {' '.join(words)}")
    lines += [f"quarantine {path}" for path in table.quarantines]
    lines += [f"language {layer} {' '.join(sorted(paths))}" for layer, paths in table.languages]
    return "".join(f"{line}\n" for line in sorted(lines))


def enforce_text(table: layer_rules.RuleTable) -> str:
    """Return the canonical text of the enforce rows of TABLE."""
    return "".join(f"enforce {path} {mode}\n" for path, mode in sorted(table.enforces))


def source_files(root: Path, table: layer_rules.RuleTable, build: Path | None) -> dict[str, str]:
    """Return the enforce mode of each source file that a layer row or a quarantine row holds.

    The walk reads each directory of a row one time.  It skips the build
    directory and each directory that a symbolic link names.  Complexity:
    O(files under the rows) times O(rows) for the class and the mode.

    Returns:
        A map from the path relative to ROOT to "error" or "report"
    """
    paths = sorted({path for layer in table.layers for path in layer.paths} | set(table.quarantines))
    candidates: set[str] = set()
    for path in paths:
        target = root / path
        if not path.endswith("/"):
            if target.is_file():
                candidates.add(path)
            continue
        for directory, subdirectories, files in os.walk(target):
            current = Path(directory)
            if build is not None:
                subdirectories[:] = [name for name in subdirectories if (current / name).resolve() != build]
            for name in files:
                if os.path.splitext(name)[1] in SOURCE_SUFFIXES:
                    candidates.add((current / name).relative_to(root).as_posix())
    modes: dict[str, str] = {}
    for relative in sorted(candidates):
        file_class, _ = table.class_of(relative)
        if file_class:
            modes[relative] = table.mode_of(relative)
    return modes


def write_if_changed(path: Path, text: str) -> bool:
    """Write TEXT to PATH when the file does not hold it yet, and return True when it wrote."""
    try:
        if path.read_text(encoding="utf-8") == text:
            return False
    except (FileNotFoundError, NotADirectoryError):
        pass
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f"{path.name}.tmp.{os.getpid()}")
    temporary.write_text(text, encoding="utf-8")
    os.replace(temporary, path)
    return True


def remove_stale(modes_root: Path, modes: dict[str, str]) -> int:
    """Remove each mode stamp under MODES_ROOT whose file is not in MODES, and each directory that it empties.

    Returns:
        The number of stamps removed
    """
    removed = 0
    if not modes_root.is_dir():
        return removed
    for directory, _, files in os.walk(modes_root, topdown=False):
        current = Path(directory)
        for name in files:
            stamp = current / name
            if stamp.relative_to(modes_root).as_posix() not in modes:
                stamp.unlink()
                removed += 1
        if current != modes_root and not any(current.iterdir()):
            current.rmdir()
    return removed


def write_stamps(root: Path, rules: Path, out: Path, build: Path | None) -> tuple[int, int]:
    """Write facts.txt, enforce.txt and the mode stamps of the rule table RULES under OUT.

    Returns:
        The number of files written and the number of mode stamps removed

    Raises:
        layer_rules.TableError: When the table does not obey its format
    """
    table = layer_rules.load(rules)
    written = int(write_if_changed(out / "facts.txt", facts_text(table)))
    written += int(write_if_changed(out / "enforce.txt", enforce_text(table)))
    modes = source_files(root, table, build)
    modes_root = out / MODES_DIRECTORY
    for relative, mode in modes.items():
        written += int(write_if_changed(modes_root / relative, f"{mode}\n"))
    return written, remove_stale(modes_root, modes)


def self_test() -> int:
    """Run the script on planted trees and tables, and judge each output."""
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    base_table = ("# a comment\nlayer low 0 include/low/\nallow low <cstdint>\nquarantine app/\n"
                  "admit std::move arity 1 | a cast\nadmit <limits> until Scalar | limits\n"
                  "enforce app/ report\n")
    with tempfile.TemporaryDirectory(prefix="quarantine-stamps-") as scratch:
        root = Path(scratch) / "root"
        out = root / "build" / "quarantine"
        for relative in ("include/low/Low.h", "app/main.cpp", "app/util.h", "app/notes.txt", "build/gen.h",
                         "other/free.cpp"):
            (root / relative).parent.mkdir(parents=True, exist_ok=True)
            (root / relative).write_text("// planted\n", encoding="utf-8")
        rules = Path(scratch) / "rules.txt"

        def run(table_text: str) -> tuple[int, int]:
            rules.write_text(table_text, encoding="utf-8")
            return write_stamps(root, rules, out, (root / "build").resolve())

        def stamps() -> dict[str, tuple[str, int]]:
            modes_root = out / MODES_DIRECTORY
            return {path.relative_to(modes_root).as_posix(): (path.read_text(encoding="utf-8"), path.stat().st_mtime_ns)
                    for path in modes_root.rglob("*") if path.is_file()}

        written, _ = run(base_table)
        first = stamps()
        facts = (out / "facts.txt").read_text(encoding="utf-8")
        expect("the first run writes facts.txt, enforce.txt and a stamp for each held source file",
               written == 5 and set(first) == {"include/low/Low.h", "app/main.cpp", "app/util.h"})
        expect("a file with no source suffix, a file of the build directory and a file of no row get no stamp",
               not {"app/notes.txt", "build/gen.h", "other/free.cpp"} & set(first))
        expect("facts.txt holds no reason, no comment and no `until` family",
               "a cast" not in facts and "comment" not in facts and "Scalar" not in facts
               and "admit std::move arity 1\n" in facts)

        reordered = ("layer low 0 include/low/\nquarantine app/\nadmit <limits> until Report | other words\n"
                     "admit std::move arity 1 | a different reason\nallow low <cstdint>\nenforce app/ report\n"
                     "# a new comment\n")
        written, removed = run(reordered)
        expect("a new order, a new reason, a new comment and a new `until` family write nothing",
               written == 0 and removed == 0 and stamps() == first)

        written, _ = run(reordered + "enforce app/sub/ report\n")
        expect("a report row under a report directory changes no mode stamp, and only enforce.txt",
               written == 1 and stamps() == first)

        written, _ = run(reordered + "enforce app/util.h error\n")
        changed = {path for path, value in stamps().items() if first.get(path) != value}
        expect("an error row for one file changes the mode stamp of that file only, and enforce.txt",
               written == 2 and changed == {"app/util.h"} and stamps()["app/util.h"][0] == "error\n")

        written, _ = run(reordered.replace("arity 1", "arity 2"))
        expect("a changed restriction changes facts.txt",
               (out / "facts.txt").read_text(encoding="utf-8") != facts)

        run(reordered + "language low include/low/Low.h\n")
        expect("a language row is a fact",
               "language low include/low/Low.h\n" in (out / "facts.txt").read_text(encoding="utf-8"))

        (root / "app" / "util.h").unlink()
        _, removed = run(base_table)
        expect("the stamp of a file that does not exist any more goes", removed == 1 and "app/util.h" not in stamps())

        try:
            run("bogus row\n")
            refused = False
        except layer_rules.TableError:
            refused = True
        expect("a table that does not obey its format is refused", refused)
    if failures:
        print(f"quarantine_stamps: {len(failures)} expectation(s) failed", file=sys.stderr)
        return 1
    print("quarantine_stamps: every expectation holds.")
    return 0


def main(argv: list[str]) -> int:
    """Parse the arguments and write the stamps, or run the self-test."""
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--root", type=Path, help="the source root")
    parser.add_argument("--rules", type=Path, help="the rule table")
    parser.add_argument("--out", type=Path, help="the directory of the stamps")
    parser.add_argument("--build", type=Path, help="the build directory, whose files the walk skips")
    parser.add_argument("--self-test", action="store_true", help="run the self-test")
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    if args.root is None or args.rules is None or args.out is None:
        parser.error("give --root, --rules and --out, or --self-test")
    build = args.build.resolve() if args.build is not None else None
    try:
        write_stamps(args.root.resolve(), args.rules, args.out, build)
    except layer_rules.TableError as failure:
        print(f"quarantine_stamps: {failure}", file=sys.stderr)
        return 1
    except OSError as failure:
        print(f"quarantine_stamps: cannot write the stamps under {args.out}: {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
