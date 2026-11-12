"""The test of the rule static-tls (assay.guard.engines.elf.static_tls).

THE CASES
    The cases hold these parts of the rule:
      * The base thread-local variables of one member
      * The TLS facts of one shared library, and the library that another
        library reaches with an initial-exec relocation
      * The read of the members and the libraries, and its errors
      * The judge of the items against the limit and the ledger rows
      * The write of the ledger from a planted build.
    The compile cases use the compiler that ASSAY_TEST_CXX names.  The
    registration of the test gives the compiler of the build.  Each link of
    a shared library uses the pinned mold (assay.guard.engines.elf.runner,
    THE PLANTS).
"""

from collections.abc import Callable, Iterable
from pathlib import Path
from tempfile import TemporaryDirectory

from assay.core import ledger as rule_ledger
from assay.core import proc, testing
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.guard.engines.elf import runner, static_tls
from assay.readers import elf
from assay.tests.guard.engines.elf.test_runner import quiet

RULE = static_tls.RULE
KIND = "planted-kind"
# The directory of the shared libraries of the planted build.
SHARED_DIRECTORY = "test"
# The units of the cases.  They hold a buffer of an unnamed namespace, a buffer in the namespace foundation and a
# buffer in the namespace crucible.  They also hold a counter, a member with two variables and a plain function.  The
# units of the shared libraries hold an initial-exec block, an aligned initial-exec block and a block of a dynamic
# model, and a block that one library defines and another library reads with the initial-exec model.
UNITS = {
    "buffer": "namespace {\nthread_local char scratch[1024];\n}\nchar* scratch_entry() { return scratch; }\n",
    "header": "namespace foundation {\ninline thread_local char buffer[512];\n}\n"
    "char* buffer_entry() { return foundation::buffer; }\n",
    "cache": "namespace crucible {\nthread_local char cache[4096];\n}\nchar* cache_entry() { return crucible::cache; }\n",
    "counter": "thread_local int counter = 0;\nint counter_entry() { return ++counter; }\n",
    "pair": "thread_local long first = 0;\nthread_local char second[16];\n"
    "long pair_entry() { return first + second[0]; }\n",
    "plain": "int plain_entry(int value) { return value + 1; }\n",
    "initial": 'namespace {\n[[gnu::tls_model("initial-exec")]] thread_local char block[1024];\n}\n'
    "char* initial_entry() { return block; }\n",
    "aligned": 'namespace {\n[[gnu::tls_model("initial-exec")]] alignas(512) thread_local char block[8];\n}\n'
    "char* aligned_entry() { return block; }\n",
    "dynamic": "__thread char dynamic_block[16384];\nchar* dynamic_entry() { return dynamic_block; }\n",
    "owner": "__thread char shared_block[1024];\nchar* owner_entry() { return shared_block; }\n",
    "reader": 'extern __thread char shared_block[1024] __attribute__((tls_model("initial-exec")));\n'
    "char* reader_entry() { return shared_block; }\n",
}
SHARED_UNITS = frozenset({"initial", "aligned", "dynamic", "owner", "reader"})


def planted_rule(limit: int = 256) -> rule_registry.Rule:
    """Return the rule static-tls of a planted registry with the limit."""
    row = (
        f"rule {RULE} engine=elf scope=builddir ledger=max limit={limit} unit=bytes doc=XV | a planted quantity | "
        f"lower it"
    )
    return rule_registry.planted_rules((row,))[RULE]


def errors_of(findings: Iterable[check_report.Finding]) -> list[str]:
    """Return the message of each error."""
    return [item.message for item in findings if item.level == "error"]


def warnings_of(findings: Iterable[check_report.Finding]) -> list[str]:
    """Return the message of each warning."""
    return [item.message for item in findings if item.level == "warning"]


def raised_text(call: Callable[[], object]) -> str:
    """Return the text of the BuildError of a call with no argument, or an empty text when it raises none."""
    try:
        call()
    except runner.BuildError as problem:
        return str(problem)
    return ""


class Scratch:
    """A planted tree with objects of the units, archives of the libraries foundation and crucible, and libraries."""

    def __init__(self, root: Path, compiler: str) -> None:
        """Compile each unit with the flags of the plants of the engine, and with -fPIC for a unit of a library."""
        self.root = root
        self.build = root / "build"
        self.objects = root / "objects"
        self.build.mkdir(parents=True)
        self.objects.mkdir()
        self.compiler = compiler
        for unit, source in UNITS.items():
            (self.objects / f"{unit}.cpp").write_text(source, encoding="utf-8")
            position = ["-fPIC"] if unit in SHARED_UNITS else []
            command = [
                compiler,
                *runner.FLAGS,
                *position,
                "-c",
                str(self.objects / f"{unit}.cpp"),
                "-o",
                str(self.object(unit)),
            ]
            proc.require(proc.run(proc.Command(command, timeout_s=proc.TOOL_S)))
        self.libraries: dict[str, list[str]] = {}
        self.linked: list[Path] = []

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

    def shared(self, name: str, unit: str) -> Path:
        """Link the shared library test/libNAME.so of the build directory from one unit, and return its path."""
        path = self.build / SHARED_DIRECTORY / f"lib{name}.so"
        path.parent.mkdir(exist_ok=True)
        command = [self.compiler, "-shared", str(self.object(unit)), "-o", str(path), *runner.mold_words()]
        proc.require(proc.run(proc.Command(command, timeout_s=proc.TOOL_S)))
        return path

    def planted(self) -> runner.ElfContext:
        """Return a context of a planted build with the archives of the libraries and the shared libraries."""
        archives = {name: self.build / f"lib{name}.a" for name in self.libraries}
        linked = tuple(runner.LinkedArtifact(SHARED_DIRECTORY, path, "SHARED_LIBRARY") for path in self.linked)
        return runner.ElfContext(self.root, None, runner.PlantedBuild(kind=KIND, libraries=archives, linked=linked))

    def ledger(self, rows: str) -> None:
        """Write the ledger of the rule in the tree, or remove it for no rows."""
        path = rule_ledger.ledger_path(RULE, self.root)
        if not rows:
            path.unlink(missing_ok=True)
            return
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(f"# a planted ledger\nformat {rule_ledger.FORMAT}\nrule {RULE}\n{rows}", encoding="utf-8")

    def write(self, rule: rule_registry.Rule, reason: str | None = None) -> tuple[int, str]:
        """Write the ledger from the planted build, and return the status and the written text."""
        request = runner.WriteRequest(self.root, self.build, rule_ledger.WriteMode.LOWER, reason)
        return quiet(lambda: static_tls.write(rule, request, self.planted()))


# ── The cases ──────────────────────────────────────────────────────


def member_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold the bytes and the names of the base thread-local variables of one member (THE MEMBERS)."""

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


def library_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold the TLS facts of one shared library, and the cause of a block that another library reaches."""

    def read(name: str, unit: str) -> static_tls.LibraryRead:
        """Link one unit into a shared library, and read its TLS facts."""
        with elf.mapped(scratch.shared(name, unit)) as member:
            return static_tls.library_read(member, f"{SHARED_DIRECTORY}/lib{name}.so")

    initial = read("initial", "initial")
    expect(
        "an initial-exec block of 1024 bytes has the value 1024, the flag DF_STATIC_TLS and its variable",
        initial.value == 1024 and initial.cause == "the flag DF_STATIC_TLS" and len(initial.variables) == 1,
    )
    expect(
        "an initial-exec block of 8 bytes with the alignment 512 has the value 512",
        read("aligned", "aligned").value == 512,
    )
    dynamic = read("dynamic", "dynamic")
    expect(
        "a block of 16384 bytes of a dynamic model has its value and no cause",
        dynamic.value == 16384 and dynamic.cause == "" and "dynamic_block" in dynamic.defined,
    )
    owner, reader = read("owner", "owner"), read("reader", "reader")
    expect(
        "a library that reads the block of a different library with the initial-exec model has no block of its own",
        reader.value == 0 and reader.referenced == frozenset({"shared_block"}) and owner.cause == "",
    )
    items = static_tls.library_items([owner, reader, dynamic])
    expect(
        "the block of the library that defines the variable goes into the static TLS, and no other block does",
        [(item.item, item.value) for item in items] == [(owner.item, 1024)]
        and reader.item in items[0].cause
        and "shared_block" in items[0].cause,
    )
    expect(
        "aligned_size() rounds a size up to the alignment, and an alignment of 0 or 1 keeps the size",
        [static_tls.aligned_size(size, align) for size, align in ((252, 16), (256, 8), (8, 512), (5, 1), (0, 0))]
        == [256, 256, 512, 5, 0],
    )


def judge_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold the judge of the items against the limit and the rows (THE JUDGE, THE ERRORS)."""
    scratch.archive("foundation", ["counter", "plain"])
    scratch.archive("crucible", ["cache", "header", "plain"])
    found = static_tls.judge(planted_rule(), scratch.planted())
    expect(
        "a base buffer above the limit in a member of crucible is an error, and no other member is judged",
        [item.entity for item in found] == ["libcrucible.a(header.cpp.o)"] and len(errors_of(found)) == 1,
    )
    found = static_tls.judge(planted_rule(limit=1024), scratch.planted())
    expect("a value at or below the limit gives no finding", found == [])
    scratch.ledger("@build/libcrucible.a(header.cpp.o) | - | 512 | - | a planted buffer\n")
    found = static_tls.judge(planted_rule(), scratch.planted())
    expect(
        "a row keeps a member above the limit up to its kept value, with no finding",
        errors_of(found) == [] and warnings_of(found) == [],
    )
    scratch.ledger(f"@build/libcrucible.a(header.cpp.o) | {KIND} | 512 | - | a row with a kind\n")
    expect(
        "a row with a KIND is an error, and it admits nothing",
        len(errors_of(static_tls.judge(planted_rule(), scratch.planted()))) == 2,
    )
    scratch.ledger("@build/libfoundation.a(counter.cpp.o) | - | 512 | - | a row that admits nothing\n")
    found = static_tls.judge(planted_rule(), scratch.planted())
    expect(
        "a row of a member under the limit gives no finding, because a ledger fails only on a growth",
        not any("counter.cpp.o" in message for message in (*errors_of(found), *warnings_of(found))),
    )
    scratch.ledger("")
    scratch.archive("crucible", ["plain"])
    scratch.linked = [scratch.build / SHARED_DIRECTORY / f"lib{name}.so" for name in ("initial", "dynamic", "aligned")]
    found = static_tls.judge(planted_rule(), scratch.planted())
    expect(
        "the initial-exec blocks above the limit are errors with the path of the library, and the dynamic block is not",
        sorted(item.entity for item in found) == ["test/libaligned.so", "test/libinitial.so"]
        and len(errors_of(found)) == 2,
    )
    scratch.ledger("@build/test/libinitial.so | - | 1024 | - | a planted block\n")
    found = static_tls.judge(planted_rule(), scratch.planted())
    expect(
        "a row keeps a library above the limit by its path in the build directory",
        [item.entity for item in found] == ["test/libaligned.so"],
    )
    scratch.ledger("")
    scratch.linked = [scratch.root / "objects" / "outside.so"]
    scratch.linked[0].write_bytes((scratch.build / SHARED_DIRECTORY / "libinitial.so").read_bytes())
    said = raised_text(lambda: static_tls.judge(planted_rule(), scratch.planted()))
    expect("a shared library outside the build directory is refused", "not in the build directory" in said)
    scratch.linked = [scratch.build / SHARED_DIRECTORY / "libmissing.so"]
    try:
        static_tls.judge(planted_rule(), scratch.planted())
        skipped = False
    except runner.NotApplicable:
        skipped = True
    expect("a build that has not made a shared library does not apply", skipped)
    scratch.linked = []
    scratch.archive("crucible", ["cache", "plain", "plain"])
    said = raised_text(lambda: static_tls.judge(planted_rule(), scratch.planted()))
    expect("an archive with two members of one name is refused", "two members with the name plain.cpp.o" in said)
    del scratch.libraries["foundation"]
    said = raised_text(lambda: static_tls.judge(planted_rule(), scratch.planted()))
    expect("a root with no library foundation is refused", "no static library foundation" in said)


def write_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold the write of a build: a new row only with a reason, and no removed row (THE LEDGER)."""
    rule = planted_rule()
    scratch.archive("foundation", ["counter", "plain"])
    scratch.archive("crucible", ["header", "plain"])
    scratch.linked = [scratch.build / SHARED_DIRECTORY / "libinitial.so"]
    status, text = scratch.write(rule)
    expect("a write refuses an item above the limit with no reason", status == 1 and "--reason" in text)
    status, _ = scratch.write(rule, "a planted block")
    ledger = rule_ledger.read(rule, scratch.root)
    expect(
        "a write with a reason writes the row of the member and of the library, with an empty KIND, its value and "
        "its reason",
        status == 0
        and sorted((key.path, key.kind, entry.value, entry.reason) for key, entry in ledger.base.items())
        == [
            ("@build/libcrucible.a(header.cpp.o)", "", 512, "a planted block"),
            ("@build/test/libinitial.so", "", 1024, "a planted block"),
        ],
    )
    status, text = scratch.write(rule)
    expect(
        "a second write keeps the reason of each row, and it says that the ledger has no change",
        status == 0
        and {entry.reason for entry in rule_ledger.read(rule, scratch.root).base.values()} == {"a planted block"}
        and "with no change" in text,
    )
    scratch.archive("crucible", ["plain"])
    scratch.linked = []
    status, text = scratch.write(rule)
    expect(
        "a write from a build that does not hold the items keeps their rows",
        status == 0 and len(rule_ledger.read(rule, scratch.root).base) == 2 and "with no change" in text,
    )
    request = runner.WriteRequest(scratch.root, scratch.build, rule_ledger.WriteMode.RAISE)
    status, _ = quiet(lambda: static_tls.write(rule, request, scratch.planted()))
    expect("the mode raise does not apply to the policy max", status == 2)


def written_cases(expect: testing.Expect) -> None:
    """Hold the ledger that written() gives from the items of a build, with no build (THE LEDGER)."""
    budget = static_tls.budget_of(planted_rule())
    item = "libcrucible.a(header.cpp.o)"
    gone = rule_ledger.Key("@build/libfixy.a(old.cpp.o)", "", "")
    ledger = rule_ledger.Ledger(
        RULE,
        rule_registry.Policy.MAX,
        "",
        base={gone: rule_ledger.Entry(gone, 300, "a row of a member that this build does not hold")},
    )
    big = static_tls.MemberTls(item, 512, ("_ZN10foundation6bufferE",))
    small = static_tls.MemberTls("libfoundation.a(counter.cpp.o)", 4, ("counter",))
    library = static_tls.LibraryTls("test/libblock.so", 1024, ("block",), "the flag DF_STATIC_TLS")
    new = static_tls.written(budget, (big, small, library), ledger, "a planted buffer")
    rows = {key.path: (key.kind, entry.value, entry.reason) for key, entry in new.base.items()}
    expect(
        "written() writes the items above the limit, and keeps the row of a member that the build does not hold",
        rows
        == {
            gone.path: ("", 300, "a row of a member that this build does not hold"),
            f"@build/{item}": ("", 512, "a planted buffer"),
            "@build/test/libblock.so": ("", 1024, "a planted buffer"),
        },
    )
    kept = static_tls.written(budget, (big, library), new, None)
    expect("written() keeps a row that keeps its item, with no reason", kept.base == new.base)
    try:
        static_tls.written(budget, (big,), ledger, None)
        said = ""
    except rule_ledger.LedgerError as problem:
        said = str(problem)
    expect("written() refuses an item above the limit with no row and no reason", item in said)


def cases(expect: testing.Expect) -> None:
    """Run each case on one planted tree."""
    written_cases(expect)
    compiler = runner.plant_compiler()
    expect("ASSAY_TEST_CXX or CRUCIBLE_GCC16_PREFIX names the patched compiler", compiler is not None)
    if compiler is None:
        return
    with TemporaryDirectory(prefix="static-tls-") as scratch_text:
        scratch = Scratch(Path(scratch_text) / "tree", compiler)
        member_cases(expect, scratch)
        library_cases(expect, scratch)
        judge_cases(expect, scratch)
        write_cases(expect, scratch)


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (cases,))
