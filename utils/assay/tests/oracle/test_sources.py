"""The test of assay.oracle.sources: the rows of each source, the refusal of a row that no oracle reads, and the unpack.

`guard selftest tests.oracle.test_sources` runs the test alone, and `guard selftest oracle` runs it in its group.
"""

import io
import os
import tarfile
import tempfile
from pathlib import Path

from assay.core import pins as pin_table
from assay.core import testing
from assay.oracle import rocq, sources, sr
from assay.oracle.rocq import OracleError

COMMIT = "0123456789abcdef0123456789abcdef01234567"
DIGEST = "0" * 64


def planted(*rows: str) -> tuple[pin_table.Pin, ...]:
    """Return a pin table of the rows."""
    return pin_table.parse("# a planted pin table\nformat 1\n" + "".join(f"{row}\n" for row in rows), "planted.txt")


def refuses(row: str) -> bool:
    """Return True when of_pin() refuses the one row of a planted table."""
    try:
        sources.of_pin(planted(row)[0])
    except OracleError:
        return True
    return False


def row_cases(expect: testing.Expect) -> None:
    """Check the source of a git row and of an archive row, and the rows that no oracle reads."""
    git_row = f"pin repo via=git version=2025-01-01 source=https://github.com/owner/name.git commit={COMMIT} | a source"
    archive_row = (
        f"pin artifact via=archive version=7 source=https://example.invalid/a.zip sha256={DIGEST} | an artifact"
    )
    repository, artifact = (sources.of_pin(pin) for pin in planted(git_row, archive_row))
    expect(
        "a git row on GitHub gives OWNER/NAME, the archive of its commit and the directory NAME-COMMIT",
        repository.repository == "owner/name"
        and repository.commit == COMMIT
        and repository.archive_url == f"https://github.com/owner/name/archive/{COMMIT}.tar.gz"
        and repository.directory == f"name-{COMMIT}",
    )
    expect(
        "an archive row gives its address as its archive, its SHA-256, and no repository",
        artifact.archive_url == "https://example.invalid/a.zip"
        and artifact.digest == DIGEST
        and artifact.repository == ""
        and artifact.commit == "",
    )
    expect(
        "a git row on a host other than GitHub is refused",
        refuses(git_row.replace("github.com", "gitlab.example.invalid")),
    )
    expect("a host row is refused", refuses("pin language via=host version=3.14 | a host"))
    try:
        sources.source("absent", planted(git_row))
        has_refused = False
    except OracleError:
        has_refused = True
    expect("a name with no row is refused", has_refused)


def tree_cases(expect: testing.Expect) -> None:
    """Check that the pin table of the tree gives each source, and that each development names its pin."""
    try:
        found = {name: sources.source(name) for name in sources.SOURCES}
    except OracleError as problem:
        expect(f"the pin table of the tree gives each source: {problem}", False)
        return
    expect("the pin table of the tree gives each source of SOURCES", set(found) == set(sources.SOURCES))
    expect(
        "each git source has a commit of 40 hexadecimal digits, and Sprout has a SHA-256",
        all(len(item.commit) == 40 for name, item in found.items() if name != sources.SPROUT)
        and len(found[sources.SPROUT].digest) == 64,
    )
    expect(
        "each development of the oracle names a pin of SOURCES",
        rocq.PROJECTION.pin == sources.PROJECTION and sr.SUBJECT_REDUCTION.pin == sources.SUBJECT_REDUCTION,
    )


def archive_of(directory: str, files: dict[str, bytes]) -> bytes:
    """Return a gzip tar archive that holds each file in the directory and its subdirectories."""
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w:gz") as archive:
        for name, data in files.items():
            info = tarfile.TarInfo(f"{directory}/{name}")
            info.size = len(data)
            archive.addfile(info, io.BytesIO(data))
    return buffer.getvalue()


def fetch_cases(expect: testing.Expect) -> None:
    """Check that fetch() unpacks the directory of the commit one time, and refuses an archive with no marker."""
    saved = os.environ.get("SESSION_ORACLE_CACHE")
    with tempfile.TemporaryDirectory(prefix="oracle-sources-") as scratch:
        root = Path(scratch)
        os.environ["SESSION_ORACLE_CACHE"] = str(root / "cache")
        try:
            address = f"file://{root}/owner/name"
            item = sources.Source("repo", address, "2025-01-01", COMMIT, "")
            served = root / "owner" / "name" / "archive" / f"{COMMIT}.tar.gz"
            served.parent.mkdir(parents=True)
            served.write_bytes(archive_of(item.directory, {"_CoqProject": b"-R . Planted\n"}))
            unpacked = sources.fetch(item, "_CoqProject")
            expect(
                "fetch() unpacks the directory NAME-COMMIT into the cache of the oracle",
                unpacked == root / "cache" / item.directory and (unpacked / "_CoqProject").is_file(),
            )
            served.unlink()
            expect(
                "a second fetch() finds the marker, and does not download again",
                sources.fetch(item, "_CoqProject") == unpacked,
            )
            other = sources.Source("other", address, "2025-01-01", COMMIT.replace("0", "1"), "")
            other_served = root / "owner" / "name" / "archive" / f"{other.commit}.tar.gz"
            other_served.write_bytes(archive_of(other.directory, {"README": b"no marker\n"}))
            try:
                sources.fetch(other, "_CoqProject")
                has_refused = False
            except OracleError:
                has_refused = True
            expect("an archive with no marker is refused", has_refused)
        finally:
            if saved is None:
                os.environ.pop("SESSION_ORACLE_CACHE", None)
            else:
                os.environ["SESSION_ORACLE_CACHE"] = saved


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (row_cases, tree_cases, fetch_cases))
