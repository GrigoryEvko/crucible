"""session-oracle — the tests of test/session_oracle/ are the tests that its golden file emits.

THE RULE
    The rule calls problems() of assay.oracle.runner (THE CHECK of that
    module).  The runner emits the tests of test/session_oracle/golden.csv
    again, and it compares them with the files of the tree.  It uses no
    oracle, no compiler and no build directory.  The rule gives:
      `drift`   an emitted file that is missing or that has a different
                text.  Or a file generated_* of the tree that the golden file
                does not emit.  The entity is the name of the file
      `golden`  the golden file cannot be read.  The entity is golden.csv
      `ledger`  a shrink-only class of semantics.SHRINK_ONLY has more rows
                than its bound.  The entity is SHRINK_ONLY
      `bound`   a shrink-only class has fewer rows than its bound, and the
                bound can go down.  A warning
    The rule has no ledger.  `loop derive session-oracle emit` writes the
    tests again, and `loop derive session-oracle regenerate` writes the
    golden file again from the oracles.  The group `oracle` of `guard
    selftest` compiles the emitted tests, and plants a wrong row in each.

THE PLANTS
    A plant holds a planted golden file test/session_oracle/golden.csv and
    the file oracle.txt, in the rows grammar of assay.core.rows:

        format 1
        emit                   write each file that the planted golden file
                               emits into test/session_oracle/
        append NAME | LINE     add a line to a file of test/session_oracle/
        remove NAME            remove a file of test/session_oracle/
        write NAME | LINE      write a file of test/session_oracle/ that holds
                               one line

    plant_context() does the rows in their sequence, after the copy of the
    plant.

Complexity: one read of the golden file, one emission, and one read of each emitted test.
"""

from pathlib import Path

from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.guard.engines.buildsys.runner import BuildContext
from assay.guard.engines.meta import runner as meta_runner
from assay.oracle import runner

RULE = "session-oracle"
ORACLE_FILE = "oracle.txt"
FORMAT = "1"
TEST_DIR = Path("test") / "session_oracle"


def findings(context: BuildContext) -> list[check_report.Finding]:
    """Give one finding for each problem of the emitted tests of the tree (THE RULE)."""
    return [
        check_report.Finding(
            "error" if problem.is_error else "warning",
            problem.path,
            0,
            RULE,
            problem.text,
            problem.kind,
            problem.entity,
        )
        for problem in runner.problems(context.root)
    ]


# ── The plants ─────────────────────────────────────────────────────────────


def test_file(directory: Path, row: rows_grammar.Row) -> Path:
    """Return the file of test/session_oracle/ that a row names.

    Raises:
        rows_grammar.RowsError: If the row names no file, or a name with a directory
    """
    if len(row.words) != 2 or "/" in row.words[1] or row.words[1] in ("", ".", ".."):
        raise row.error(f"a {row.words[0]} row names one file of {TEST_DIR.as_posix()}/ with no directory")
    return directory / row.words[1]


def read_oracle(root: Path) -> None:
    """Do the rows of oracle.txt in their sequence (THE PLANTS).

    Complexity: one emission for each emit row, and linear in the other rows.

    Raises:
        rows_grammar.RowsError: If a row does not obey its shape, or the planted golden file cannot be read
    """
    from assay.oracle import emit

    rows = rows_grammar.read(root / ORACLE_FILE, ORACLE_FILE)
    if not rows or rows[0].words != ("format", FORMAT):
        raise rows_grammar.RowsError(ORACLE_FILE, rows[0].line if rows else 0, f"the first row is 'format {FORMAT}'")
    directory = root / TEST_DIR
    directory.mkdir(parents=True, exist_ok=True)
    for row in rows[1:]:
        kind = row.words[0]
        if kind == "emit" and len(row.words) == 1 and not row.cells:
            try:
                _, golden = emit.read_golden(directory / runner.GOLDEN.name)
            except (emit.GoldenError, ValueError, OSError) as problem:
                raise row.error(f"the planted golden file cannot be read: {problem}") from None
            for name, text in emit.emit_all(golden).items():
                (directory / name).write_text(text, encoding="utf-8")
        elif kind == "append" and len(row.cells) == 1:
            path = test_file(directory, row)
            path.write_text(path.read_text(encoding="utf-8") + row.cells[0] + "\n", encoding="utf-8")
        elif kind == "remove" and not row.cells:
            test_file(directory, row).unlink()
        elif kind == "write" and len(row.cells) == 1:
            test_file(directory, row).write_text(row.cells[0] + "\n", encoding="utf-8")
        else:
            raise row.error(f"the row kind {kind!r} or its words do not have a shape of {ORACLE_FILE}")


def plant_context(plant: Path, scratch: Path) -> BuildContext:
    """Copy a plant, do the rows of its oracle.txt, and return its context with no build directory (THE PLANTS).

    Raises:
        NotApplicable: If oracle.txt does not obey its rows
    """
    meta_runner.copy_plant(plant, scratch)
    if (scratch / ORACLE_FILE).is_file():
        try:
            read_oracle(scratch)
        except (rows_grammar.RowsError, OSError) as problem:
            raise meta_runner.NotApplicable(f"the planted oracle does not read: {problem}") from None
    return BuildContext(scratch, None)
