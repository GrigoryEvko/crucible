"""The test of the read set of a test (assay.loop.readset).

`guard selftest tests.loop.test_readset` runs the test.  The cases write and read
planted read sets.  They take the signatures of planted files and directories,
and they give planted events to the classes of a read.  They also call plan()
for each condition of a retrace.  The scratch files are in the work root of
assay.core.cache, because /tmp is a runtime root.  Each case writes one line.
Exit 0 when each case holds, else 1.
"""

import os
import tempfile
from pathlib import Path

from assay.core import cache, testing
from assay.loop import readset


def record_cases(expect: testing.Expect) -> None:
    """Write and read read sets, with escapes, and refuse each record of a different shape (THE RECORD)."""
    entries = (
        readset.Entry(readset.READ, readset.KEY, "f:1:2:3", "f:1:2:3", "/a/with\ttab"),
        readset.Entry(readset.LIST, readset.FACT, readset.NOT_MEASURED, readset.NOT_MEASURED, "/proc/self/fd"),
    )
    written = readset.ReadSet("00ff", -6, False, ["/b/test", "back\\slash", "line\nbreak"], entries)
    with tempfile.TemporaryDirectory(prefix="test-readset-") as scratch:
        path = os.path.join(scratch, "test-records", "x" + readset.SUFFIX)
        readset.write(path, written)
        expect("a written read set reads back equal, with each escape", readset.read(path) == written)
        expect("read_set_file() gives NAME.test.reads in test-records", readset.read_set_file(scratch, "x") == path)
        text = Path(path).read_text()
        damaged = (
            ("a different first line", text.replace(readset.FORMAT, "crucible-read-set 2")),
            ("a status that is not a number", text.replace("status -6", "status six")),
            ("a settled word that is not yes or no", text.replace("settled no", "settled maybe")),
            ("an entry of an unknown kind", text.replace("\nread\t", "\nwrite\t")),
            ("an escape that is not known", text.replace("\\t", "\\q")),
            ("no command line", "\n".join(text.splitlines()[:4]) + "\n"),
        )
        for label, damaged_text in damaged:
            Path(path).write_text(damaged_text)
            expect(f"read() refuses a read set with {label}", readset.read(path) is None)
        readset.remove(path)
        readset.remove(path)
        expect("remove() removes a read set, and a second remove changes nothing", readset.read(path) is None)


def signature_cases(expect: testing.Expect) -> None:
    """Take the signatures of a file, a directory, a pipe and a path with no file (THE SIGNATURE)."""
    with tempfile.TemporaryDirectory(prefix="test-readset-") as scratch:
        root = Path(scratch)
        item = root / "item.txt"
        item.write_text("one\n")
        first = readset.signature(str(item))
        expect(
            "a file gives its size, its time and its inode",
            first.startswith(readset.FILE_SIGNATURE) and first.split(":")[1] == "4",
        )
        os.utime(item, ns=(1, 1))
        expect("a new time of a file gives a different signature", readset.signature(str(item)) != first)
        listing = readset.signature(str(root))
        item.write_text("two\n")
        expect(
            "a directory gives the names in it, and a change of a file in it keeps its signature",
            listing.startswith(readset.DIRECTORY_SIGNATURE) and readset.signature(str(root)) == listing,
        )
        (root / "new.txt").write_text("")
        expect("a new name in a directory gives a different signature", readset.signature(str(root)) != listing)
        pipe = root / "pipe"
        os.mkfifo(pipe)
        expect("a pipe gives its type", readset.signature(str(pipe)).startswith(readset.OTHER_SIGNATURE))
        expect(
            "a path with no file gives ABSENT",
            readset.signature(str(root / "none")) == readset.ABSENT
            and readset.signature(str(item / "below")) == readset.ABSENT,
        )


def classify_cases(expect: testing.Expect) -> None:
    """Give planted events to classify() (THE CLASSES OF A READ)."""
    work = cache.work_root()
    work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="test-readset-", dir=work) as scratch:
        root = Path(os.path.realpath(scratch))
        runtime = root / "runtime"
        runtime.mkdir()
        kept, made, removed, missing = (root / name for name in ("kept.txt", "made.txt", "removed.txt", "missing"))
        kept.write_text("kept\n")
        made.write_text("made\n")
        roots = (str(runtime),)
        events = {
            (readset.READ, str(kept)): readset.signature(str(kept)),
            (readset.READ, str(made)): readset.ABSENT,
            (readset.READ, str(removed)): "f:1:2:3",
            (readset.READ, str(missing)): readset.ABSENT,
            (readset.READ, str(root)): readset.signature(str(root)),
            (readset.LIST, str(root)): readset.signature(str(root)),
            (readset.READ, str(runtime / "x")): readset.NOT_MEASURED,
            (readset.EXEC, os.path.join(str(root), ".", "kept.txt")): readset.signature(str(kept)),
        }
        entries, is_settled = readset.classify(events, roots)
        roles = {(entry.kind, entry.path): entry.role for entry in entries}
        expect(
            "a file that the test read and did not change is a key part, and the read set is settled",
            roles.get((readset.READ, str(kept))) == readset.KEY and is_settled,
        )
        expect(
            "a file that the test made, and a file that it removed, are facts",
            roles.get((readset.READ, str(made))) == readset.FACT
            and roles.get((readset.READ, str(removed))) == readset.FACT,
        )
        expect(
            "a path with no file at the first stop and at the end is a key part",
            roles.get((readset.READ, str(missing))) == readset.KEY,
        )
        expect(
            "an open of a directory gives no entry, and a listing gives a key part",
            (readset.READ, str(root)) not in roles and roles.get((readset.LIST, str(root))) == readset.KEY,
        )
        expect(
            "a path in a runtime root is a fact, which the launcher does not stat",
            roles.get((readset.READ, str(runtime / "x"))) == readset.FACT,
        )
        expect("an entry holds the real path", roles.get((readset.EXEC, str(kept))) == readset.KEY)
        events[(readset.READ, str(kept))] = "f:0:0:0"
        _, is_settled = readset.classify(events, roots)
        expect("a key part that changed while the test ran makes the read set not settled", not is_settled)


def plan_cases(expect: testing.Expect) -> None:
    """Call plan() for each condition of a retrace (THE RETRACE)."""
    work = cache.work_root()
    work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="test-readset-", dir=work) as scratch:
        build = os.path.realpath(scratch)
        source = Path(build) / "input.txt"
        source.write_text("input\n")
        command = ("/b/test", "--flag")
        entry = readset.Entry(readset.READ, readset.KEY, "", readset.signature(str(source)), str(source))
        path = readset.read_set_file(build, "t")

        def planned(
            version: str = "v1", status: int = 0, is_settled: bool = True, words: tuple[str, ...] = command
        ) -> bool:
            """Write a read set with these fields, and return True when plan() gives a traced run."""
            readset.write(path, readset.ReadSet(version, status, is_settled, words, [entry]))
            return readset.plan(build, "t", command, "v1") is not None

        expect("a current read set of the same version and command needs no trace", not planned())
        expect("a different version needs a trace", planned(version="v0"))
        expect("a run with a status that is not 0 needs a trace", planned(status=1))
        expect("a read set that is not settled needs a trace", planned(is_settled=False))
        expect("a different command needs a trace", planned(words=("/b/test",)))
        readset.write(path, readset.ReadSet("v1", 0, True, command, [entry]))
        source.write_text("changed\n")
        is_planned = readset.plan(build, "t", command, "v1") is not None
        expect("a key part with a different signature needs a trace", is_planned)
        readset.remove(path)
        expect("a test with no read set needs a trace", readset.plan(build, "t", command, "v1") is not None)


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    groups = (record_cases, signature_cases, classify_cases, plan_cases)
    return testing.run(__name__.removeprefix("assay."), argv, groups)
