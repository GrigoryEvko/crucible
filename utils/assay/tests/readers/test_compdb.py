"""The test of the reader of the compile database (assay.readers.compdb).

`guard selftest tests.readers.test_compdb` runs the test.  Each case prints
one line.  Exit 0 when each case holds, else 1.
"""

import shlex
import sys
import tempfile
from collections.abc import Callable
from pathlib import Path

from assay.readers import compdb


def refused(build_dir: Path, error: type[compdb.CompdbError]) -> str:
    """Return the message of the error that a read raises, or an empty text when the read raises no such error."""
    try:
        compdb.entries(build_dir)
    except error as problem:
        return str(problem)
    return ""


def read_cases(expect: Callable[[str, bool], None], build: Path) -> None:
    """Check the entries of a planted database: the paths, the command, the arguments and the output."""
    compdb.plant(build, [
        {"directory": "/b", "file": "/r/src/a.cpp", "output": "src/CMakeFiles/a.dir/a.cpp.o",
         "command": "g++ -DNAME=\"a b\" -o src/CMakeFiles/a.dir/a.cpp.o -c /r/src/a.cpp"},
        {"directory": "/b/sub", "file": "b.cpp", "arguments": ["g++", "-c", "b.cpp"]},
        {"directory": "/b", "file": "/r/c.cpp", "output": "x/../c.o", "command": "g++ -c /r/c.cpp"}])
    first, second, third = compdb.entries(build)
    expect("an entry gives its file, its directory and its absolute output",
           first.file == "/r/src/a.cpp" and first.directory == "/b"
           and first.output == "/b/src/CMakeFiles/a.dir/a.cpp.o")
    expect("argv() splits the command line with its shell quotes",
           first.argv() == ("g++", "-DNAME=a b", "-o", "src/CMakeFiles/a.dir/a.cpp.o", "-c", "/r/src/a.cpp"))
    expect("a list of arguments gives argv() and a command line, and a relative file joins its directory",
           second.argv() == ("g++", "-c", "b.cpp") and second.command == "g++ -c b.cpp"
           and second.file == "/b/sub/b.cpp" and second.output is None)
    expect("the output is normalized", third.output == "/b/c.o")
    expect("two reads give equal entries", compdb.entries(build) == (first, second, third))
    expect("database_file() names the file of the reader", compdb.database_file(build).is_file())
    samples = ["g++ -I/a  -DX=1\t-c a.cpp -o a.o", 'g++ -DP=\\"/x y\\" -c a.cpp', "g++ '-Dq=a b' -c a.cpp",
               "g++ -DE=a\\ b -c a.cpp"]
    expect("argv() splits each command line as shlex.split does, also on the path with no quote",
           all(compdb.CompileEntry("/a.cpp", "/", None, sample).argv() == tuple(shlex.split(sample))
               for sample in samples))


def error_cases(expect: Callable[[str, bool], None], build: Path) -> None:
    """Check each refusal: a missing file, a file that is not JSON, the wrong top level and a malformed compile."""
    compdb.database_file(build).unlink(missing_ok=True)
    message = refused(build, compdb.MissingDatabase)
    expect("a missing database is MissingDatabase, which names the file and the command that writes it",
           "does not exist" in message and str(build) in message and "cmake --preset" in message)
    for text, label in (("[{]", "a file that is not JSON"), ('{"a": 1}', "a top level that is no array"),
                        ('[{"directory": "/b"}]', "a compile with no file"),
                        ('[{"directory": "/b", "file": "a.cpp"}]', "a compile with no command"),
                        ('[{"directory": "/b", "file": "a.cpp", "command": "g++", "output": 3}]',
                         "an output that is not a text"),
                        ("[7]", "an item that is not an object")):
        compdb.plant(build, text)
        message = refused(build, compdb.CompdbError)
        expect(f"the reader refuses {label}, and names the file", bool(message) and str(build) in message
               and not isinstance(refused_error(build), compdb.MissingDatabase))
    compdb.plant(build, '[{"directory": "/b", "file": "a.cpp", "command": "g++ \'open"}]')
    entry = compdb.entries(build)[0]
    try:
        entry.argv()
        expect("argv() refuses a command line with an open quote", False)
    except ValueError:
        expect("argv() refuses a command line with an open quote", True)


def refused_error(build_dir: Path) -> compdb.CompdbError | None:
    """Return the error that a read raises, or None."""
    try:
        compdb.entries(build_dir)
    except compdb.CompdbError as problem:
        return problem
    return None


def main(argv: list[str]) -> int:
    """Run each case, and print one line for each.

    Returns:
        0 when each case holds, 1 when a case fails, 2 for a usage error
    """
    if argv:
        print("usage: guard selftest tests.readers.test_compdb", file=sys.stderr)
        return 2
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    with tempfile.TemporaryDirectory(prefix="assay-compdb-") as scratch:
        read_cases(expect, Path(scratch))
        error_cases(expect, Path(scratch))
    if failures:
        print(f"tests.readers.test_compdb: FAILED, {len(failures)} case(s) did not hold")
        return 1
    print("tests.readers.test_compdb: every case holds.")
    return 0
