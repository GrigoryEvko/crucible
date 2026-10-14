"""The test of the reader of the section of the quarantine plugin (assay.readers.sections).

`guard selftest tests.readers.test_sections` runs the test.  Each case writes
one line.  Exit 0 when each case holds, else 1.

The cases plant objects with the writer of assay.readers.elf.  An object
has the section, no section, or two sections of the name.  An object also
has the extended numbering, or a name that only ends with the name of the
section.  The cases of the entities plant the entity of each kind of THE
ENTITIES OF THE RECORDS, with more spaces than the plugin writes.  They
also plant each entity of padded-list and mint-pattern that does not read.
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
    records = sections.parse_section(
        b"# crucible-guard 2 stamp=x\n"
        b"library-code call a.h:3:5 _Znwm operator new\n"
        b"library-code reference a.h:4:1 stderr stderr\n"
        b"library-code symbol a.h:5:2 memcpy\n"
        b"library-code symbol a.h:9:2 _Znwm\n"
        b"library-code inlined a.h:6:3 std::exchange\n"
        b"quarantine std-entity a.h:7:1 memset\n",
        "a.o",
    )
    expect(
        "symbols gives the first record call, reference or symbol of each symbol, and no record inlined or finding",
        {name: (item.line, item.kind) for name, item in records.symbols().items()}
        == {"_Znwm": (3, "call"), "stderr": (4, "reference"), "memcpy": (5, "symbol")},
    )
    blank = sections.parse_section(b"# crucible-guard 2 stamp=x\nlibrary-code symbol a.h:5:2 \n", "a.o")
    expect("a record symbol with no entity names no symbol", blank.symbols() == {})


def refuses_entity(read: Callable[[str, str], object], text: str) -> bool:
    """Return True when a read of a planted entity raises EntityError."""
    try:
        read(text, "planted")
    except sections.EntityError:
        return True
    return False


def library_cases(expect: testing.Expect) -> None:
    """Check library_record() on each kind of a record of the rule library-code."""
    expect(
        "a record call gives its symbol and its name, with one space between the words of the name",
        sections.library_record("call", "_Znwm operator  new") == sections.LibraryRecord("_Znwm", "operator new"),
    )
    expect(
        "a record reference gives its symbol and its name",
        sections.library_record("reference", "stderr stderr") == sections.LibraryRecord("stderr", "stderr"),
    )
    expect(
        "a record symbol gives its symbol and no name",
        sections.library_record("symbol", "memcpy") == sections.LibraryRecord("memcpy", ""),
    )
    expect(
        "a record inlined gives no symbol, and its whole entity is the name",
        sections.library_record("inlined", "(anonymous namespace)::helper")
        == sections.LibraryRecord("", "(anonymous namespace)::helper"),
    )
    expect(
        "an empty entity gives no symbol and no name",
        sections.library_record("call", " ") == sections.LibraryRecord("", ""),
    )


def quarantine_cases(expect: testing.Expect) -> None:
    """Check quarantine_entity() and opted_out() on the entities of the rule quarantine."""
    header = sections.quarantine_entity(sections.LAYER_HEADER, "<vector>  in the layer X1_wrappers")
    expect(
        "an include of a layer header gives the header and the allowance, with one space between the words",
        header == sections.LayerHeader("<vector> in the layer X1_wrappers", "<vector>", "the layer X1_wrappers"),
    )
    allowance = "the layer F0_core, a file of the language with the allowance of the layer F0_primitives"
    language = f"<atomic> in {allowance}"
    expect(
        "the allowance of a file of the language holds its two layers",
        sections.quarantine_entity(sections.LAYER_HEADER, language)
        == sections.LayerHeader(language, "<atomic>", allowance),
    )
    upward = "include/fixy/concurrent/ParallelismRule.h (the layer X4_concurrent) from the layer X3_os"
    expect(
        "an upward include gives the target, the layer of the target and the layer of the file",
        sections.quarantine_entity(sections.UPWARD_INCLUDE, upward)
        == sections.UpwardInclude(
            upward, "include/fixy/concurrent/ParallelismRule.h", "the layer X4_concurrent", "X3_os"
        ),
    )
    no_layer = "include/crucible/Arena.h (no layer) from the layer X1_wrappers"
    expect(
        "the target of an upward include can have no layer",
        sections.quarantine_entity(sections.UPWARD_INCLUDE, no_layer)
        == sections.UpwardInclude(no_layer, "include/crucible/Arena.h", "no layer", "X1_wrappers"),
    )
    expect(
        "an include entity without its grammar gives its words",
        sections.quarantine_entity(sections.LAYER_HEADER, "<vector>") == sections.Words("<vector>")
        and sections.quarantine_entity(sections.LAYER_HEADER, "vector> in the layer X1")
        == sections.Words("vector> in the layer X1")
        and sections.quarantine_entity(sections.UPWARD_INCLUDE, "include/crucible/X.h")
        == sections.Words("include/crucible/X.h")
        and sections.quarantine_entity(sections.UPWARD_INCLUDE, "a.h (the layer X1)")
        == sections.Words("a.h (the layer X1)"),
    )
    expect(
        "each other kind, also a region, gives its words",
        sections.quarantine_entity("std-entity", " std::array::operator[] ") == sections.Words("std::array::operator[]")
        and sections.quarantine_entity(sections.REGION, "PROBE:  a  region") == sections.Words("PROBE: a region"),
    )
    expect(
        "an opted-out finding gives the kind and the record of the finding in the region",
        sections.opted_out("layer-header  <vector> in the layer X3_os")
        == sections.OptedOut(
            "layer-header <vector> in the layer X3_os",
            sections.LAYER_HEADER,
            sections.LayerHeader("<vector> in the layer X3_os", "<vector>", "the layer X3_os"),
        ),
    )
    expect(
        "an opted-out finding of a kind with no entity gives an empty inner entity",
        sections.opted_out("raw-pointer-object")
        == sections.OptedOut("raw-pointer-object", "raw-pointer-object", sections.Words("")),
    )


PADDED = "crucible::Table::rows: crucible::Row | count=256 holes=24"


def padded_cases(expect: testing.Expect) -> None:
    """Check padded_list() on a line of the rule padded-list, and on each line that does not read."""
    expect(
        "a line gives the declaration, the element, the count and the holes",
        sections.padded_list(PADDED, "planted")
        == sections.PaddedList(
            "crucible::Table::rows: crucible::Row", "crucible::Table::rows", "crucible::Row", 256, 24
        ),
    )
    parameter = "crucible::f(crucible::Row) parameter 1: crucible::Row | count=64 holes=8"
    expect(
        "the declaration of a parameter holds the signature and the number of the parameter",
        sections.padded_list(parameter, "planted").declaration == "crucible::f(crucible::Row) parameter 1",
    )
    broken = (
        "crucible::Table::rows: crucible::Row",
        "crucible::Table::rows crucible::Row | count=256 holes=24",
        ": crucible::Row | count=256 holes=24",
        "crucible::Table::rows: crucible::Row | count=256",
        "crucible::Table::rows: crucible::Row | count=x holes=24",
        f"crucible::Table::rows: crucible::Row | count={chr(0x663)} holes=24",
        "crucible::Table::rows: crucible::Row | count=256 holes=24 extra=1",
        "crucible::Table::rows: crucible::Row | count=256 count=256 holes=24",
    )
    expect(
        "a line with no facts, no element, no declaration, a missing fact, a fact that is no number, a fact that is "
        "not ASCII, an extra fact or a second fact does not read",
        all(refuses_entity(sections.padded_list, text) for text in broken),
    )
    try:
        sections.padded_list(broken[0], "'the planted line'")
        message = ""
    except sections.EntityError as problem:
        message = str(problem)
    expect("the error names the line", message.startswith("'the planted line': "))


MINT_FACTS = (
    "name=mint_cell static=0 shape=ctx template=1 nodiscard=1 constexpr=0 noexcept=1 constrained=1 gated=1 projection=0"
)
MINT_SIGNATURE = "fixy::Cell::mint_cell(const Ctx&, int) const"


def mint_text(facts: str = MINT_FACTS, defined: int = 1, owner: str = "fixy::Cell<int, char>") -> str:
    """Return the entity of a planted mint record with its facts, the fact `defined`, the class and the owner."""
    return f"{MINT_SIGNATURE} | {facts} defined={defined} class=Cell owner={owner}"


def mint_cases(expect: testing.Expect) -> None:
    """Check mint_record() and mint_alias() on the records of the rule mint-pattern, and each record that does not read."""
    mint = sections.mint_record(mint_text(), "planted")
    expect(
        "a record gives the signature, the parameters, the name, the shape, the class and the owner",
        (mint.entity, mint.parameters, mint.name, mint.shape, mint.class_name, mint.owner)
        == (MINT_SIGNATURE, "(const Ctx&, int) const", "mint_cell", "ctx", "Cell", "fixy::Cell<int, char>"),
    )
    expect(
        "a record gives each flag",
        mint.is_template
        and mint.is_nodiscard
        and not mint.is_constexpr
        and mint.is_noexcept
        and mint.is_constrained
        and mint.is_gated
        and not mint.is_projection
        and mint.is_defined
        and not mint.is_static,
    )
    expect(
        "a mint with an owner is a member, and the owner - is none",
        mint.has_owner and not sections.mint_record(mint_text(owner="-"), "planted").has_owner,
    )
    broken = (
        f"{MINT_SIGNATURE} {MINT_FACTS}",
        mint_text(defined=2),
        mint_text(facts=MINT_FACTS.replace("shape=ctx", "shape=free")),
        mint_text(facts=MINT_FACTS.replace(" gated=1", "")),
        mint_text(facts=f"{MINT_FACTS} name=again"),
        mint_text(facts=f"{MINT_FACTS} loose"),
        mint_text(owner=""),
    )
    expect(
        "a record with no facts, a flag of 2, an unknown shape, a missing flag, a second name, a word that is no fact "
        "or no owner does not read",
        all(refuses_entity(sections.mint_record, text) for text in broken),
    )
    expect(
        "an alias record gives the alias, its name and the owner with a space",
        sections.mint_alias("fixy::Wide | alias=Wide owner=fixy::Cell<int, char>", "planted")
        == sections.MintAlias("fixy::Wide", "Wide", "fixy::Cell<int, char>"),
    )
    broken_aliases = (
        "fixy::Epoch alias=Epoch owner=fixy::Counter",
        "fixy::Epoch | name=Epoch owner=fixy::Counter",
        "fixy::Epoch | alias=Epoch",
        "fixy::Epoch | alias=Two Words owner=fixy::Counter",
        " | alias=Epoch owner=fixy::Counter",
    )
    expect(
        "an alias record with no facts, no fact alias, no owner, a name with a space or no alias does not read",
        all(refuses_entity(sections.mint_alias, text) for text in broken_aliases),
    )


def name_cases(expect: testing.Expect) -> None:
    """Check qualified_name() on the entities of the family home."""
    expect(
        "a qualified name gives its last part",
        sections.qualified_name("foundation::algebra::is_graded_specialization")
        == sections.QualifiedName("foundation::algebra::is_graded_specialization", "is_graded_specialization"),
    )
    expect(
        "a name of one part is its own last part, and the name has one space between its words",
        sections.qualified_name("admitted_retags") == sections.QualifiedName("admitted_retags", "admitted_retags")
        and sections.qualified_name("(anonymous  namespace)::(lambda)")
        == sections.QualifiedName("(anonymous namespace)::(lambda)", "(lambda)"),
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
        archive = scratch / "liball.a"
        archive.write_bytes(elf.make_archive([("plain.o", plain.read_bytes()), ("absent.o", absent.read_bytes())]))
        expect(
            "of_member reads the section of each member of an archive",
            [sections.of_member(member) for member in elf.members(archive)]
            == [sections.Section("00ab", FINDINGS), None],
        )
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
        report = directory / f"a.cpp.0123456789abcdef{sections.REPORT_SUFFIX}"
        report.write_text(report_text, encoding="utf-8")
        expect(
            "read_report_file reads each finding line of one report file, and skips the comment",
            sections.read_report_file(report) == FINDINGS,
        )
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
    return testing.run(
        __name__.removeprefix("assay."),
        argv,
        (
            parse_cases,
            library_cases,
            quarantine_cases,
            padded_cases,
            mint_cases,
            name_cases,
            object_cases,
            report_cases,
        ),
    )
