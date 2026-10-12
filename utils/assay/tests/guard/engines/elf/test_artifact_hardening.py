"""The test of the rule artifact-hardening (assay.guard.engines.elf.artifact_hardening).

THE CASES
    The cases hold these parts of the rule:
      * The problems of the facts of one artifact, for each kind and each
        machine, with planted facts
      * The inputs of a link that remove the control-flow protection, from
        planted merges of a map
      * The kinds of a row `properties` of a plant
      * The read of linked artifacts of planted builds, the groups of the
        problems, and the builds that the rule does not apply to.
    The compile cases use the compiler that ASSAY_TEST_CXX names.  The
    registration of the test gives the compiler of the build.
"""

from collections.abc import Iterable
from dataclasses import replace
from pathlib import Path
from tempfile import TemporaryDirectory

from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core import testing
from assay.guard.engines.elf import artifact_hardening, runner
from assay.readers import elf, linkmap
from assay.tests.guard.engines.elf.test_runner import raised

X86 = artifact_hardening.PROTECTIONS[elf.EM_X86_64]
AARCH64 = artifact_hardening.PROTECTIONS[elf.EM_AARCH64]
ALL = frozenset(artifact_hardening.KINDS)
# The facts of an executable of x86_64 with each property.
HARDENED = artifact_hardening.Facts(
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


def kinds_of(facts: artifact_hardening.Facts, kinds: frozenset[str] = ALL) -> list[str]:
    """Return the kind of each problem of the facts."""
    return [problem.kind for problem in artifact_hardening.problems_of(facts, kinds)]


def details_of(facts: artifact_hardening.Facts) -> str:
    """Return the words of each problem of the facts, in one text."""
    return " | ".join(problem.detail for problem in artifact_hardening.problems_of(facts, ALL))


def errors_of(findings: Iterable[check_report.Finding]) -> list[str]:
    """Return the kind of each error."""
    return [item.kind for item in findings if item.level == "error"]


def planted_rule() -> rule_registry.Rule:
    """Return the rule artifact-hardening of a planted registry."""
    row = f"rule {artifact_hardening.RULE} engine=elf scope=builddir ledger=none doc=V | a planted property | add it"
    return rule_registry.planted_rules((row,))[artifact_hardening.RULE]


# ── The cases ──────────────────────────────────────────────────────


def problem_cases(expect: testing.Expect) -> None:
    """Hold the problems of the facts of one artifact (THE PROPERTIES, THE CONTROL FLOW)."""
    expect("an executable with each property has no problem", kinds_of(HARDENED) == [])
    fixed = replace(HARDENED, file_type=elf.ET_EXEC, flags_1=elf.DF_1_NOW)
    expect(
        "an executable of the type ET_EXEC is not position independent",
        kinds_of(fixed) == [artifact_hardening.PIE] and "the type ET_EXEC" in details_of(fixed),
    )
    unmarked = replace(HARDENED, flags_1=elf.DF_1_NOW)
    expect(
        "an executable of the type ET_DYN with no bit DF_1_PIE is not position independent",
        kinds_of(unmarked) == [artifact_hardening.PIE] and "no bit DF_1_PIE" in details_of(unmarked),
    )
    expect(
        "a shared library of the type ET_DYN with no bit DF_1_PIE is no problem",
        kinds_of(replace(unmarked, is_executable=False)) == [],
    )
    expect(
        "an artifact with no segment PT_GNU_RELRO has the problem relro",
        kinds_of(replace(HARDENED, has_relro=False)) == [artifact_hardening.RELRO],
    )
    lazy = replace(HARDENED, flags=0, flags_1=elf.DF_1_PIE)
    expect("an artifact with no bind-now mark binds each symbol late", kinds_of(lazy) == [artifact_hardening.BIND_NOW])
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
        kinds_of(no_stack) == kinds_of(executable_stack) == [artifact_hardening.STACK]
        and "no segment PT_GNU_STACK" in details_of(no_stack)
        and "the bit PF_X" in details_of(executable_stack),
    )
    no_shadow = replace(HARDENED, features={X86.kind: 0x1})
    expect(
        "an artifact of x86_64 with IBT and no SHSTK has the problem control-flow, which names SHSTK",
        kinds_of(no_shadow) == [artifact_hardening.CONTROL_FLOW]
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
        kinds_of(replace(arm, features={X86.kind: X86.required})) == [artifact_hardening.CONTROL_FLOW],
    )
    other = replace(HARDENED, machine=40)
    expect(
        "an artifact of a different machine has the problem machine, and no problem control-flow",
        kinds_of(other) == [artifact_hardening.MACHINE],
    )
    bare = artifact_hardening.Facts(True, elf.ET_EXEC, elf.EM_X86_64, 0, 0, False, False, None, {})
    expect(
        "an artifact with no property has each problem of its machine, in the sequence of KINDS",
        kinds_of(bare) == list(artifact_hardening.KINDS[:-1]),
    )
    expect(
        "the kinds limit the problems",
        kinds_of(bare, frozenset({artifact_hardening.STACK})) == [artifact_hardening.STACK],
    )


def input_cases(expect: testing.Expect) -> None:
    """Hold the inputs that removed the control-flow protection, from the merges of a map (THE CONTROL FLOW)."""

    def merge(first: str, first_value: int | None, second: str, second_value: int | None) -> linkmap.PropertyMerge:
        """Return a planted merge of the feature property of x86."""
        return linkmap.PropertyMerge("Removed", X86.kind, None, first, first_value, second, second_value)

    merges = [
        merge("crt1.o", 3, "unit.o", None),
        linkmap.PropertyMerge("Removed", 0xC0008002, None, "crt1.o", 1, "other.o", None),
        merge("crt1.o", None, "libx.a(member.o)", 3),
        merge("crt1.o", None, "unit.o", None),
        linkmap.PropertyMerge("Updated", X86.kind, 1, "crt1.o", 3, "part.o", 1),
    ]
    expect(
        "each second file with no property or with no required bit comes one time, and a different type adds none",
        artifact_hardening.lacking_inputs(merges, X86) == ("unit.o", "part.o"),
    )
    expect(
        "the first file of the first merge comes when it has no required bit",
        artifact_hardening.lacking_inputs([merge("start.o", 1, "unit.o", 3)], X86) == ("start.o",)
        and artifact_hardening.lacking_inputs([merge("start.o", None, "unit.o", 3)], X86) == ("start.o",),
    )
    expect("no merge gives no input", artifact_hardening.lacking_inputs([], X86) == ())
    words = (("/opt/gcc", "{toolchain}"),)
    many = [f"/opt/gcc/lib/x{index}.o" for index in range(artifact_hardening.SHOWN_INPUTS + 2)]
    shown = artifact_hardening.shown_inputs(many, words)
    expect(
        "an error names at most SHOWN_INPUTS inputs with the words of the paths, and counts the others",
        shown.startswith("{toolchain}/lib/x0.o, ") and shown.endswith(" and 2 more") and "/opt/gcc" not in shown,
    )


def kind_cases(expect: testing.Expect) -> None:
    """Hold the kinds of a row `properties` of a plant (THE PLANTS)."""

    def planted(text: str) -> runner.ElfContext:
        """Return a context of a plant with the rows of a text."""
        rows = rows_grammar.parse(text, "plant.txt")
        return runner.ElfContext(Path("/tree"), None, runner.PlantedBuild(rows=rows))

    bare = runner.ElfContext(Path("/tree"))
    expect("a context with no plant reads each kind", artifact_hardening.kinds_of(bare) == ALL)
    expect("a plant with no row properties reads each kind", artifact_hardening.kinds_of(planted("kind a\n")) == ALL)
    expect(
        "a row properties limits the kinds",
        artifact_hardening.kinds_of(planted("properties pie stack\n")) == {"pie", "stack"},
    )
    said = raised(lambda: artifact_hardening.kinds_of(planted("properties pie colour\n")), runner.BuildError) or ""
    expect("a row properties with a different kind is refused", "colour" in said)


def build_cases(expect: testing.Expect, compiler: str, scratch: Path) -> None:
    """Hold the read of the linked artifacts of planted builds (THE ARTIFACTS, NOT APPLICABLE)."""

    def built(name: str, manifest: str) -> runner.ElfContext:
        """Build a plant with one unit and the rows of a manifest, and return its context."""
        tree = scratch / name / "tree"
        tree.mkdir(parents=True)
        (tree / "main.cpp").write_text(PROGRAM, encoding="utf-8")
        (tree / runner.MANIFEST).write_text(manifest, encoding="utf-8")
        return runner.ElfContext(tree, None, runner.planted_build(tree, scratch / name / "build", compiler))

    hardened = built(
        "hardened",
        f"flags -std=c++17 -O1 -fPIE {runner.CFI_WORD}\nprogram examples tool main.cpp | {HARDENED_LINK}\n"
        f"module test libplanted.so main.cpp | -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack\n",
    )
    found, reading = artifact_hardening.examine(hardened)
    expect(
        "a hardened program and a hardened module give no error, and the reading counts each of them",
        errors_of(found) == [] and (reading.executables, reading.libraries) == (1, 1),
    )
    plain = built("plain", "program examples tool main.cpp | -no-pie -Wl,-z,lazy\n")
    found = artifact_hardening.judge(planted_rule(), plain)
    expect(
        "a program with no flag of the hardening has the problems pie, bind-now and control-flow",
        {artifact_hardening.PIE, artifact_hardening.BIND_NOW, artifact_hardening.CONTROL_FLOW} <= set(errors_of(found)),
    )
    control_flow = [item.message for item in found if item.kind == artifact_hardening.CONTROL_FLOW]
    expect(
        "the error of the control flow names the object of the unit, from the map of the link",
        len(control_flow) == 1 and "main.cpp.o" in control_flow[0],
    )
    program = plain.linked_artifacts()[0].path
    program.with_name(program.name + ".map").unlink()
    cause = artifact_hardening.control_flow_cause(program, X86, ())
    expect("a map that cannot be read gives words that say so", "cannot be read" in cause)
    twice = built("twice", "program examples one main.cpp | -no-pie\nprogram examples two main.cpp | -no-pie\n")
    pie_errors = [item for item in artifact_hardening.findings(twice) if item.kind == artifact_hardening.PIE]
    expect(
        "two artifacts with one problem give one error, which names one of them and the number of the others",
        len(pie_errors) == 1 and "and 1 more artifacts: each" in pie_errors[0].message,
    )
    missing = runner.LinkedArtifact("examples", scratch / "missing", runner.EXECUTABLE)
    unmade = runner.ElfContext(scratch, None, runner.PlantedBuild(linked=(missing,)))
    said = raised(lambda: artifact_hardening.examine(unmade), runner.NotApplicable) or ""
    expect("a build that has not made each artifact does not apply", "has not made 1 artifacts" in said)
    empty = runner.ElfContext(scratch, None, runner.PlantedBuild())
    said = raised(lambda: artifact_hardening.examine(empty), runner.NotApplicable) or ""
    expect("a build with no linked artifact does not apply", "no linked artifact" in said)


def cases(expect: testing.Expect) -> None:
    """Run the cases with planted facts, and the compile cases on planted builds."""
    problem_cases(expect)
    input_cases(expect)
    kind_cases(expect)
    compiler = runner.plant_compiler()
    expect("ASSAY_TEST_CXX or CRUCIBLE_GCC16_PREFIX names the patched compiler", compiler is not None)
    if compiler is None:
        return
    with TemporaryDirectory(prefix="artifact-hardening-") as scratch_text:
        build_cases(expect, compiler, Path(scratch_text))


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (cases,))
