"""The test of the rule artifact-links (assay.guard.engines.elf.artifact_links).

`guard selftest tests.guard.engines.elf.test_artifact_links` runs the test.
Each case writes one line.  Exit 0 when each case holds, else 1.

THE CASES
    The cases hold the groups and the paths of the rule, and the facts of
    planted programs.  They also hold a link with no map, and the verdict of
    a planted ledger.  The compile cases use the compiler that ASSAY_TEST_CXX names, and the
    registration of the test in test/CMakeLists.txt gives the compiler of the
    build.
"""

from pathlib import Path
from tempfile import TemporaryDirectory

from assay.core import ledger as rule_ledger
from assay.core import testing
from assay.guard.engines.elf import artifact_links, runner

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
MANIFEST = "program examples tool tool.cpp\nprogram vessel vessel_tool vessel.cpp\nprogram test check tool.cpp\n"


def path_cases(expect: testing.Expect) -> None:
    """Hold the group of a source directory, the prefix of a compiler and the words of a run path (THE PATHS)."""
    expect(
        "the first directory of a source directory gives the group, and each other directory is tools",
        [artifact_links.group_of(name) for name in ("vessel", "bench", "test", "examples", "")]
        == ["vessel", "benches", "tests", "tools", "tools"],
    )
    expect(
        "the prefix of a compiler is the directory above usr/bin, or above bin",
        artifact_links.toolchain_prefix("/opt/gcc/usr/bin/g++") == Path("/opt/gcc")
        and artifact_links.toolchain_prefix("/opt/gcc/bin/g++") == Path("/opt/gcc"),
    )
    words = artifact_links.path_words("/opt/gcc/usr/bin/g++", Path("/work/build"))
    expect(
        "a run path gets the word of the toolchain and of the build directory, and a different path stays",
        [artifact_links.shown_path(item, words) for item in ("/opt/gcc/usr/lib64", "/work/build/test", "/usr/lib")]
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


def fact_cases(expect: testing.Expect) -> None:
    """Build planted programs, and hold their facts, a link with no map and the verdict of a ledger."""
    compiler = runner.plant_compiler()
    expect("ASSAY_TEST_CXX or CRUCIBLE_GCC16_PREFIX names the patched compiler", compiler is not None)
    if compiler is None:
        return
    with TemporaryDirectory(prefix="artifact-links-") as scratch_text:
        scratch = Path(scratch_text)
        context = planted_context(scratch, compiler)
        facts, counts = artifact_links.read_facts(context, scratch / "build")
        found = {(fact.group, fact.kind, fact.entity) for fact in facts}
        expect("each program counts in its group", counts == {"tests": 1, "tools": 1, "vessel": 1})
        expect(
            "a program gives its needed libraries, its file type and its interpreter",
            {("tools", "needed", "libc.so.6"), ("tools", "elf-type", "executable")} <= found
            and any(group == "tools" and kind == "interpreter" for group, kind, _ in found),
        )
        expect(
            "the map gives the member of libc_nonshared.a that the link takes for atexit",
            ("tools", "static-member", "libc_nonshared.a(atexit.oS)") in found,
        )
        expect(
            "the init array gives the constructor and frame_dummy",
            {("tools", "init", "_ZL12planted_initv"), ("tools", "init", "frame_dummy")} <= found,
        )
        expect(
            "a program gives its needed versions, its sections and the texts of .comment",
            any(kind == "version" and entity.startswith("libc.so.6 GLIBC_") for _, kind, entity in found)
            and ("tools", "section", ".text") in found
            and any(kind == "comment" and entity.startswith("GCC:") for _, kind, entity in found),
        )
        expect(
            "only the group vessel gives its imports, with their versions",
            any(
                group == "vessel" and kind == "import" and entity.startswith("printf@GLIBC_")
                for group, kind, entity in found
            )
            and not any(kind == "import" and group != "vessel" for group, kind, _ in found),
        )
        rule = rule_ledger.rule_of(artifact_links.RULE)
        ledger = rule_ledger.ledger_path(artifact_links.RULE, context.root)
        ledger.parent.mkdir(parents=True)
        head = f"format {rule_ledger.FORMAT}\nrule {artifact_links.RULE}\nconfiguration planted kind planted\n"
        ledger.write_text(f"{head}configuration planted tier planted\n", encoding="utf-8")
        inspection = artifact_links.examine(rule, context)
        expect(
            "each fact that the ledger does not hold is an error",
            len([item for item in inspection.findings if item.level == "error"]) == len(facts),
        )
        rows = "".join(f"{item.path} | {item.kind} | 1 | {item.entity} | -\n" for item in inspection.counts)
        ledger.write_text(f"{head}configuration planted tier planted\n{rows}", encoding="utf-8")
        judged = artifact_links.examine(rule, context).findings
        expect("a ledger with each fact gives no error", [item for item in judged if item.level == "error"] == [])
        program = next(path for directory, path in context.linked_artifacts() if directory == "examples")
        program.with_name(program.name + artifact_links.MAP_SUFFIX).unlink()
        try:
            artifact_links.map_facts(program, scratch / "build")
            said = ""
        except runner.BuildError as problem:
            said = str(problem)
        expect("a link with no map is an error that names the file of cmake", "cmake/LinkMap.cmake" in said)


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (path_cases, fact_cases))
