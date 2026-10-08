"""sections — read the findings of the quarantine plugin from the section of an object.

THE SECTION
    The quarantine plugin of utils/tools/quarantine/ writes the findings of a
    unit into the section .crucible.quarantine of its object (the head comment
    of plugin.cpp, THE SECTION).  The section is text in UTF-8.  Its first line
    is "# crucible-guard 2 stamp=TEXT", and each other line is one finding:

        RULE KIND FILE:LINE:COLUMN ENTITY

    RULE is a rule of the engine plugin of utils/registry.txt, and KIND is a
    kind of that rule.  FILE is relative to the source root.  A report file of
    the plugin argument out= holds the same finding lines, after a comment
    line.

THE OBJECT
    read_section() reads the section table of an ELF64 little-endian object
    with utils/assay/readers/elf.py, and then the bytes of each section with
    the name.

`guard sections OBJECT...` prints the finding lines of objects, and `guard
selftest readers.sections` does a test of the module.
"""

from __future__ import annotations

import contextlib
import dataclasses
import io
import re
import sys
import tempfile
from pathlib import Path

from assay.readers import elf as elf_file

SECTION_NAME = b".crucible.quarantine"
HEAD_PREFIX = "# crucible-guard "
FORMAT = 2
# A finding in an opt-out region.  Its entity is the kind and the entity of the finding in the region.
OPTED_OUT = "opted-out"
# A line of this kind is an opt-out region with its reason, and no finding.
REGION = "region"
NOT_FINDINGS = frozenset((OPTED_OUT, REGION))
# The spelling of a rule and of a kind of a finding line (parse_line).
RULE = re.compile(r"[a-z][a-z0-9-]*")
KIND = re.compile(r"[a-z][a-z0-9-]*(:\S+)?")


class SectionError(ValueError):
    """An object or a section does not have the format that this module reads."""


@dataclasses.dataclass(frozen=True, slots=True, order=True)
class Finding:
    """One finding of the plugin: its place, its rule, its kind and its entity."""

    file: str
    line: int
    column: int
    rule: str
    kind: str
    entity: str

    def text(self) -> str:
        """Return the finding as one line of a section, with no line break."""
        return f"{self.rule} {self.kind} {self.file}:{self.line}:{self.column} {self.entity}"


@dataclasses.dataclass(frozen=True, slots=True)
class Section:
    """The findings of one object, and the stamp of the compile that wrote them."""

    stamp: str
    findings: tuple[Finding, ...]


def parse_line(text: str, where: str) -> Finding:
    """Read one finding line of a section or of a report file.

    A rule is a word of lower-case ASCII letters, digits and hyphens.  A kind is such a word too, and the kind of a
    use that an admit row with `until` admits has a colon and the entry of the row after it.

    Raises:
        SectionError: If the line does not have the format
    """
    words = text.split(" ", 3)
    if len(words) < 3 or RULE.fullmatch(words[0]) is None or KIND.fullmatch(words[1]) is None:
        raise SectionError(f"{where}: the line {text[:120]!r} is not 'RULE KIND FILE:LINE:COLUMN ENTITY'")
    place = words[2].rsplit(":", 2)
    if len(place) != 3 or not place[0] or not all(part.isascii() and part.isdigit() for part in place[1:]):
        raise SectionError(f"{where}: the place {words[2]!r} is not FILE:LINE:COLUMN")
    return Finding(place[0], int(place[1]), int(place[2]), words[0], words[1], words[3] if len(words) == 4 else "")


def parse_section(data: bytes, where: str) -> Section:
    """Read the text of one section.

    Complexity: linear in the size of the section.

    Raises:
        SectionError: If the text is not UTF-8, or its head line or a finding line does not have the format
    """
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as problem:
        raise SectionError(f"{where}: the section is not UTF-8 ({problem})") from None
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    if not lines or not lines[0].startswith(HEAD_PREFIX):
        raise SectionError(f"{where}: the section has no head line {HEAD_PREFIX!r}")
    head = lines[0][len(HEAD_PREFIX) :].split(" ", 1)
    if head[0] != str(FORMAT) or len(head) != 2 or not head[1].startswith("stamp="):
        raise SectionError(f"{where}: the head line {lines[0]!r} is not format {FORMAT} with a stamp")
    findings = tuple(parse_line(line, f"{where}: line {number}") for number, line in enumerate(lines[1:], start=2))
    return Section(head[1][len("stamp=") :], findings)


def read_section(path: Path) -> bytes | None:
    """Return the bytes of the section .crucible.quarantine of one object, or None when it has none.

    Two sections with the name, as a relocatable link can make, give their
    bytes one after the other.  Complexity: linear in the number of sections.

    Raises:
        SectionError: If the file is not an ELF64 little-endian object, or a table or the section is outside it
        OSError: If the file cannot be read
    """
    try:
        with elf_file.ElfFile.mapped(path) as elf:
            parts = [elf.data(section) for section in elf.named(SECTION_NAME)]
    except elf_file.ElfError as problem:
        raise SectionError(str(problem)) from None
    return b"".join(parts) if parts else None


# ── The self-test ──────────────────────────────────────────────────


def planted_object(sections: list[tuple[bytes, bytes]], extended: bool = False) -> bytes:
    """Return an object with the named sections and their bytes, for a self-test."""
    return elf_file.make_file([elf_file.Planted(name, data) for name, data in sections], extended)


def self_test() -> int:
    """Do a test of each function with planted objects and sections.

    Returns:
        0 when each case holds, else 1
    """
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    def refuses(name: str, action: object) -> None:
        try:
            action()  # type: ignore[operator]
        except SectionError:
            expect(name, True)
            return
        expect(name, False)

    text = (
        b"# crucible-guard 2 stamp=00ab\n"
        b"quarantine std-object test/a.cpp:3:7 std::vector (std::vector<int>)\n"
        b"contract-specifier contract-specifier include/crucible/B.h:9:1 pre\n"
    )
    section = parse_section(text, "a.o")
    expect("parse_section reads the stamp", section.stamp == "00ab")
    expect(
        "parse_section reads each finding",
        section.findings
        == (
            Finding("test/a.cpp", 3, 7, "quarantine", "std-object", "std::vector (std::vector<int>)"),
            Finding("include/crucible/B.h", 9, 1, "contract-specifier", "contract-specifier", "pre"),
        ),
    )
    expect(
        "a finding gives its line back",
        section.findings[0].text() == "quarantine std-object test/a.cpp:3:7 std::vector (std::vector<int>)",
    )
    expect(
        "a section with the head line only has no finding",
        parse_section(b"# crucible-guard 2 stamp=\n", "a.o") == Section("", ()),
    )
    expect(
        "a line with no entity has an empty entity",
        parse_section(b"# crucible-guard 2 stamp=x\nquarantine region a.cpp:3:1\n", "a.o").findings[0].entity == "",
    )
    refuses("parse_section refuses a section with no head line", lambda: parse_section(text.split(b"\n", 1)[1], "a.o"))
    refuses("parse_section refuses another format", lambda: parse_section(b"# crucible-guard 1 stamp=x\n", "a.o"))
    refuses(
        "parse_section refuses the head line of format 1",
        lambda: parse_section(b"# crucible-quarantine 1 stamp=x\n", "a.o"),
    )
    refuses(
        "parse_section refuses a line that is not a finding",
        lambda: parse_section(b"# crucible-guard 2 stamp=x\nnoise\n", "a.o"),
    )
    refuses(
        "parse_section refuses a line of format 1",
        lambda: parse_section(b"# crucible-guard 2 stamp=x\nquarantine: std_object a.cpp:3:7 x\n", "a.o"),
    )
    refuses(
        "parse_section refuses a place with no column",
        lambda: parse_section(b"# crucible-guard 2 stamp=x\nquarantine std-object a.cpp:3 x\n", "a.o"),
    )
    arabic_digit = chr(0x663)
    refuses(
        "parse_section refuses a line number that is not ASCII",
        lambda: parse_section(
            f"# crucible-guard 2 stamp=x\nquarantine std-object a.cpp:{arabic_digit}:7 x\n".encode(), "a.o"
        ),
    )
    refuses(
        "parse_section refuses bytes that are not UTF-8",
        lambda: parse_section(b"# crucible-guard 2 stamp=x\n\xff\n", "a.o"),
    )

    with tempfile.TemporaryDirectory(prefix="quarantine-sections-") as scratch_text:
        scratch = Path(scratch_text)
        plain = scratch / "plain.o"
        plain.write_bytes(planted_object([(b".text", b"\x90"), (SECTION_NAME, text)]))
        expect("read_section finds the section", read_section(plain) == text)
        extended = scratch / "extended.o"
        extended.write_bytes(planted_object([(b".text", b"\x90"), (SECTION_NAME, text)], extended=True))
        expect("read_section follows the extended numbering", read_section(extended) == text)
        absent = scratch / "absent.o"
        absent.write_bytes(planted_object([(b".text", b"\x90"), (b".data", b"x")]))
        expect("read_section gives None for an object with no section", read_section(absent) is None)
        suffix = scratch / "suffix.o"
        suffix.write_bytes(planted_object([(b".text", b"\x90"), (b".rela.crucible.quarantine", b"r")]))
        expect("read_section does not take a longer name that ends with the name", read_section(suffix) is None)
        joined = scratch / "joined.o"
        joined.write_bytes(planted_object([(SECTION_NAME, b"one\n"), (SECTION_NAME, b"two\n")]))
        expect("read_section joins two sections with the name", read_section(joined) == b"one\ntwo\n")
        foreign = scratch / "foreign.o"
        foreign.write_bytes(b"!<arch>\n" + b"\0" * 64)
        refuses("read_section refuses a file that is not ELF", lambda: read_section(foreign))
        short = scratch / "short.o"
        short.write_bytes(planted_object([(SECTION_NAME, text)])[:-10])
        refuses("read_section refuses a cut table", lambda: read_section(short))
        empty = scratch / "empty.o"
        empty.write_bytes(b"")
        refuses("read_section refuses an empty file", lambda: read_section(empty))
        with contextlib.redirect_stdout(io.StringIO()) as printed, contextlib.redirect_stderr(io.StringIO()):
            status = print_objects([str(plain)])
            absent_status = print_objects([str(absent)])
        expect(
            "the command prints the finding lines of an object",
            status == 0 and printed.getvalue() == "".join(f"{item.text()}\n" for item in section.findings),
        )
        expect("the command fails for an object with no section", absent_status == 1)

    if failures:
        print(f"quarantine_sections --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 1
    print("quarantine_sections --self-test: every case holds.")
    return 0


def print_objects(paths: list[str]) -> int:
    """Print the finding lines of the section of each object, in the order of the section.

    Returns:
        0 when each object holds a section, 1 when an object holds none or cannot be read
    """
    status = 0
    for name in paths:
        try:
            data = read_section(Path(name))
            if data is None:
                print(f"guard sections: {name} holds no section {SECTION_NAME.decode()}", file=sys.stderr)
                status = 1
                continue
            for finding in parse_section(data, name).findings:
                print(finding.text())
        except (OSError, SectionError) as problem:
            print(f"guard sections: {problem}", file=sys.stderr)
            status = 1
    return status


def main(argv: list[str]) -> int:
    """Print the finding lines of objects (`guard sections`), or run the self-test.

    Args:
        argv: The paths of objects, or --self-test

    Returns:
        The exit status of the mode, or 2 on a usage error
    """
    if argv == ["--self-test"]:
        return self_test()
    if argv and not argv[0].startswith("-"):
        return print_objects(argv)
    print("usage: guard sections OBJECT... | guard selftest readers.sections", file=sys.stderr)
    return 2
