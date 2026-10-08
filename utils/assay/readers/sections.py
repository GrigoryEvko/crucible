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
    read() maps an object with assay.readers.elf, and it reads the bytes of
    each section with the name.  Two sections with the name, as a relocatable
    link can make, give their bytes one after the other.  No other module of
    the package names the section, and the rule structured-reads refuses each
    other read of it.

`guard sections OBJECT...` prints the finding lines of objects, and `guard
selftest tests.readers.test_sections` does the test of the module.
"""

import dataclasses
import os
import re
from pathlib import Path

from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.readers import elf

SECTION_NAME = ".crucible.quarantine"
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


def read(path: str | os.PathLike[str]) -> Section | None:
    """Return the findings and the stamp of the section of one object, or None when the object has no section.

    Complexity: linear in the number of sections and in the length of the section.

    Raises:
        SectionError: If the ELF reader cannot read the object, or the section does not have the format
        OSError: If the file cannot be read
    """
    try:
        with elf.mapped(path) as member:
            parts = [member.data(section) for section in member.named(SECTION_NAME)]
    except elf.ElfError as problem:
        raise SectionError(str(problem)) from None
    return parse_section(b"".join(parts), os.fspath(path)) if parts else None


def print_objects(paths: list[str]) -> ExitCode:
    """Print the finding lines of the section of each object, in the order of the section.

    Returns:
        ExitCode.OK when each object holds a section, ExitCode.FINDINGS when an object holds none or cannot be read
    """
    status = ExitCode.OK
    for name in paths:
        try:
            section = read(Path(name))
        except (OSError, SectionError) as problem:
            check_report.write_line(f"guard sections: {problem}", is_error=True)
            status = ExitCode.FINDINGS
            continue
        if section is None:
            check_report.write_line(f"guard sections: {name} holds no section {SECTION_NAME}", is_error=True)
            status = ExitCode.FINDINGS
            continue
        for finding in section.findings:
            check_report.write_line(finding.text())
    return status


def main(argv: list[str]) -> int:
    """Print the finding lines of objects (`guard sections`).

    Args:
        argv: The paths of objects

    Returns:
        The exit status of print_objects, or ExitCode.USAGE when no object is given
    """
    if argv and not any(word.startswith("-") for word in argv):
        return print_objects(argv)
    check_report.write_line("usage: guard sections OBJECT...", is_error=True)
    return ExitCode.USAGE
