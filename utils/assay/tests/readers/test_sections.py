"""The test of the reader of the section of the quarantine plugin (assay.readers.sections).

`guard selftest tests.readers.test_sections` runs the test.  Each case writes
one line.  Exit 0 when each case holds, else 1.

The cases plant objects with the writer of assay.readers.elf.  An object
has the section, no section, or two sections of the name.  An object also
has the extended numbering, or a name that only ends with the name of the
section.
"""

import contextlib
import io
import tempfile
from collections.abc import Callable
from pathlib import Path

from assay.core import testing
from assay.core.exit import ExitCode
from assay.readers import elf, sections

TEXT = (
    b"# crucible-guard 2 stamp=00ab\n"
    b"quarantine std-object test/a.cpp:3:7 std::vector (std::vector<int>)\n"
    b"contract-specifier contract-specifier include/crucible/B.h:9:1 pre\n"
)
FINDINGS = (
    sections.Finding("test/a.cpp", 3, 7, "quarantine", "std-object", "std::vector (std::vector<int>)"),
    sections.Finding("include/crucible/B.h", 9, 1, "contract-specifier", "contract-specifier", "pre"),
)


def refuses(action: Callable[[], object]) -> bool:
    """Return True when an action raises SectionError."""
    try:
        action()
    except sections.SectionError:
        return True
    return False


def planted_object(parts: list[tuple[str, bytes]], extended: bool = False) -> bytes:
    """Return an object with the named sections and their bytes."""
    return elf.make_file([elf.Planted(name, data) for name, data in parts], extended)


def parse_cases(expect: testing.Expect) -> None:
    """Check parse_section() and parse_line() on each shape of a line and of a head line."""
    section = sections.parse_section(TEXT, "a.o")
    expect("parse_section reads the stamp", section.stamp == "00ab")
    expect("parse_section reads each finding", section.findings == FINDINGS)
    expect(
        "a finding gives its line back",
        section.findings[0].text() == "quarantine std-object test/a.cpp:3:7 std::vector (std::vector<int>)",
    )
    expect(
        "a section with the head line only has no finding",
        sections.parse_section(b"# crucible-guard 2 stamp=\n", "a.o") == sections.Section("", ()),
    )
    expect(
        "a line with no entity has an empty entity",
        sections.parse_section(b"# crucible-guard 2 stamp=x\nquarantine region a.cpp:3:1\n", "a.o").findings[0].entity
        == "",
    )
    expect(
        "parse_section refuses a section with no head line",
        refuses(lambda: sections.parse_section(TEXT.split(b"\n", 1)[1], "a.o")),
    )
    expect(
        "parse_section refuses another format",
        refuses(lambda: sections.parse_section(b"# crucible-guard 1 stamp=x\n", "a.o")),
    )
    expect(
        "parse_section refuses the head line of format 1",
        refuses(lambda: sections.parse_section(b"# crucible-quarantine 1 stamp=x\n", "a.o")),
    )
    expect(
        "parse_section refuses a line that is not a finding",
        refuses(lambda: sections.parse_section(b"# crucible-guard 2 stamp=x\nnoise\n", "a.o")),
    )
    old_line = b"# crucible-guard 2 stamp=x\nquarantine: std_object a.cpp:3:7 x\n"
    expect("parse_section refuses a line of format 1", refuses(lambda: sections.parse_section(old_line, "a.o")))
    no_column = b"# crucible-guard 2 stamp=x\nquarantine std-object a.cpp:3 x\n"
    expect("parse_section refuses a place with no column", refuses(lambda: sections.parse_section(no_column, "a.o")))
    arabic_digit = chr(0x663)
    expect(
        "parse_section refuses a line number that is not ASCII",
        refuses(
            lambda: sections.parse_section(
                f"# crucible-guard 2 stamp=x\nquarantine std-object a.cpp:{arabic_digit}:7 x\n".encode(), "a.o"
            )
        ),
    )
    expect(
        "parse_section refuses bytes that are not UTF-8",
        refuses(lambda: sections.parse_section(b"# crucible-guard 2 stamp=x\n\xff\n", "a.o")),
    )


def object_cases(expect: testing.Expect) -> None:
    """Check read() and print_objects() on planted objects."""
    with tempfile.TemporaryDirectory(prefix="sections-test-") as scratch_text:
        scratch = Path(scratch_text)
        plain = scratch / "plain.o"
        plain.write_bytes(planted_object([(".text", b"\x90"), (sections.SECTION_NAME, TEXT)]))
        expect("read finds the section", sections.read(plain) == sections.Section("00ab", FINDINGS))
        extended = scratch / "extended.o"
        extended.write_bytes(planted_object([(".text", b"\x90"), (sections.SECTION_NAME, TEXT)], extended=True))
        expect("read follows the extended numbering", sections.read(extended) == sections.Section("00ab", FINDINGS))
        absent = scratch / "absent.o"
        absent.write_bytes(planted_object([(".text", b"\x90"), (".data", b"x")]))
        expect("read gives None for an object with no section", sections.read(absent) is None)
        suffix = scratch / "suffix.o"
        suffix.write_bytes(planted_object([(".text", b"\x90"), (".rela.crucible.quarantine", b"r")]))
        expect("read does not take a longer name that ends with the name", sections.read(suffix) is None)
        joined = scratch / "joined.o"
        head, first, second = TEXT.split(b"\n")[:3]
        halves = [(sections.SECTION_NAME, head + b"\n" + first + b"\n"), (sections.SECTION_NAME, second + b"\n")]
        joined.write_bytes(planted_object(halves))
        expect("read joins two sections with the name", sections.read(joined) == sections.Section("00ab", FINDINGS))
        foreign = scratch / "foreign.o"
        foreign.write_bytes(elf.ARCHIVE_MAGIC + b"\0" * 64)
        expect("read refuses a file that is not ELF", refuses(lambda: sections.read(foreign)))
        short = scratch / "short.o"
        short.write_bytes(planted_object([(sections.SECTION_NAME, TEXT)])[:-10])
        expect("read refuses a cut table", refuses(lambda: sections.read(short)))
        empty = scratch / "empty.o"
        empty.write_bytes(b"")
        expect("read refuses an empty file", refuses(lambda: sections.read(empty)))
        with contextlib.redirect_stdout(io.StringIO()) as printed, contextlib.redirect_stderr(io.StringIO()):
            status = sections.print_objects([str(plain)])
            absent_status = sections.print_objects([str(absent)])
        expect(
            "the command prints the finding lines of an object",
            status == ExitCode.OK and printed.getvalue() == "".join(f"{item.text()}\n" for item in FINDINGS),
        )
        expect("the command fails for an object with no section", absent_status == ExitCode.FINDINGS)


def report_cases(expect: testing.Expect) -> None:
    """Check report_directory() and read_report() on planted report directories of a parse build."""
    with tempfile.TemporaryDirectory(prefix="sections-report-") as scratch_text:
        build = Path(scratch_text)
        directory = sections.report_directory(build, build / "CMakeFiles" / "t.dir" / "a.cpp.o")
        expect(
            "the report directory is the place of the object below the syntax directory",
            directory == build / sections.SYNTAX_DIRECTORY / "CMakeFiles" / "t.dir" / "a.cpp.o",
        )
        expect("a missing directory has no report", sections.read_report(directory) is None)
        directory.mkdir(parents=True)
        report_text = TEXT.decode().replace("# crucible-guard 2 stamp=00ab", "# unit a.cpp")
        (directory / f"a.cpp.0123456789abcdef{sections.REPORT_SUFFIX}").write_text(report_text, encoding="utf-8")
        expect("a report with no stamp file is no report", sections.read_report(directory) is None)
        (directory / sections.STAMP_FILE).write_text("00ab\n", encoding="utf-8")
        expect(
            "read_report reads the stamp file and each finding line, and skips the comment",
            sections.read_report(directory) == sections.Section("00ab", FINDINGS),
        )
        (directory / f"b.cpp.0123456789abcdef{sections.REPORT_SUFFIX}").write_text(report_text, encoding="utf-8")
        expect("read_report refuses two report files", refuses(lambda: sections.read_report(directory)))


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (parse_cases, object_cases, report_cases))
