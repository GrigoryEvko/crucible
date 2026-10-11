"""The test of the command guard asm-compare (assay.guard.asm_compare), on planted units.

`guard selftest tests.guard.test_asm_compare` runs the test.  The compile
cases compile two planted units with the patched compiler of
assay.core.testing.patched_compiler(), and ASSAY_TEST_CXX of the
registration gives it.  The cases hold:
  * The name of a function with no qualifier and no parameter list
  * The difference of two objects, for a function with one more call on the
    side after, and for a function with no change
  * The resolve of a NAME of a member function, and of a name that agrees
    with no function
  * The resolve of a NAME of an overload, with and with no parameter list
  * The exit of the command, with and with no `--require-same`
  * The sides of one unit in two planted builds, and at a revision of a
    planted repository and in its tree
  * The usage errors of main.
Exit 0 when each case holds, else 1.
"""

import functools
import tempfile
from collections.abc import Callable
from pathlib import Path

from assay.core import git, proc, testing
from assay.core.exit import ExitCode
from assay.guard import asm_compare
from assay.guard.engines.elf import codegen_shapes as shapes
from assay.readers import cmake, compdb

# The unit of the side before.
BEFORE = """\
extern "C" void note(int value);
extern "C" int pick(int value) { return value + 1; }
extern "C" int keep(int value) { return value * 3; }
struct Box { int value; int get() const; };
int Box::get() const { return value; }
int twice(int value) { return value * 2; }
long twice(long value) { return value * 2; }
"""
# The unit of the side after: pick calls note on one path.
AFTER = BEFORE.replace(
    'extern "C" int pick(int value) { return value + 1; }',
    'extern "C" int pick(int value) { if (value > 7) note(value); return value + 1; }',
)
FLAGS = ("-std=c++17", "-O2")


def compiled(compiler: str, source: Path, obj: Path) -> bool:
    """Compile one planted unit into an object, and return True when the compile succeeds."""
    command = proc.Command([compiler, *FLAGS, "-c", str(source), "-o", str(obj)], timeout_s=proc.QUICK_S)
    return proc.run(command).exit == 0


def name_cases(expect: testing.Expect) -> None:
    """Hold the name of a function with no qualifier and no parameter list."""
    expect(
        "a const member loses its qualifier and its list", asm_compare.plain_name("a::B::get() const") == "a::B::get"
    )
    expect("a plain function loses its list", asm_compare.plain_name("pick(int)") == "pick")
    expect("a name with no list stays", asm_compare.plain_name("pick") == "pick")
    expect("two qualifiers go", asm_compare.plain_name("a::B::f() const &&") == "a::B::f")


def object_cases(expect: testing.Expect) -> None:
    """Hold the difference, the resolve and the exit on the objects of two planted units."""
    compiler = testing.held_compiler(expect)
    if compiler is None:
        return
    with tempfile.TemporaryDirectory(prefix="asm-compare-test-") as scratch_text:
        scratch = Path(scratch_text)
        (scratch / "before.cpp").write_text(BEFORE, encoding="utf-8")
        (scratch / "after.cpp").write_text(AFTER, encoding="utf-8")
        objects = (scratch / "before.o", scratch / "after.o")
        is_compiled = [
            compiled(compiler, scratch / f"{side}.cpp", obj) for side, obj in zip(("before", "after"), objects)
        ]
        expect("the two planted units compile", all(is_compiled))
        before, after = asm_compare.object_side(objects[0]), asm_compare.object_side(objects[1])
        picked = asm_compare.difference("pick", before, after)
        expect(
            "the function with one more call has a call and a branch only on the side after",
            any(step.kind == shapes.StepClass.CALL for step in picked.only_after)
            and any(step.kind == shapes.StepClass.BRANCH for step in picked.only_after)
            and not picked.is_same,
        )
        kept = asm_compare.difference("keep", before, after)
        expect("the function with no change is the same on the two sides", kept.is_same)
        expect(
            "a NAME of a const member agrees with the member",
            asm_compare.difference("Box::get", before, after).after_count > 0,
        )
        refused = {
            name: problem_of(functools.partial(asm_compare.difference, name, before, after))
            for name in ("twice", "pikc")
        }
        expect("a NAME of two overloads is an error that names each", "twice(int)" in refused["twice"])
        expect("a NAME of no function is an error that gives the nearest names", "pick" in refused["pikc"])
        expect(
            "a NAME with a parameter list selects one overload",
            asm_compare.difference("twice(long)", before, after).is_same,
        )
        same = asm_compare.Request(("keep",), objects=objects, require_same=True)
        changed = asm_compare.Request(("keep", "pick"), objects=objects, require_same=True)
        printed = asm_compare.Request(("pick",), objects=objects)
        statuses = [testing.captured(functools.partial(asm_compare.compare, item)) for item in (same, changed, printed)]
        expect("--require-same with the same code exits 0", statuses[0][0] == ExitCode.OK)
        expect("--require-same with a changed function exits 1", statuses[1][0] == ExitCode.FINDINGS)
        expect(
            "with no --require-same the command exits 0 and prints the instructions of one side",
            statuses[2][0] == ExitCode.OK and "+ call note" in statuses[2][1],
        )
        missing = asm_compare.Request(("pick",), objects=(scratch / "absent.o", objects[1]))
        expect(
            "a missing object is a missing input",
            testing.captured(functools.partial(asm_compare.compare, missing))[0] == ExitCode.MISSING_INPUT,
        )


def problem_of(action: Callable[[], object]) -> str:
    """Return the message of the SideError that the action raises, or an empty text when it raises none."""
    try:
        action()
    except asm_compare.SideError as problem:
        return str(problem)
    return ""


def planted_build(build: Path, root: Path, compiler: str) -> None:
    """Write a planted reply and a planted compile database of the unit unit.cpp of root into a build directory."""
    build.mkdir(parents=True, exist_ok=True)
    cmake.plant(build, source=str(root), build=str(build))
    unit, obj = root / "unit.cpp", build / "unit.o"
    command = " ".join([compiler, *FLAGS, asm_compare.PLUGIN_DEFINE, "-c", str(unit), "-o", str(obj)])
    compdb.plant(build, [{"directory": str(build), "file": str(unit), "output": str(obj), "command": command}])


def build_cases(expect: testing.Expect) -> None:
    """Hold the sides of one unit in two planted builds, and at a revision of a planted repository and in its tree."""
    compiler = testing.held_compiler(expect)
    if compiler is None:
        return
    with tempfile.TemporaryDirectory(prefix="asm-compare-test-") as scratch_text:
        scratch = Path(scratch_text)
        for name, text in (("old", BEFORE), ("new", AFTER)):
            (scratch / name).mkdir()
            (scratch / name / "unit.cpp").write_text(text, encoding="utf-8")
            planted_build(scratch / f"build-{name}", scratch / name, compiler)
        before, after = asm_compare.build_sides([scratch / "build-old", scratch / "build-new"], Path("unit.cpp"), False)
        expect(
            "the unit of two builds gives the difference of its two sources",
            not asm_compare.difference("pick", before, after).is_same
            and asm_compare.difference("keep", before, after).is_same,
        )
    with git.scratch_repository({"unit.cpp": BEFORE.encode()}) as root:
        git.commit_all(root, "the planted unit")
        (root / "unit.cpp").write_text(AFTER, encoding="utf-8")
        with tempfile.TemporaryDirectory(prefix="asm-compare-test-") as build_text:
            planted_build(Path(build_text), root, compiler)
            before, after = asm_compare.base_sides(Path(build_text), Path("unit.cpp"), "HEAD", False)
        expect(
            "the unit at HEAD and in the tree gives the difference of the edit",
            not asm_compare.difference("pick", before, after).is_same
            and asm_compare.difference("keep", before, after).is_same,
        )
        expect(
            "a revision that names no commit is an error",
            "no commit" in problem_of(functools.partial(asm_compare.exported, root, "no-such-revision", root)),
        )


# The command lines that main refuses as usage errors.
USAGE_WORDS = (
    ("--objects", "a.o", "b.o"),
    ("--function", "pick", "--builds", "a", "b"),
    ("--function", "pick", "--base", "HEAD", "--source", "unit.cpp"),
    ("--function", "pick", "--objects", "a.o", "b.o", "--base", "HEAD"),
)


def usage_cases(expect: testing.Expect) -> None:
    """Hold the usage errors of main (USAGE_WORDS)."""
    statuses = [testing.captured(functools.partial(asm_compare.main, list(words)))[0] for words in USAGE_WORDS]
    expect(
        "main refuses no NAME, --builds with no FILE, --base with no DIR and two modes",
        statuses == [ExitCode.USAGE] * len(USAGE_WORDS),
    )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (name_cases, object_cases, build_cases, usage_cases))
