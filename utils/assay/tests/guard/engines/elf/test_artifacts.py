"""The test of the rule artifacts (assay.guard.engines.elf.artifacts).

`guard selftest tests.guard.engines.elf.test_artifacts` runs the test.
Each case writes one line.  Exit 0 when each case holds, else 1.

THE CASES
    The cases hold these parts of the rule:
      * The groups and the paths of the facts, the facts of planted
        programs, a link with no map, and the verdict of a planted ledger
      * Each reason of a run path directory that the host does not give, and
        a planted program that names such a directory in its run path
      * The problems of the hardening facts of one artifact, for each kind
        and each machine, with planted facts
      * The inputs of a link that remove the control-flow protection, from a
        planted mold map, planted objects and a planted archive
      * The kinds of a row `kinds` of a plant
      * The read of linked artifacts of planted builds, the groups of the
        problems, the facts and the problems of one build together, and the
        builds that the rule does not apply to.
    The compile cases use the compiler that ASSAY_TEST_CXX names, and the
    registration of the test in test/CMakeLists.txt gives the compiler of
    the build.
"""

from collections.abc import Iterable
from dataclasses import replace
from pathlib import Path
from tempfile import TemporaryDirectory

from assay.core import ledger as rule_ledger
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core import testing
from assay.guard.engines.elf import artifacts, one_set, runner
from assay.readers import elf, linkmap
from assay.tests.guard.engines.elf.test_runner import raised

# A program with a constructor and a call of atexit, and a program that calls the C library.
TOOL = """\
#include <cstdlib>
volatile int planted_state = 0;
[[gnu::constructor]] static void planted_init() { planted_state = 1; }
static void planted_exit() { planted_state = 2; }
int main() { return std::atexit(planted_exit); }
"""
VESSEL = """\
#include <cstdio>
int main(int count, char**) { return std::printf("%d\\n", count); }
"""
# The rows of plant.txt that limit a plant to the facts, or to the properties (THE PLANTS).
FACT_ROW = f"{artifacts.KINDS_ROW} {' '.join(artifacts.FACT_KINDS)}\n"
PROPERTY_ROW = f"{artifacts.KINDS_ROW} {' '.join(artifacts.PROPERTY_KINDS)}\n"
MANIFEST = (
    f"{FACT_ROW}program examples tool tool.cpp\nprogram vessel vessel_tool vessel.cpp\nprogram test check tool.cpp\n"
)
X86 = artifacts.PROTECTIONS[elf.EM_X86_64]
AARCH64 = artifacts.PROTECTIONS[elf.EM_AARCH64]
ALL = frozenset(artifacts.PROPERTY_KINDS)
# The hardening facts of an executable of x86_64 with each property.
HARDENED = artifacts.Hardening(
    is_executable=True,
    file_type=elf.ET_DYN,
    machine=elf.EM_X86_64,
    flags=elf.DF_BIND_NOW,
    flags_1=elf.DF_1_PIE | elf.DF_1_NOW,
    has_bind_now_tag=False,
    has_relro=True,
    stack_flags=6,
    features={X86.kind: X86.required},
)
# The program of the compile cases, and the link words of a hardened program.
PROGRAM = "int main() { return 0; }\n"
HARDENED_LINK = "-pie -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack"


def kinds_of(hardening: artifacts.Hardening, kinds: frozenset[str] = ALL) -> list[str]:
    """Return the kind of each problem of the hardening facts."""
    return [problem.kind for problem in artifacts.problems_of(hardening, kinds)]


def details_of(hardening: artifacts.Hardening) -> str:
    """Return the words of each problem of the hardening facts, in one text."""
    return " | ".join(problem.detail for problem in artifacts.problems_of(hardening, ALL))


def errors_of(findings: Iterable[check_report.Finding]) -> list[str]:
    """Return the kind of each error."""
    return [item.kind for item in findings if item.level == "error"]


def ledger_head(context: runner.ElfContext) -> tuple[Path, str]:
    """Make the directory of the ledger of the rule in the tree of a context, and return the file and its head."""
    ledger = rule_ledger.ledger_path(artifacts.RULE, context.root)
    ledger.parent.mkdir(parents=True)
    return ledger, f"format {rule_ledger.FORMAT}\nrule {artifacts.RULE}\n"


def planted_object(word: int | None, kind: int = X86.kind) -> bytes:
    """Return an object with the feature property KIND of a word in its section .note.gnu.property, or no section."""
    note = elf.property_note([(kind, word.to_bytes(4, "little"))]) if word is not None else b""
    sections = [elf.Planted(artifacts.PROPERTY_SECTION, note)] if word is not None else []
    return elf.make_file([elf.Planted(".text", b"\x90"), *sections])


def planted_map(inputs: Iterable[str]) -> str:
    """Return a mold map with one output section, one input section of each input, and one symbol of the first."""
    rows = [linkmap.HEADING, f"{0:>18}{64:>11}{8:>6} EHDR", f"{0x1000:#18x}{16:>11}{16:>6} .text"]
    for index, item in enumerate(inputs):
        rows.append(f"{0x1000 + index:#18x}{1:>11}{1:>6}         {item}:(.text)")
        if not index:
            rows.append(f"{0x1000:#18x}          0     0                 main(int, char**)")
    return "\n".join(rows) + "\n"


# ── The facts ──────────────────────────────────────────────────────


def path_cases(expect: testing.Expect) -> None:
    """Hold the group of a source directory and the words of a run path (THE PATHS)."""
    expect(
        "the first directory of a source directory gives the group, and each other directory is tools",
        [artifacts.group_of(name) for name in ("vessel", "bench", "test", "examples", "")]
        == ["vessel", "benches", "tests", "tools", "tools"],
    )
    words = artifacts.path_words("/opt/gcc/usr/bin/g++", Path("/work/build"))
    expect(
        "a run path gets the word of the toolchain and of the build directory, and a different path stays",
        [artifacts.shown_path(item, words) for item in ("/opt/gcc/usr/lib64", "/work/build/test", "/usr/lib")]
        == ["{toolchain}/usr/lib64", "{build}/test", "/usr/lib"],
    )


def planted_context(scratch: Path, compiler: str) -> runner.ElfContext:
    """Write the planted tree, build its programs, and give the context of the build."""
    tree = scratch / "tree"
    tree.mkdir()
    (tree / "tool.cpp").write_text(TOOL, encoding="utf-8")
    (tree / "vessel.cpp").write_text(VESSEL, encoding="utf-8")
    (tree / runner.MANIFEST).write_text(MANIFEST, encoding="utf-8")
    return runner.ElfContext(tree, None, runner.planted_build(tree, scratch / "build", compiler))


def fact_cases(expect: testing.Expect, compiler: str) -> None:
    """Build planted programs, and hold their facts, a link with no map and the verdict of a ledger."""
    with TemporaryDirectory(prefix="artifacts-facts-") as scratch_text:
        scratch = Path(scratch_text)
        context = planted_context(scratch, compiler)
        reading = artifacts.read_build(context, scratch / "build")
        facts, counts = reading.facts, reading.counts
        found = {(fact.group, fact.kind, fact.entity) for fact in facts}
        expect("each program counts in its group", counts == {"tests": 1, "tools": 1, "vessel": 1})
        expect("a program with no run path names no missing directory", reading.missing == ())
        expect("a row kinds of the facts reads no property", dict(reading.problems) == {})
        expect("a program gives its needed libraries", ("tools", "needed", "libc.so.6") in found)
        expect(
            "the map gives the member of libc_nonshared.a that the link takes for atexit",
            ("tools", "static-member", "libc_nonshared.a(atexit.oS)") in found,
        )
        expect(
            "each fact has a kind of the rule, so a program gives no fact of the host, and the vessel gives no import",
            {kind for _, kind, _ in found} <= set(artifacts.FACT_KINDS),
        )
        rule = rule_ledger.rule_of(artifacts.RULE)
        ledger, head = ledger_head(context)
        ledger.write_text(head, encoding="utf-8")
        inspection = artifacts.examine(rule, context)
        expect("each fact that the ledger does not hold is an error", len(errors_of(inspection.findings)) == len(facts))
        rows = "".join(f"{item.path} | {item.kind} | 1 | {item.entity} | -\n" for item in inspection.counts)
        ledger.write_text(f"{head}{rows}", encoding="utf-8")
        judged = artifacts.examine(rule, context).findings
        expect("a ledger with each fact gives no error", errors_of(judged) == [])
        gone = "@build/tools | needed | 1 | libplanted_gone.so | -\n"
        ledger.write_text(f"{head}{rows}{gone}", encoding="utf-8")
        judged = artifacts.examine(rule, context).findings
        expect("a row of a fact that goes gives no error", errors_of(judged) == [])
        configured = "configuration planted kind planted\nconfiguration planted tier planted\n"
        ledger.write_text(f"{head}{configured}{rows}", encoding="utf-8")
        judged = artifacts.examine(rule, context).findings
        expect(
            "a ledger with a configuration row cannot be read, and that is an error",
            any("cannot be read" in item.message for item in judged if item.level == "error"),
        )
        program = next(item.path for item in context.linked_artifacts() if item.directory == "examples")
        program.with_name(program.name + artifacts.MAP_SUFFIX).unlink()
        said = raised(lambda: artifacts.map_facts(program, scratch / "build"), runner.BuildError) or ""
        expect("a link with no map is an error that names the file of cmake", "cmake/LinkMap.cmake" in said)


def directory_cases(expect: testing.Expect) -> None:
    """Hold each reason of a run path directory that the host does not give (THE RUN PATH DIRECTORIES)."""
    with TemporaryDirectory(prefix="artifacts-directories-") as scratch_text:
        scratch = Path(scratch_text).resolve()
        (scratch / "bin").mkdir()
        (scratch / "file").write_text("", encoding="utf-8")
        parts = (
            str(scratch),
            f"{scratch}/bin/..",
            f"{scratch}/missing",
            f"{scratch}/file",
            "",
            "$ORIGIN",
            "${ORIGIN}/missing",
            "$LIB",
            "$ORIGINAL",
            "relative",
        )
        entries = (
            elf.DynamicEntry(elf.DT_NEEDED, 0, "libc.so.6"),
            elf.DynamicEntry(elf.DT_RUNPATH, 0, ":".join(parts)),
            elf.DynamicEntry(elf.DT_RPATH, 0, ""),
        )
        found = artifacts.missing_directories(entries, scratch / "bin" / "tool")
        expect(
            "a directory of the host and $ORIGIN give nothing, and each other part gives its path and its reason",
            found
            == [
                (f"{scratch}/missing", f"{scratch}/missing", artifacts.NO_PATH_REASON),
                (f"{scratch}/file", f"{scratch}/file", artifacts.FILE_REASON),
                ("", "", artifacts.EMPTY_REASON),
                ("${ORIGIN}/missing", f"{scratch}/bin/missing", artifacts.NO_PATH_REASON),
                ("$LIB", "$LIB", artifacts.TOKEN_REASON),
                ("$ORIGINAL", "$ORIGINAL", artifacts.TOKEN_REASON),
                ("relative", "relative", artifacts.RELATIVE_REASON),
            ],
        )
        item = artifacts.MissingDirectory(
            "{toolchain}/lib64", "/opt/gcc/lib64", artifacts.NO_PATH_REASON, "test/check", 3
        )
        finding = artifacts.missing_finding(item)
        said = "test/check and 2 more name the run path directory '{toolchain}/lib64' (/opt/gcc/lib64), which is not"
        expect(
            "the error names the directory with its word, its path on the host, the artifacts and the reason",
            finding.level == "error" and finding.kind == artifacts.MISSING_KIND and said in finding.message,
        )


def missing_cases(expect: testing.Expect, compiler: str) -> None:
    """Build a planted program with a run path directory that the host does not give.

    Hold that no ledger row admits its error.
    """
    with TemporaryDirectory(prefix="artifacts-missing-") as scratch_text:
        scratch = Path(scratch_text)
        tree = scratch / "tree"
        tree.mkdir()
        (tree / "tool.cpp").write_text(PROGRAM, encoding="utf-8")
        (tree / runner.MANIFEST).write_text(
            f"{FACT_ROW}program examples tool tool.cpp | -Wl,--enable-new-dtags -Wl,-rpath,{{root}}/planted-missing "
            "-Wl,-rpath,{root}\n",
            encoding="utf-8",
        )
        context = runner.ElfContext(tree, None, runner.planted_build(tree, scratch / "build", compiler))
        rule = rule_ledger.rule_of(artifacts.RULE)
        ledger, head = ledger_head(context)
        ledger.write_text(head, encoding="utf-8")
        counts = artifacts.examine(rule, context).counts
        rows = "".join(f"{item.path} | {item.kind} | 1 | {item.entity} | -\n" for item in counts)
        ledger.write_text(f"{head}{rows}", encoding="utf-8")
        inspection = artifacts.examine(rule, context)
        errors = [(item.kind, item.entity) for item in inspection.findings if item.level == "error"]
        expect(
            "a ledger with each fact holds the run path, and the missing directory is still the one error",
            "@build/tools | runpath | 1 | {build}/planted-missing | -" in rows
            and errors == [(artifacts.MISSING_KIND, "{build}/planted-missing")]
            and [(item.kind, item.entity) for item in inspection.unadmitted] == errors,
        )


# ── The properties ─────────────────────────────────────────────────


def problem_cases(expect: testing.Expect) -> None:
    """Hold the problems of the hardening facts of one artifact (THE PROPERTIES, THE CONTROL FLOW)."""
    expect("an executable with each property has no problem", kinds_of(HARDENED) == [])
    fixed = replace(HARDENED, file_type=elf.ET_EXEC, flags_1=elf.DF_1_NOW)
    expect(
        "an executable of the type ET_EXEC is not position independent",
        kinds_of(fixed) == [artifacts.PIE] and "the type ET_EXEC" in details_of(fixed),
    )
    unmarked = replace(HARDENED, flags_1=elf.DF_1_NOW)
    expect(
        "an executable of the type ET_DYN with no bit DF_1_PIE is not position independent",
        kinds_of(unmarked) == [artifacts.PIE] and "no bit DF_1_PIE" in details_of(unmarked),
    )
    expect(
        "a shared library of the type ET_DYN with no bit DF_1_PIE is no problem",
        kinds_of(replace(unmarked, is_executable=False)) == [],
    )
    expect(
        "an artifact with no segment PT_GNU_RELRO has the problem relro",
        kinds_of(replace(HARDENED, has_relro=False)) == [artifacts.RELRO],
    )
    lazy = replace(HARDENED, flags=0, flags_1=elf.DF_1_PIE)
    expect("an artifact with no bind-now mark binds each symbol late", kinds_of(lazy) == [artifacts.BIND_NOW])
    expect(
        "DT_BIND_NOW, the bit DF_BIND_NOW and the bit DF_1_NOW each bind each symbol at the start",
        kinds_of(replace(lazy, has_bind_now_tag=True)) == []
        and kinds_of(replace(lazy, flags=elf.DF_BIND_NOW)) == []
        and kinds_of(replace(lazy, flags_1=elf.DF_1_PIE | elf.DF_1_NOW)) == [],
    )
    no_stack = replace(HARDENED, stack_flags=None)
    executable_stack = replace(HARDENED, stack_flags=6 | elf.PF_X)
    expect(
        "an artifact with no segment PT_GNU_STACK, or with the bit PF_X of it, has the problem stack",
        kinds_of(no_stack) == kinds_of(executable_stack) == [artifacts.STACK]
        and "no segment PT_GNU_STACK" in details_of(no_stack)
        and "the bit PF_X" in details_of(executable_stack),
    )
    no_shadow = replace(HARDENED, features={X86.kind: 0x1})
    expect(
        "an artifact of x86_64 with IBT and no SHSTK has the problem control-flow, which names SHSTK",
        kinds_of(no_shadow) == [artifacts.CONTROL_FLOW]
        and "has no SHSTK" in details_of(no_shadow)
        and "IBT" not in details_of(no_shadow),
    )
    expect(
        "an artifact of x86_64 with no feature property names IBT and SHSTK",
        "has no IBT and SHSTK" in details_of(replace(HARDENED, features={})),
    )
    arm = replace(HARDENED, machine=elf.EM_AARCH64, features={AARCH64.kind: AARCH64.required})
    expect("an artifact of aarch64 with BTI and PAC has no problem", kinds_of(arm) == [])
    expect(
        "an artifact of aarch64 with BTI and no PAC names PAC",
        "has no PAC" in details_of(replace(arm, features={AARCH64.kind: 0x1})),
    )
    expect(
        "the feature property of x86 does not protect an artifact of aarch64",
        kinds_of(replace(arm, features={X86.kind: X86.required})) == [artifacts.CONTROL_FLOW],
    )
    other = replace(HARDENED, machine=40)
    expect(
        "an artifact of a different machine has the problem machine, and no problem control-flow",
        kinds_of(other) == [artifacts.MACHINE],
    )
    bare = artifacts.Hardening(True, elf.ET_EXEC, elf.EM_X86_64, 0, 0, False, False, None, {})
    expect(
        "an artifact with no property has each problem of its machine, in the sequence of PROPERTY_KINDS",
        kinds_of(bare) == list(artifacts.PROPERTY_KINDS[:-1]),
    )
    expect("the kinds limit the problems", kinds_of(bare, frozenset({artifacts.STACK})) == [artifacts.STACK])
    expect("the kinds of the facts read no property", kinds_of(bare, frozenset(artifacts.FACT_KINDS)) == [])


def input_cases(expect: testing.Expect) -> None:
    """Hold the inputs that removed the control-flow protection, from a planted map and its inputs (THE CONTROL FLOW)."""
    with TemporaryDirectory(prefix="artifacts-inputs-") as scratch_text:
        scratch = Path(scratch_text)
        for name, word in (("good.o", 3), ("bare.o", None), ("shadowless.o", 1)):
            (scratch / name).write_bytes(planted_object(word))
        (scratch / "arm.o").write_bytes(planted_object(3, AARCH64.kind))
        members = (
            ("good.o", planted_object(3)),
            ("bad.o", planted_object(None)),
            ("twice.o", planted_object(3)),
            ("twice.o", planted_object(1)),
        )
        (scratch / "libx.a").write_bytes(elf.make_archive(members))
        (scratch / "libdamaged.a").write_bytes(elf.ARCHIVE_MAGIC + b"not a member header")
        reader = artifacts.InputReader(scratch)
        inputs = (
            "good.o",
            "bare.o",
            "shadowless.o",
            "libx.a(good.o)",
            "libx.a(bad.o)",
            "libx.a(twice.o)",
            "libx.a(absent.o)",
            "missing.o",
            str(scratch / "good.o"),
            "libdamaged.a(one.o)",
            "libdamaged.a(two.o)",
        )
        scan = reader.scan(inputs, X86)
        expect(
            "an object or a member with no property, or with no required bit, lacks the bits, in the sequence of the "
            "map, and a relative input gets the build directory",
            scan.lacking
            == tuple(str(scratch / name) for name in ("bare.o", "shadowless.o", "libx.a(bad.o)", "libx.a(twice.o)")),
        )
        unread = dict(scan.unread)
        unread_names = ("libx.a(absent.o)", "missing.o", "libdamaged.a(one.o)", "libdamaged.a(two.o)")
        expect(
            "a member that the archive does not hold, a missing file and a damaged archive cannot be read, with "
            "their reasons",
            list(unread) == [str(scratch / name) for name in unread_names]
            and "has no member absent.o" in unread[str(scratch / "libx.a(absent.o)")],
        )
        expect(
            "the reader reads each archive one time, also an archive that cannot be read",
            sorted(path.name for path, _ in reader.archives) == ["libdamaged.a", "libx.a"],
        )
        expect(
            "the feature property of x86 does not protect an object of aarch64, and the property of aarch64 does",
            reader.scan(("arm.o", "good.o"), AARCH64).lacking == (str(scratch / "good.o"),),
        )
        program = scratch / "program"
        words = ((str(scratch), "{build}"),)
        program.with_name("program.map").write_text(planted_map(inputs[:3] + inputs[6:7]), encoding="utf-8")
        cause = artifacts.control_flow_cause(program, X86, words, reader)
        expect(
            "the cause names each input with no bit with the words of the paths, and the inputs that cannot be read",
            "these inputs do not have them: {build}/bare.o, {build}/shadowless.o." in cause
            and "cannot read 1 inputs of the map, such as {build}/libx.a(absent.o): " in cause,
        )
        program.with_name("program.map").write_text(planted_map(inputs[:1]), encoding="utf-8")
        cause = artifacts.control_flow_cause(program, X86, words, reader)
        expect(
            "a map whose inputs have the bits tells that an object with no section in the output removed them",
            "Each input of the map has the bits" in cause,
        )
        program.with_name("program.map").write_text("Archive member included to satisfy reference\n", encoding="utf-8")
        cause = artifacts.control_flow_cause(program, X86, words, reader)
        expect(
            "a map of GNU ld cannot be read, and the cause tells that the tree links with mold",
            "cannot be read" in cause and "links with mold" in cause,
        )
    words = (("/opt/gcc", "{toolchain}"),)
    many = [f"/opt/gcc/lib/x{index}.o" for index in range(artifacts.SHOWN_INPUTS + 2)]
    shown = artifacts.shown_inputs(many, words)
    expect(
        "an error names at most SHOWN_INPUTS inputs with the words of the paths, and counts the others",
        shown.startswith("{toolchain}/lib/x0.o, ") and shown.endswith(" and 2 more") and "/opt/gcc" not in shown,
    )


def kind_cases(expect: testing.Expect) -> None:
    """Hold the kinds of a row `kinds` of a plant (THE PLANTS)."""

    def planted(text: str) -> runner.ElfContext:
        """Return a context of a plant with the rows of a text."""
        rows = rows_grammar.parse(text, "plant.txt")
        return runner.ElfContext(Path("/tree"), None, runner.PlantedBuild(rows=rows))

    each = frozenset(artifacts.KINDS)
    expect("a context with no plant reads each kind", artifacts.kinds_of(runner.ElfContext(Path("/tree"))) == each)
    expect("a plant with no row kinds reads each kind", artifacts.kinds_of(planted("kind a\n")) == each)
    expect(
        "a row kinds limits the facts and the properties",
        artifacts.kinds_of(planted("kinds pie needed\n")) == {"pie", "needed"},
    )
    said = raised(lambda: artifacts.kinds_of(planted("kinds pie colour\n")), runner.BuildError) or ""
    expect("a row kinds with a different kind is refused", "colour" in said)
    said = raised(lambda: artifacts.kinds_of(planted(f"kinds {artifacts.MISSING_KIND}\n")), runner.BuildError) or ""
    expect("a row kinds cannot name the run path directories, which the rule reads each time", "names missing" in said)


def build_cases(expect: testing.Expect, compiler: str, scratch: Path) -> None:
    """Hold the read of the linked artifacts of planted builds (THE ARTIFACTS, THE LEDGER, NOT APPLICABLE)."""

    def built(name: str, manifest: str) -> runner.ElfContext:
        """Build a plant with one unit and the rows of a manifest, and return its context."""
        tree = scratch / name / "tree"
        tree.mkdir(parents=True)
        (tree / "main.cpp").write_text(PROGRAM, encoding="utf-8")
        (tree / runner.MANIFEST).write_text(manifest, encoding="utf-8")
        return runner.ElfContext(tree, None, runner.planted_build(tree, scratch / name / "build", compiler))

    rule = rule_ledger.rule_of(artifacts.RULE)
    hardened = built(
        "hardened",
        f"{PROPERTY_ROW}flags -std=c++17 -O1 -fPIE {runner.CFI_WORD}\nprogram examples tool main.cpp | {HARDENED_LINK}\n"
        f"module test libplanted.so main.cpp | -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack\n",
    )
    reading = artifacts.read_build(hardened, scratch / "hardened" / "build")
    expect(
        "a hardened program and a hardened module give no error, and the reading counts each of them",
        errors_of(artifacts.examine(rule, hardened).findings) == []
        and (reading.executables, sum(reading.counts.values())) == (1, 2),
    )
    plain = built("plain", f"{PROPERTY_ROW}program examples tool main.cpp | -no-pie -Wl,-z,lazy\n")
    found = artifacts.judge(rule, plain)
    expect(
        "a program with no flag of the hardening has the problems pie, bind-now and control-flow",
        {artifacts.PIE, artifacts.BIND_NOW, artifacts.CONTROL_FLOW} <= set(errors_of(found)),
    )
    control_flow = [item.message for item in found if item.kind == artifacts.CONTROL_FLOW]
    expect(
        "the error of the control flow names the object of the unit, from the map of the link",
        len(control_flow) == 1 and "main.cpp.o" in control_flow[0],
    )
    together = built("together", "program examples tool main.cpp | -no-pie -Wl,-z,lazy\n")
    inspection = artifacts.examine(rule, together)
    properties = {item.kind for item in inspection.findings if item.kind in artifacts.PROPERTY_KINDS}
    expect(
        "a build with no row kinds gives its facts and its problems, only a fact has a count, and no row admits a "
        "problem",
        {artifacts.NEEDED, artifacts.PIE, artifacts.BIND_NOW} <= set(errors_of(inspection.findings))
        and {key.kind for key in inspection.counts} <= set(artifacts.FACT_KINDS)
        and {item.kind for item in inspection.unadmitted} == properties,
    )
    request = runner.WriteRequest(together.root, Path(runner.PLANTED), rule_ledger.WriteMode.RAISE, "a planted reason")
    status = one_set.write_inspection(rule, request, lambda _context: inspection, artifacts.WRITE_POLICY)
    expect(
        "a write with --raise refuses a build with a missing property, and writes no ledger",
        status == 1 and not rule_ledger.ledger_path(artifacts.RULE, together.root).exists(),
    )
    ledger, head = ledger_head(together)
    ledger.write_text(f"{head}@build/tools | {artifacts.PIE} | 1 | examples/tool | -\n", encoding="utf-8")
    shaped = artifacts.row_problems(rule_ledger.read(rule, together.root))
    expect(
        "a ledger row of a property does not have the shape of a row, so no row admits a problem",
        [item.message for item in shaped] == [artifacts.ROW_SHAPE],
    )
    program = plain.linked_artifacts()[0].path
    program.with_name(program.name + artifacts.MAP_SUFFIX).unlink()
    cause = artifacts.control_flow_cause(program, X86, (), artifacts.InputReader(scratch))
    expect("a map that cannot be read gives words that say so", "cannot be read" in cause)
    twice = built(
        "twice", f"{PROPERTY_ROW}program examples one main.cpp | -no-pie\nprogram examples two main.cpp | -no-pie\n"
    )
    pie_errors = [item for item in artifacts.findings(twice) if item.kind == artifacts.PIE]
    expect(
        "two artifacts with one problem give one error, which names one of them and the number of the others",
        len(pie_errors) == 1 and "and 1 more artifacts: each" in pie_errors[0].message,
    )
    missing = runner.LinkedArtifact("examples", scratch / "missing", runner.EXECUTABLE)
    unmade = runner.ElfContext(scratch, None, runner.PlantedBuild(linked=(missing,)))
    said = raised(lambda: artifacts.examine(rule, unmade), runner.NotApplicable) or ""
    expect("a build that has not made each artifact does not apply", "has not made 1 artifacts" in said)
    empty = runner.ElfContext(scratch, None, runner.PlantedBuild())
    said = raised(lambda: artifacts.examine(rule, empty), runner.NotApplicable) or ""
    expect("a build with no linked artifact does not apply", "no linked artifact" in said)


def cases(expect: testing.Expect) -> None:
    """Run the cases with planted facts, and the compile cases on planted builds."""
    path_cases(expect)
    directory_cases(expect)
    problem_cases(expect)
    input_cases(expect)
    kind_cases(expect)
    compiler = runner.plant_compiler()
    expect("ASSAY_TEST_CXX or CRUCIBLE_GCC16_PREFIX names the patched compiler", compiler is not None)
    if compiler is None:
        return
    fact_cases(expect, compiler)
    missing_cases(expect, compiler)
    with TemporaryDirectory(prefix="artifacts-builds-") as scratch_text:
        build_cases(expect, compiler, Path(scratch_text))


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (cases,))
