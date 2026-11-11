"""sections — read the findings of the quarantine plugin from the section of an object.

THE SECTION
    The quarantine plugin of utils/tools/quarantine/ writes the findings of a
    unit into the section .crucible.quarantine of its object (the head comment
    of plugin.cpp, THE SECTION).  The section is text in UTF-8.  Its first line
    is "# crucible-guard 2 stamp=TEXT", and each other line is one finding:

        RULE KIND FILE:LINE:COLUMN ENTITY

    RULE is a rule of the engine plugin of utils/registry.txt, and KIND is a
    kind of that rule.  A record of the library code has the tag
    LIBRARY_RECORDS in the place of RULE, and no rule of the registry (THE
    SYMBOLS OF THE LIBRARY CODE).  FILE is relative to the source root.  A
    report file of the plugin argument out= holds the same finding lines,
    after a comment line.

THE OBJECT
    read() maps an object with assay.readers.elf, and it reads the bytes of
    each section with the name.  Two sections with the name, as a relocatable
    link can make, give their bytes one after the other.  No other module of
    the package names the section.

THE ENTITIES OF THE RECORDS
    The plugin writes the entity of some kinds in a grammar of its own, and
    the writer of each kind gives the grammar.  A function of this module
    reads such an entity into a typed record, and a rule reads the fields of
    the record.  No other module reads the entity of a finding as text.  In a
    record of the library code, quarantine and the family home, a field that
    holds words has one space between each two words.  The plugin can write
    more spaces:
      library code   The writer is utils/tools/quarantine/body.cpp (THE
                     RECORDS).  inlined: NAME.  call and reference: SYMBOL
                     NAME.  symbol: SYMBOL.  SYMBOL is an assembler name with
                     no space, and NAME can hold a space, as in operator new.
                     library_record() gives a LibraryRecord
      quarantine     The writers are report() of plugin.cpp and
                     check_include() of include.cpp.  `layer-header`:
                     <HEADER> in ALLOWANCE.  `upward-include`: TARGET
                     (TARGET_LAYER) from the layer LAYER.  quarantine_entity()
                     gives a LayerHeader, an UpwardInclude, or Words for each
                     other kind and for a text that does not have the grammar
                     of its kind.  opted-out: KIND ENTITY, the kind and the
                     entity of the finding in the region.  opted_out() gives
                     an OptedOut.  region: the reason of the region, as Words
      padded-list    The writer is record_padded_lists() of declaration.cpp.
                     DECLARATION: ELEMENT | count=N holes=H.  padded_list()
                     gives a PaddedList
      mint           The writers are check_mint() and check_mint_alias() of
                     declaration.cpp.  mint: SIGNATURE | name=NAME static=F
                     shape=SHAPE template=F nodiscard=F constexpr=F
                     noexcept=F constrained=F gated=F projection=F defined=F
                     class=NAME owner=CLASS, with each flag F 0 or 1, and "-"
                     for no class and no owner.  The owner comes last, because
                     the name of a class can hold a space.  mint-alias: ALIAS
                     | alias=NAME owner=CLASS.  mint_record() and mint_alias()
                     give a MintRecord and a MintAlias
      the family home  The writer is record_site() of home.cpp.  A name with
                     the names of its scopes, such as a::b::c.
                     `qualified_name()` gives a `QualifiedName`.
    Each fact of a record of padded-list or mint is necessary to its
    rule.  When such a record does not have its grammar, its function raises
    EntityError.

THE SYMBOLS OF THE LIBRARY CODE
    The records of the library code have four kinds.  The entity of a record
    call, reference or symbol starts with the assembler name of a symbol that
    an instruction of the unit names.  Section.symbols() gives these names
    with the first record of each.  The rule artifact-symbols explains the
    imports of a member of an archive with them, and `guard bom` gives each
    import a line of the source.

THE REPORT OF A PARSE BUILD
    A build of a configuration that only parses has no object (`loop syntax`,
    CRUCIBLE_SYNTAX_ONLY).  Its driver gives each unit the report directory
    report_directory(): SYNTAX_DIRECTORY, and then the path of the object
    below the build directory.  The plugin writes one report file there.  The
    driver writes the stamp of the compile command of the unit into the file
    STAMP_FILE of the same directory.  read_report() reads the two.

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
# The directory of a parse build that holds the report directory of each unit (THE REPORT OF A PARSE BUILD).
SYNTAX_DIRECTORY = Path("quarantine") / "syntax"
# The file of a report directory that holds the stamp of the compile command of its unit.
STAMP_FILE = "stamp"
# The suffix of a report file of the plugin argument out=.
REPORT_SUFFIX = ".quarantine"
# The tag of the records of the library code, and the kinds of its records that name a symbol first (THE SYMBOLS OF
# THE LIBRARY CODE).
LIBRARY_RECORDS = "library-records"
SYMBOL_RECORDS = frozenset({"call", "reference", "symbol"})
# The two kinds of the rule quarantine with an include grammar, and the texts that divide their parts (THE ENTITIES OF
# THE RECORDS).
LAYER_HEADER = "layer-header"
UPWARD_INCLUDE = "upward-include"
HEADER_END = "> in "
TARGET_LAYER_START = " ("
TARGET_LAYER_END = ") from the layer "
# The texts that divide the parts of a record of padded-list and of mint, and their facts.
FACTS_SEPARATOR = " | "
ELEMENT_SEPARATOR = ": "
PADDED_FACTS = ("count", "holes")
OWNER_FACT = " owner="
ALIAS_FACT = "alias="
MINT_TEXT_FACTS = ("name", "shape", "class")
# The flags of a mint record, in the sequence of the record.
MINT_FLAG_FACTS = (
    "static",
    "template",
    "nodiscard",
    "constexpr",
    "noexcept",
    "constrained",
    "gated",
    "projection",
    "defined",
)
MINT_SHAPES = frozenset({"ctx", "token", "member"})
# The class and the owner of a mint that is no member.
NO_OWNER = "-"
# The text between the name of a scope and the name in it (the family home).
SCOPE_SEPARATOR = "::"


class SectionError(ValueError):
    """An object or a section does not have the format that this module reads."""


class EntityError(SectionError):
    """The entity of a record does not have the grammar of its kind (THE ENTITIES OF THE RECORDS)."""


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

    def symbols(self) -> dict[str, Finding]:
        """Return the first record call, reference or symbol of the library code of each symbol that one names.

        THE SYMBOLS OF THE LIBRARY CODE gives the records.  Complexity: linear in the findings.
        """
        found: dict[str, Finding] = {}
        for item in self.findings:
            if item.rule != LIBRARY_RECORDS or item.kind not in SYMBOL_RECORDS:
                continue
            symbol = library_record(item.kind, item.entity).symbol
            if symbol:
                found.setdefault(symbol, item)
        return found


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
            return of_member(member)
    except elf.ElfError as problem:
        raise SectionError(str(problem)) from None


def of_member(member: elf.Member) -> Section | None:
    """Return the findings and the stamp of the section of one ELF file or member, or None when it has no section.

    A member of an archive holds the section of its object (THE OBJECT).  Complexity: linear in the number of
    sections and in the length of the section.

    Raises:
        SectionError: If the ELF reader cannot read the member, or the section does not have the format
    """
    try:
        parts = [member.data(section) for section in member.named(SECTION_NAME)]
    except elf.ElfError as problem:
        raise SectionError(str(problem)) from None
    return parse_section(b"".join(parts), member.where) if parts else None


def report_directory(build_dir: Path, object_path: str | os.PathLike[str]) -> Path:
    """Return the report directory of one object of a parse build (THE REPORT OF A PARSE BUILD)."""
    return build_dir / SYNTAX_DIRECTORY / os.path.relpath(object_path, build_dir)


def read_report(directory: Path) -> Section | None:
    """Return the findings of the one report file of a report directory and the stamp of that directory.

    The result is None when the directory holds no stamp or no report file.  A line of the report that starts with #
    is a comment.  Complexity: linear in the length of the report.

    Raises:
        SectionError: If the directory holds more than one report file, or a line does not have the format
        OSError: If a file cannot be read
    """
    stamp_path = directory / STAMP_FILE
    reports = sorted(directory.glob(f"*{REPORT_SUFFIX}")) if directory.is_dir() else []
    if not stamp_path.is_file() or not reports:
        return None
    if len(reports) > 1:
        raise SectionError(f"{directory} holds {len(reports)} report files, and a unit writes one")
    return Section(stamp_path.read_text(encoding="utf-8").strip(), read_report_file(reports[0]))


def read_report_file(path: Path) -> tuple[Finding, ...]:
    """Return the findings of one report file of the plugin.  A line that starts with # is a comment.

    Complexity: linear in the length of the report.

    Raises:
        SectionError: If a line does not have the format
        OSError: If the file cannot be read
        UnicodeDecodeError: If the file is not UTF-8
    """
    lines = path.read_text(encoding="utf-8").splitlines()
    return tuple(
        parse_line(line, f"{path}: line {number}")
        for number, line in enumerate(lines, start=1)
        if line and not line.startswith("#")
    )


# ── The entities of the records ────────────────────────────────────


@dataclasses.dataclass(frozen=True, slots=True)
class LibraryRecord:
    """The entity of a record of the library code (THE ENTITIES OF THE RECORDS).

    `symbol` is the assembler name of a record call, reference or symbol, and empty for a record inlined.  `name` is
    the name of the library function or variable, and empty for a record symbol.
    """

    symbol: str
    name: str


@dataclasses.dataclass(frozen=True, slots=True)
class Words:
    """An entity of the rule quarantine with no parts: its words with one space between each two."""

    text: str


@dataclasses.dataclass(frozen=True, slots=True)
class LayerHeader:
    """The entity `<HEADER> in ALLOWANCE` of a finding layer-header.

    `text` is the full entity, `header` is the header with its angle brackets, and `allowance` names the layers of the
    headers that the file can include.
    """

    text: str
    header: str
    allowance: str


@dataclasses.dataclass(frozen=True, slots=True)
class UpwardInclude:
    """The entity `TARGET (TARGET_LAYER) from the layer LAYER` of a finding `upward-include`.

    `text` is the full entity.  `target` is the file that the include names, `target_layer` is "the layer NAME" or
    "no layer", and `layer` is the layer of the file that holds the include.
    """

    text: str
    target: str
    target_layer: str
    layer: str


# The entity of a finding of the rule quarantine that is not opted out.
type FindingEntity = Words | LayerHeader | UpwardInclude


@dataclasses.dataclass(frozen=True, slots=True)
class OptedOut:
    """The entity `KIND ENTITY` of a finding opted-out: the kind and the entity of the finding in the region.

    `text` is the full entity, and `inner` is the entity of the finding in the region.
    """

    text: str
    kind: str
    inner: FindingEntity


@dataclasses.dataclass(frozen=True, slots=True)
class PaddedList:
    """The entity `DECLARATION: ELEMENT | count=N holes=H` of a line of the rule padded-list.

    `entity` is `DECLARATION: ELEMENT`, as the plugin wrote it.  `count` is the element count of the list, and `holes`
    is the number of the padding bits of one element.
    """

    entity: str
    declaration: str
    element: str
    count: int
    holes: int


@dataclasses.dataclass(frozen=True, slots=True)
class MintRecord:
    """The entity of a record mint of the rule mint.

    `entity` is the signature of the mint: its full name with its namespaces, and the types of its parameters.
    `parameters` is the part of the signature after the name, from its opening parenthesis.  `class_name` is the name
    of the class of a member with no namespace, and `owner` is the full name of the class, each NO_OWNER for no
    member.  Each flag is a fact of the record.
    """

    entity: str
    parameters: str
    name: str
    shape: str
    class_name: str
    owner: str
    is_static: bool
    is_template: bool
    is_nodiscard: bool
    is_constexpr: bool
    is_noexcept: bool
    is_constrained: bool
    is_gated: bool
    is_projection: bool
    is_defined: bool

    @property
    def has_owner(self) -> bool:
        """Return True for a member of a class: a mint with an owner."""
        return self.owner != NO_OWNER


@dataclasses.dataclass(frozen=True, slots=True)
class MintAlias:
    """The entity `ALIAS | alias=NAME owner=CLASS` of a record mint-alias of the rule mint.

    `entity` is the full name of the alias, `name` is its name with no namespace, and `owner` is the full name of the
    class that it names.
    """

    entity: str
    name: str
    owner: str


@dataclasses.dataclass(frozen=True, slots=True)
class QualifiedName:
    """The entity of a line of a rule of the family home: a name with the names of its scopes.

    `text` is the name, and `simple_name` is its last part, after the last `::`.
    """

    text: str
    simple_name: str


def words_of(text: str) -> str:
    """Return the words of a text with one space between each two (THE ENTITIES OF THE RECORDS)."""
    return " ".join(text.split())


def library_record(kind: str, text: str) -> LibraryRecord:
    """Return the record of the entity of a line of the library code (THE ENTITIES OF THE RECORDS).

    A record of a kind of SYMBOL_RECORDS gives its first word as the symbol and its other words as the name.  Each
    other kind gives its words as the name.
    """
    words = text.split()
    if kind not in SYMBOL_RECORDS:
        return LibraryRecord("", " ".join(words))
    return LibraryRecord(words[0] if words else "", " ".join(words[1:]))


def quarantine_entity(kind: str, text: str) -> FindingEntity:
    """Return the record of the entity of a finding of the rule quarantine (THE ENTITIES OF THE RECORDS).

    A finding `layer-header` gives a LayerHeader and a finding `upward-include` an UpwardInclude, when the text has
    the grammar of the kind.  Each other entity gives its Words.  The function takes the kind and the text, because the
    census of a fixture holds the same entities with no section.
    """
    words = words_of(text)
    if kind == LAYER_HEADER:
        head, separator, allowance = words.partition(HEADER_END)
        if separator and head.startswith("<"):
            return LayerHeader(words, f"{head}>", allowance)
    elif kind == UPWARD_INCLUDE:
        target, separator, rest = words.partition(TARGET_LAYER_START)
        target_layer, layer_separator, layer = rest.partition(TARGET_LAYER_END)
        if target and separator and layer_separator:
            return UpwardInclude(words, target, target_layer, layer)
    return Words(words)


def opted_out(text: str) -> OptedOut:
    """Return the record of the entity of a finding opted-out of the rule quarantine (THE ENTITIES OF THE RECORDS).

    The first word is the kind of the finding in the region, and the other words are its entity.
    """
    words = words_of(text)
    kind, _, entity = words.partition(" ")
    return OptedOut(words, kind, quarantine_entity(kind, entity))


def _facts_of(text: str, where: str) -> dict[str, str]:
    """Return the value of each fact of a text of facts `KEY=VALUE KEY=VALUE ...`, in the sequence of the text.

    Raises:
        EntityError: If a word is not KEY=VALUE, or a key is there two times
    """
    values: dict[str, str] = {}
    for word in text.split(" "):
        key, equal, value = word.partition("=")
        if not equal or not key or key in values:
            raise EntityError(f"{where}: the fact {word!r} is not KEY=VALUE, or its key is there two times")
        values[key] = value
    return values


def is_number(text: str) -> bool:
    """Return True for a run of ASCII decimal digits."""
    return text.isascii() and text.isdigit()


def padded_list(text: str, where: str) -> PaddedList:
    """Return the record of the entity of a line of the rule padded-list (THE ENTITIES OF THE RECORDS).

    `where` names the line in a message.

    Raises:
        EntityError: If the entity is not `DECLARATION: ELEMENT | count=N holes=H`
    """
    entity, separator, facts = text.rpartition(FACTS_SEPARATOR)
    declaration, element_separator, element = entity.partition(ELEMENT_SEPARATOR)
    shape = f"{where}: a line of padded-list is 'DECLARATION: ELEMENT | count=N holes=H'"
    if not separator or not element_separator or not declaration or not element:
        raise EntityError(shape)
    values = _facts_of(facts, where)
    if tuple(values) != PADDED_FACTS or not all(is_number(value) for value in values.values()):
        raise EntityError(shape)
    return PaddedList(entity, declaration, element, int(values["count"]), int(values["holes"]))


def mint_record(text: str, where: str) -> MintRecord:
    """Return the record of the entity of a record mint of the rule mint (THE ENTITIES OF THE RECORDS).

    `where` names the line in a message.

    Raises:
        EntityError: If the entity is not `SIGNATURE | FACTS` with each fact of a mint record, the owner last
    """
    entity, separator, facts = text.rpartition(FACTS_SEPARATOR)
    head, marker, owner = facts.partition(OWNER_FACT)
    if not separator or not entity or not marker or not owner:
        raise EntityError(f"{where}: a mint record is 'SIGNATURE | FACTS', and its last fact is the owner")
    values = _facts_of(head, where)
    flags = [values.get(key) for key in MINT_FLAG_FACTS]
    if set(values) != {*MINT_TEXT_FACTS, *MINT_FLAG_FACTS} or any(value not in ("0", "1") for value in flags):
        raise EntityError(
            f"{where}: a mint record gives {', '.join(MINT_TEXT_FACTS)} and the flags {', '.join(MINT_FLAG_FACTS)}, "
            f"each flag 0 or 1"
        )
    if values["shape"] not in MINT_SHAPES:
        raise EntityError(f"{where}: the shape {values['shape']!r} is not one of {', '.join(sorted(MINT_SHAPES))}")
    name = values["name"]
    _, opening, after = entity.rpartition(f"{name}(")
    return MintRecord(
        entity,
        f"({after}" if opening else "",
        name,
        values["shape"],
        values["class"],
        owner,
        *(flag == "1" for flag in flags),
    )


def mint_alias(text: str, where: str) -> MintAlias:
    """Return the record of the entity of a record mint-alias of the rule mint (THE ENTITIES OF THE RECORDS).

    `where` names the line in a message.

    Raises:
        EntityError: If the entity is not `ALIAS | alias=NAME owner=CLASS`
    """
    entity, separator, facts = text.rpartition(FACTS_SEPARATOR)
    head, marker, owner = facts.partition(OWNER_FACT)
    name = head.removeprefix(ALIAS_FACT)
    if not separator or not entity or not marker or not owner or not head.startswith(ALIAS_FACT) or " " in name:
        raise EntityError(f"{where}: an alias record is 'ALIAS | alias=NAME owner=CLASS'")
    return MintAlias(entity, name, owner)


def qualified_name(text: str) -> QualifiedName:
    """Return the record of the entity of a line of a rule of the family home (THE ENTITIES OF THE RECORDS)."""
    words = words_of(text)
    return QualifiedName(words, words.rpartition(SCOPE_SEPARATOR)[2])


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
