"""The test of the rule static-tls (assay.guard.engines.elf.static_tls).

THE CASES
    The cases hold these parts of the rule:
      * The base thread-local variables of one member
      * The read of the members of the libraries, and its errors
      * The judge of the members against the thresholds and the ledger rows
      * The write of the ledger on a planted build directory.
    The compile cases use the compiler that ASSAY_TEST_CXX names.  The
    registration of the test gives the compiler of the build.
"""

from collections.abc import Iterable
from pathlib import Path
from tempfile import TemporaryDirectory

from assay.core import ledger as rule_ledger
from assay.core import proc, testing
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.guard.engines.elf import runner, static_tls
from assay.readers import cmake, elf
from assay.tests.guard.engines.elf.test_runner import quiet

RULE = static_tls.RULE
KIND = "planted-kind"
# The units of the cases.  They hold a buffer of an unnamed namespace, a buffer in the namespace foundation and a
# buffer in the namespace crucible.  They also hold a counter, a member with two variables and a plain function.
UNITS = {
    "buffer": "namespace {\nthread_local char scratch[1024];\n}\nchar* scratch_entry() { return scratch; }\n",
    "header": "namespace foundation {\ninline thread_local char buffer[512];\n}\n"
    "char* buffer_entry() { return foundation::buffer; }\n",
    "cache": "namespace crucible {\nthread_local char cache[4096];\n}\nchar* cache_entry() { return crucible::cache; }\n",
    "counter": "thread_local int counter = 0;\nint counter_entry() { return ++counter; }\n",
    "pair": "thread_local long first = 0;\nthread_local char second[16];\n"
    "long pair_entry() { return first + second[0]; }\n",
    "plain": "int plain_entry(int value) { return value + 1; }\n",
}


def planted_rule(warn: int = 64, error: int = 256) -> rule_registry.Rule:
    """Return the rule static-tls of a planted registry with the thresholds."""
    row = (
        f"rule {RULE} engine=elf scope=builddir ledger=max warn={warn} error={error} unit=bytes doc=XV | a planted "
        f"quantity | lower it"
    )
    return rule_registry.planted_rules((row,))[RULE]


def errors_of(findings: Iterable[check_report.Finding]) -> list[str]:
    """Return the message of each error."""
    return [item.message for item in findings if item.level == "error"]


def warnings_of(findings: Iterable[check_report.Finding]) -> list[str]:
    """Return the message of each warning."""
    return [item.message for item in findings if item.level == "warning"]


class Scratch:
    """A planted tree with objects of the units, and archives of the libraries foundation and crucible."""

    def __init__(self, root: Path, compiler: str) -> None:
        """Compile each unit with the flags of the plants of the engine."""
        self.root = root
        self.build = root / "build"
        self.objects = root / "objects"
        self.build.mkdir(parents=True)
        self.objects.mkdir()
        self.compiler = compiler
        for unit, source in UNITS.items():
            (self.objects / f"{unit}.cpp").write_text(source, encoding="utf-8")
            command = [compiler, *runner.FLAGS, "-c", str(self.objects / f"{unit}.cpp"), "-o", str(self.object(unit))]
            proc.require(proc.run(proc.Command(command, timeout_s=proc.TOOL_S)))
        self.libraries: dict[str, list[str]] = {}

    def object(self, unit: str) -> Path:
        """Return the object of a unit.  Its name in an archive is UNIT.cpp.o."""
        return self.objects / f"{unit}.cpp.o"

    def archive(self, name: str, units: list[str]) -> None:
        """Make the archive libNAME.a of the units again."""
        path = self.build / f"lib{name}.a"
        path.unlink(missing_ok=True)
        objects = [str(self.object(unit)) for unit in units]
        proc.require(proc.run(proc.Command(["ar", "rcs", str(path), *objects], timeout_s=proc.QUICK_S)))
        self.libraries[name] = units

    def planted(self) -> runner.ElfContext:
        """Return a context of a planted build with the archives of the libraries."""
        archives = {name: self.build / f"lib{name}.a" for name in self.libraries}
        return runner.ElfContext(self.root, None, runner.PlantedBuild(kind=KIND, libraries=archives))

    def directory(self) -> None:
        """Write the build kind and the reply of the file API of a build directory with the libraries."""
        (self.build / "build-kind.txt").write_text(f"{KIND}\n", encoding="utf-8")
        targets = [
            {
                "name": name,
                "type": runner.STATIC_LIBRARY,
                "artifacts": [{"path": f"lib{name}.a"}],
                "paths": {"source": "."},
            }
            for name in self.libraries
        ]
        cache: dict[str, str | tuple[str, str]] = {"CMAKE_CXX_COMPILER": ("FILEPATH", self.compiler)}
        cmake.plant(self.build, cache=cache, targets=targets, source=str(self.root), build=str(self.build))

    def ledger(self, rows: str) -> None:
        """Write the ledger of the rule in the tree, or remove it for no rows."""
        path = rule_ledger.ledger_path(RULE, self.root)
        if not rows:
            path.unlink(missing_ok=True)
            return
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(f"# a planted ledger\nformat {rule_ledger.FORMAT}\nrule {RULE}\n{rows}", encoding="utf-8")

    def write(self, rule: rule_registry.Rule, reason: str | None = None) -> tuple[int, str]:
        """Write the ledger from the build directory, and return the status and the written text."""
        request = runner.WriteRequest(self.root, self.build, rule_ledger.WriteMode.LOWER, reason)
        return quiet(lambda: static_tls.write(rule, request))


# ── The cases ──────────────────────────────────────────────────────


def member_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold the bytes and the names of the base thread-local variables of one member (THE RULE)."""

    def read(unit: str, is_base_library: bool) -> tuple[int, tuple[str, ...]]:
        """Return the base thread-local variables of the object of one unit."""
        return static_tls.member_tls(elf.members(scratch.object(unit))[0], is_base_library)

    expect("a buffer of a member of a base library counts", read("buffer", True)[0] == 1024)
    expect("the same buffer in a member of a different library does not count", read("buffer", False) == (0, ()))
    expect(
        "a buffer in the namespace foundation counts in a member of a different library",
        read("header", False) == (512, ("_ZN10foundation6bufferE",)),
    )
    expect("a buffer in the namespace crucible does not count in a different library", read("cache", False) == (0, ()))
    value, names = read("pair", True)
    expect("two variables add, and the larger comes first", value == 24 and names == ("second", "first"))
    expect("a member with no thread-local variable gives nothing", read("plain", True) == (0, ()))


def judge_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold the judge of the members against the thresholds and the rows (THE JUDGE, THE ERRORS)."""
    scratch.archive("foundation", ["counter", "plain"])
    scratch.archive("crucible", ["cache", "header", "plain"])
    found = static_tls.judge(planted_rule(), scratch.planted())
    expect(
        "a base buffer over the error threshold in a member of crucible is an error, and no other member is judged",
        [item.entity for item in found] == ["libcrucible.a(header.cpp.o)"] and len(errors_of(found)) == 1,
    )
    found = static_tls.judge(planted_rule(warn=2, error=1024), scratch.planted())
    expect(
        "a value over the warning threshold is a warning",
        errors_of(found) == [] and len(warnings_of(found)) == 2,
    )
    scratch.ledger(f"@build/libcrucible.a(header.cpp.o) | {KIND} | 512 | - | a planted buffer\n")
    found = static_tls.judge(planted_rule(), scratch.planted())
    expect(
        "a row of the kind admits a member over the error threshold, with a warning",
        errors_of(found) == [] and any("admits it" in message for message in warnings_of(found)),
    )
    scratch.ledger("@build/libcrucible.a(header.cpp.o) | other-kind | 512 | - | a row of a different kind\n")
    expect(
        "a row of a different kind admits nothing in this build",
        len(errors_of(static_tls.judge(planted_rule(), scratch.planted()))) == 1,
    )
    scratch.ledger(f"@build/libfoundation.a(counter.cpp.o) | {KIND} | 512 | - | a row that admits nothing\n")
    expect(
        "a row of a member under the error threshold is an error",
        any("Remove the row" in message for message in errors_of(static_tls.judge(planted_rule(), scratch.planted()))),
    )
    scratch.ledger("")
    scratch.archive("crucible", ["cache", "plain", "plain"])
    try:
        static_tls.judge(planted_rule(), scratch.planted())
        said = ""
    except runner.BuildError as problem:
        said = str(problem)
    expect("an archive with two members of one name is refused", "two members with the name plain.cpp.o" in said)
    del scratch.libraries["foundation"]
    try:
        static_tls.judge(planted_rule(), scratch.planted())
        said = ""
    except runner.BuildError as problem:
        said = str(problem)
    expect("a root with no library foundation is refused", "no static library foundation" in said)


def write_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold the write of the rows of the kind of a build directory (THE LEDGER)."""
    rule = planted_rule()
    scratch.archive("foundation", ["counter", "plain"])
    scratch.archive("crucible", ["header", "plain"])
    scratch.directory()
    status, text = scratch.write(rule)
    expect("a write refuses a member over the error threshold with no reason", status == 1 and "--reason" in text)
    status, _ = scratch.write(rule, "a planted buffer")
    ledger = rule_ledger.read(rule, scratch.root)
    expect(
        "a write with a reason writes the row of the member, with its value and its reason",
        status == 0
        and [(key.path, key.kind, entry.value, entry.reason) for key, entry in ledger.base.items()]
        == [("@build/libcrucible.a(header.cpp.o)", KIND, 512, "a planted buffer")],
    )
    status, _ = scratch.write(rule)
    expect(
        "a second write keeps the reason of the row",
        status == 0
        and [entry.reason for entry in rule_ledger.read(rule, scratch.root).base.values()] == ["a planted buffer"],
    )
    scratch.archive("crucible", ["plain"])
    scratch.directory()
    status, _ = scratch.write(rule)
    expect(
        "a write removes the row of a member under the threshold",
        status == 0 and not rule_ledger.read(rule, scratch.root).base,
    )
    request = runner.WriteRequest(scratch.root, scratch.build, rule_ledger.WriteMode.RAISE)
    status, _ = quiet(lambda: static_tls.write(rule, request))
    expect("the mode raise does not apply to the policy max", status == 2)


def cases(expect: testing.Expect) -> None:
    """Run each case on one planted tree."""
    compiler = runner.plant_compiler()
    expect("ASSAY_TEST_CXX or CRUCIBLE_GCC16_PREFIX names the patched compiler", compiler is not None)
    if compiler is None:
        return
    with TemporaryDirectory(prefix="static-tls-") as scratch_text:
        scratch = Scratch(Path(scratch_text) / "tree", compiler)
        member_cases(expect, scratch)
        judge_cases(expect, scratch)
        write_cases(expect, scratch)


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (cases,))
