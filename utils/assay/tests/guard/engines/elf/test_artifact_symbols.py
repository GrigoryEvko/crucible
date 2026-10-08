"""The test of the rule artifact-symbols (assay.guard.engines.elf.artifact_symbols).

THE CASES
    The cases hold these parts of the rule:
      * The class and the mark of a symbol
      * The shapes of the ledger
      * The throws, the downcasts and the classes of planted archives
      * The run and the write of the rule on a planted build directory.
    The compile cases use the compiler that ASSAY_TEST_CXX names.  The
    registration of the test in test/CMakeLists.txt gives the compiler of the
    build.

THE PLANTED BUILD DIRECTORY
    Scratch compiles the units of UNITS, and it makes the archives of the
    libraries foundation, fixy and crucible from them.  It writes a build
    kind, a reply of the file API with the three libraries and the compiler,
    and a ledger.  A case changes the units of a library, the kind or the
    ledger, and then it runs or writes the rule.
"""

import sys
from collections import Counter
from collections.abc import Callable, Iterable
from functools import partial
from pathlib import Path
from tempfile import TemporaryDirectory

from assay.core import ledger as rule_ledger
from assay.core import proc, testing
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core import target as build_target
from assay.guard.engines.elf import artifact_symbols, runner
from assay.guard.engines.plugin import runner as plugin_runner
from assay.readers import cmake, elf
from assay.tests.guard.engines.elf.test_runner import quiet

RULE = artifact_symbols.RULE
REGISTRY_ROWS = (
    "rule artifact-symbols engine=elf scope=builddir ledger=count doc=XII | a planted symbol rule | lower it",
)
HEAD = f"# a planted ledger\nformat {rule_ledger.FORMAT}\nrule {RULE}\n"
KIND = "planted-kind"
OTHER = "planted-other"
ROWS_K = "configuration k kind k\nconfiguration k tier t\n"
ROWS_KM = f"{ROWS_K}configuration m kind m\nconfiguration m tier t\n"
# The units of the archives of the cases of the artifact.  Each unit gives one archive libUNIT.a.
FIXTURES = {
    # An error path that returns and does not throw.
    "clean": "struct Plain { int value = 0; };\nint clean_entry(int input) noexcept {\n    Plain plain{input};\n"
    "    return plain.value;\n}\n",
    "throws": "int throwing_entry(int input) {\n    if (input < 0) { throw input; }\n    return input;\n}\n",
    # A catch with no throw: the shape of Topology.cpp, which gives no finding.
    "catches": "extern int may_fail(int);\nint catching_entry(int input) noexcept {\n    try {\n"
    "        return may_fail(input);\n    } catch (...) {\n        return -1;\n    }\n}\n",
    "rtti": "struct Base { virtual ~Base() = default; virtual int tag() const { return 0; } };\n"
    "struct Derived final : Base { int tag() const override { return 1; } };\n"
    "int downcast_entry(Base* base) noexcept {\n    auto* derived = dynamic_cast<Derived*>(base);\n"
    "    return derived ? derived->tag() : -1;\n}\n",
    # A throw that a row admits, and a throw in a local function that nothing calls, which no link keeps.
    "admitted": "int admitted_throwing_entry(int input) {\n    if (input < 0) { throw input; }\n    return input;\n}\n"
    "[[gnu::used]] static int dead_throwing_entry(int input) {\n    if (input < 0) { throw input; }\n"
    "    return input;\n}\nint also_clean(int input) noexcept { return input; }\n",
    "polymorphic": "struct Admitted { virtual ~Admitted() = default; virtual int tag() const { return 2; } };\n"
    "int admitted_class_entry() noexcept { Admitted admitted; Admitted* held = &admitted;\n"
    "    return held->tag(); }\nAdmitted* admitted_make() { static Admitted one; return &one; }\n",
    # A class with a vtable in a different object: a reference, and not a definition.
    "references": "#include <new>\nstruct Elsewhere { Elsewhere() = default; virtual int tag() const; };\n"
    "Elsewhere* placed_entry(void* memory) noexcept { return new (memory) Elsewhere; }\n",
    "allocation": "#include <new>\nint allocation_entry(int input) { if (input < 0) { throw std::bad_alloc(); } "
    "return input; }\nint array_entry(int input) { if (input < 0) { throw std::bad_array_new_length(); }"
    " return input; }\n",
    # A function that throws std::bad_alloc and an int: the int throw stays a finding.
    "mixed": "#include <new>\nint mixed_entry(int input) {\n    if (input < 0) { throw std::bad_alloc(); }\n"
    "    if (input > 9) { throw input; }\n    return input;\n}\n",
    # A weak reference to the downcast of the C++ runtime: a weak undefined symbol, and not a strong one.
    "weak_downcast": 'extern "C" [[gnu::weak]] void* __dynamic_cast(const void*, const void*, const void*, long);\n'
    "void* weak_downcast_entry(const void* pointer) noexcept {\n"
    "    return __dynamic_cast ? __dynamic_cast(pointer, nullptr, nullptr, 0) : nullptr;\n}\n",
}
ADMIT_ROWS = (
    "@build/libadmitted.a | throw | admit | admitted_throwing_entry | a planted throw\n"
    "@build/libpolymorphic.a | vtable | admit | Admitted | a planted class\n"
)
# The units of the planted build directory.  base holds a plain function, and threads holds a std::thread call.
# text is a planted std::string use.  guard holds a function-local static with an initializer that can throw.  It
# references the guard functions and __gxx_personality_v0 of the allowlist, and _Unwind_Resume of libgcc.  glibc
# holds calls of functions of glibc.  thrower holds a throw of an int.
UNITS = {
    "base": 'extern "C" int crucible_symbols_extern(int);\n'
    "int plain_entry(int value) { return crucible_symbols_extern(value) + 1; }\n",
    "threads": "#include <thread>\nunsigned thread_entry() noexcept { return std::thread::hardware_concurrency(); }\n",
    "text": "#include <cstddef>\n#include <string>\nstd::size_t text_entry(int count) {\n"
    "    std::string text(static_cast<std::size_t>(count), 'x');\n    return text.size();\n}\n",
    "guard": 'extern "C" int crucible_symbols_seed();\n'
    "int guard_entry() { static const int seeded = crucible_symbols_seed(); return seeded; }\n",
    "glibc": "#include <cstdlib>\n#include <cstring>\n"
    'extern "C" void crucible_symbols_at_exit();\n'
    "void copy_entry(char* to, const char* from, unsigned long size) { std::memcpy(to, from, size); }\n"
    "int exit_entry() { return std::atexit(crucible_symbols_at_exit); }\n",
    "thrower": "int thrower_entry(int input) {\n    if (input < 0) { throw input; }\n    return input;\n}\n",
}
# A compiler that prints the target of an aarch64 GCC with no target flag.  It runs the patched compiler for each
# other command.  It has no -march value, and it gives its target through -mcpu.
AARCH64_WRAPPER = """\
import os
import sys
if "--help=target" in sys.argv:
    sys.stdout.write({help!r})
    sys.exit(0)
os.execv({real!r}, [{real!r}, *sys.argv[1:]])
"""
LOG_PREFIX = "build+test aarch64\tTest\t2026-10-02T21:00:00Z "


def planted_rule() -> rule_registry.Rule:
    """Return the rule artifact-symbols of a planted registry."""
    return rule_registry.planted_rules(REGISTRY_ROWS)[RULE]


def errors_of(findings: Iterable[check_report.Finding]) -> list[str]:
    """Return the message of each error."""
    return [item.message for item in findings if item.level == "error"]


def parsed(text: str) -> rule_ledger.Ledger:
    """Read a planted ledger of the rule."""
    return rule_ledger.parse(HEAD + text, "planted.txt", RULE, rule_registry.Policy.COUNT)


def is_refused(text: str) -> bool:
    """Return True when the reader of the ledger refuses a planted ledger, or the rule gives it a row problem."""
    try:
        ledger = parsed(text)
    except rows_grammar.RowsError:
        return True
    return bool(artifact_symbols.row_problems(ledger))


def raised(action: Callable[[], object]) -> str:
    """Return the message of the BuildError that an action raises, or an empty text when it raises none."""
    try:
        action()
    except runner.BuildError as problem:
        return str(problem)
    return ""


def compiled(compiler: str, source: Path, obj: Path) -> None:
    """Compile one planted unit with the flags of the legacy fixtures.

    Raises:
        proc.ToolFailed: If the compile fails
    """
    command = [compiler, "-std=c++17", "-O1", "-ffunction-sections", "-c", str(source), "-o", str(obj)]
    proc.require(proc.run(proc.Command(command, timeout_s=proc.TOOL_S)))


def archived(archive: Path, objects: Iterable[Path]) -> None:
    """Make an archive of the objects again.

    Raises:
        proc.ToolFailed: If ar fails
    """
    archive.unlink(missing_ok=True)
    proc.require(proc.run(proc.Command(["ar", "rcs", str(archive), *map(str, objects)], timeout_s=proc.QUICK_S)))


# ── The functions ──────────────────────────────────────────────────


def class_cases(expect: testing.Expect) -> None:
    """Hold the class and the mark of each kind of symbol (THE CLASS OF A SYMBOL)."""
    for symbol, wanted in (
        ("_Znwm", "alloc"),
        ("_ZdlPvm", "alloc"),
        ("_ZnwmSt11align_val_t", "alloc"),
        ("__cxa_throw", "abi"),
        ("__gxx_personality_v0", "abi"),
        ("__dynamic_cast", "abi"),
        ("_ZTVN10__cxxabiv120__si_class_type_infoE", "abi"),
        ("_ZSt9terminatev", "std"),
        ("_ZNKSt5ctypeIcE13_M_widen_initEv", "std"),
        ("_ZTISt9bad_alloc", "std"),
        ("_ZTTSt14basic_ifstreamIcSt11char_traitsIcEE", "std"),
        ("_ZNSaIcEC1Ev", "std"),
        ("_ZNSt3_V215system_categoryEv", "std"),
        ("_ZTSSt9bad_alloc", "std"),
        ("_ZN9__gnu_cxx27__verbose_terminate_handlerEv", "gnu"),
        ("__once_proxy", "c"),
        ("_ZN8crucible6detail5entryEv", "other"),
        ("_ZN4stdx1fEv", "other"),
    ):
        expect(f"the class of {symbol} is {wanted}", artifact_symbols.classify(symbol) == wanted)
    expect(
        "the allowlist marks the guard functions and __gxx_personality_v0, and not operator new",
        [
            artifact_symbols.mark_of(name)
            for name in ("__cxa_guard_acquire", "__gxx_personality_v0", "_Znwm", "__cxa_throw")
        ]
        == [artifact_symbols.ALLOW, artifact_symbols.ALLOW, artifact_symbols.RATCHET, artifact_symbols.RATCHET],
    )


def ledger_cases(expect: testing.Expect) -> None:
    """Hold the shapes of the ledger (THE LEDGER): each malformed ledger is refused, and the good one is read."""
    for name, body in (
        ("a row with three cells", f"{ROWS_K}@build/lib.a | ratchet | 1 | _ZSt9terminatev\n"),
        ("a row with an unknown mark", f"{ROWS_K}@build/lib.a | maybe | 1 | _Zx | -\n"),
        ("a row that names no archive of the build directory", f"{ROWS_K}lib.a | ratchet | 1 | _Zx | -\n"),
        ("a symbol row with the count 2", f"{ROWS_K}@build/lib.a | ratchet | 2 | _Zx | -\n"),
        ("a symbol row before the configuration rows", f"@build/lib.a | ratchet | 1 | _Zx | -\n{ROWS_K}"),
        ("a second tier of one kind", f"{ROWS_K}configuration k tier u\n"),
        ("a configuration with no tier", "configuration k kind k\n"),
        ("a configuration with a kind that is not its name", "configuration k kind m\nconfiguration k tier t\n"),
        ("symbol rows and no configuration", "@build/lib.a | ratchet | 1 | _Zx | -\n"),
        (
            "a difference row that gives a symbol the count 2",
            f"{ROWS_KM}@build/lib.a | ratchet | 1 | _Zx | -\n@m | @build/lib.a | ratchet | +1 | _Zx | -\n",
        ),
        ("a row throw with no reason", f"{ROWS_K}@build/lib.a | throw | admit | entry | -\n"),
        ("a row throw with a count", f"{ROWS_K}@build/lib.a | throw | 1 | entry | -\n"),
        ("a symbol row with the value admit", f"{ROWS_K}@build/lib.a | ratchet | admit | _Zx | a reason\n"),
        ("a row of an unknown kind with the value admit", f"{ROWS_K}@build/lib.a | catch | admit | entry | a reason\n"),
        (
            "a row downcast with the value admit",
            f"{ROWS_K}@build/lib.a | downcast | admit | __dynamic_cast | a reason\n",
        ),
        (
            "a difference row of a row throw",
            f"{ROWS_KM}@build/lib.a | throw | admit | entry | a reason\n@m | @build/lib.a | throw | +1 | entry | -\n",
        ),
    ):
        expect(f"the ledger refuses {name}", is_refused(body))
    good = (
        f"{ROWS_KM}@build/lib.a | allow | 1 | __cxa_guard_acquire | -\n"
        "@build/lib.a | throw | admit | entry | a reason\n@build/lib.a | vtable | admit | Class | a reason\n"
        "@m | @build/lib.a | allow | -1 | __cxa_guard_acquire | -\n"
    )
    expect("the ledger reads a good set of rows of each shape", not is_refused(good))


# ── The archives ───────────────────────────────────────────────────


def artifact_cases(expect: testing.Expect) -> None:
    """Plant a throw, a downcast and their look-alikes in archives, and hold the verdict of each (THE TWO PROPERTIES)."""
    compiler = runner.plant_compiler()
    expect("ASSAY_TEST_CXX or CRUCIBLE_GCC16_PREFIX names the patched compiler", compiler is not None)
    if compiler is None:
        return
    rule = planted_rule()
    with TemporaryDirectory(prefix="artifact-symbols-") as scratch_text:
        scratch = Path(scratch_text)
        for unit, source in FIXTURES.items():
            (scratch / f"{unit}.cpp").write_text(source, encoding="utf-8")
            compiled(compiler, scratch / f"{unit}.cpp", scratch / f"{unit}.o")
            archived(scratch / f"lib{unit}.a", [scratch / f"{unit}.o"])
        artifacts = {f"lib{unit}.a": artifact_symbols.read_artifact(scratch / f"lib{unit}.a") for unit in FIXTURES}
        wanted = {reference.function for artifact in artifacts.values() for reference in artifact.references}
        wanted |= {symbol for artifact in artifacts.values() for _, symbol in artifact.defined}
        ordered = sorted(wanted)
        names = dict(zip(ordered, elf.demangle(ordered), strict=True))
        ledger = parsed(ADMIT_ROWS)
        rows = list(ledger.admits())
        counted: Counter[str] = Counter()

        def found(unit: str, keys: list[rule_ledger.Key] = rows) -> list[check_report.Finding]:
            """Return the findings of the archive of one unit."""
            archive = f"lib{unit}.a"
            return artifact_symbols.class_findings({archive: artifacts[archive]}, names, keys, counted)

        def kinds(unit: str) -> list[str]:
            """Return the kind of each finding of the archive of one unit."""
            return sorted(item.kind for item in found(unit))

        expect("a clean archive gives no finding", found("clean") == [])
        throws = found("throws")
        expect(
            "a throw gives a finding at __cxa_throw in its function",
            [item.kind for item in throws] == [artifact_symbols.THROW]
            and "__cxa_throw in throws.o, in throwing_entry(int)." in throws[0].message,
        )
        expect("a catch with no throw is the documented boundary, and it gives no finding", found("catches") == [])
        expect(
            "a downcast and a polymorphic type give findings",
            artifact_symbols.DOWNCAST in kinds("rtti") and artifact_symbols.VTABLE in kinds("rtti"),
        )
        admitted = found("admitted")
        expect(
            "a throw that a row admits has the TEXT of the row as its entity, and a dead local function gives none",
            [item.entity for item in admitted] == ["admitted_throwing_entry"],
        )
        polymorphic = found("polymorphic")
        expect(
            "the typeinfo and the vtable of a class that a row admits have the TEXT of the row as their entity",
            polymorphic != [] and {item.entity for item in polymorphic} == {"Admitted"},
        )
        judged = rule_ledger.compare(rule, ledger, [*admitted, *polymorphic])
        expect("the rows admit the throw and the class", errors_of(judged) == [])
        expect("a reference to a vtable of a different object gives no finding", found("references") == [])
        before = counted["allocation"]
        expect(
            "a throw of std::bad_alloc or std::bad_array_new_length gives no finding, and the run counts it",
            found("allocation") == [] and counted["allocation"] >= before + 2,
        )
        mixed = found("mixed")
        expect(
            "a throw of an int beside a throw of std::bad_alloc gives a finding",
            len(mixed) == 1 and "__cxa_throw in mixed.o, in mixed_entry(int)." in mixed[0].message,
        )
        weak = found("weak_downcast")
        expect(
            "a weak reference to the downcast gives a finding",
            [(item.kind, item.entity) for item in weak] == [(artifact_symbols.DOWNCAST, "__dynamic_cast")],
        )
        (scratch / "libjunk.a").write_text("not an archive\n", encoding="utf-8")
        said = raised(partial(artifact_symbols.read_artifact, scratch / "libjunk.a"))
        expect("an archive that the ELF reader cannot read is refused", "cannot read" in said)
        broken = elf.make_file(
            [elf.Planted(".symtab", bytes(elf.SYMBOL_SIZE), elf.SHT_SYMTAB, link=99, entry_size=elf.SYMBOL_SIZE)]
        )
        (scratch / "libbroken.a").write_bytes(elf.make_archive([("broken.o", broken)]))
        said = raised(partial(artifact_symbols.read_artifact, scratch / "libbroken.a"))
        expect("an archive with a symbol table that the ELF reader cannot read is refused", "cannot read" in said)
        stale = parsed(
            ADMIT_ROWS + "@build/libclean.a | throw | admit | no_such_function | a row that admits nothing\n"
        )
        stale_rows = list(stale.admits())
        judged = rule_ledger.compare(
            rule,
            stale,
            [*found("admitted", stale_rows), *found("polymorphic", stale_rows), *found("clean", stale_rows)],
        )
        expect(
            "a row that admits nothing is an error",
            [message.split(" (")[0] for message in errors_of(judged)] == ["the row of @build/libclean.a"],
        )
        other = found("throws", stale_rows)
        expect(
            "a row of one archive does not admit a throw in a different archive",
            errors_of(rule_ledger.compare(rule, stale, other)) != [] and other[0].entity == "throwing_entry(int)",
        )


# ── The planted build directory ────────────────────────────────────


class Scratch:
    """A planted build directory with the archives of three libraries, a build kind, a reply and a ledger.

    THE PLANTED BUILD DIRECTORY tells what the directory holds.
    """

    def __init__(self, root: Path, compiler: str, rule: rule_registry.Rule) -> None:
        """Compile the units, and make the archives of the libraries foundation, fixy and crucible."""
        self.root = root
        self.compiler = compiler
        self.rule = rule
        self.build = root / "build"
        self.objects = root / "objects"
        self.build.mkdir(parents=True)
        self.objects.mkdir()
        self.ledger = rule_ledger.ledger_path(RULE, root)
        self.ledger.parent.mkdir(parents=True)
        self.ledger.write_text(HEAD, encoding="utf-8")
        self.extra: dict[str, str | tuple[str, str]] = {}
        self.missing: list[str] = []
        for unit, source in UNITS.items():
            (self.objects / f"{unit}.cpp").write_text(source, encoding="utf-8")
            compiled(compiler, self.objects / f"{unit}.cpp", self.objects / f"{unit}.o")
        self.units: dict[str, list[str]] = {}
        self.set_kind(KIND)
        self.archive("foundation", ["base", "threads"])
        self.archive("fixy", ["base"])
        self.archive("crucible", ["glibc", "base"])

    def set_kind(self, kind: str) -> None:
        """Write the build kind of the build directory."""
        (self.build / "build-kind.txt").write_text(f"{kind}\n", encoding="utf-8")

    def archive(self, name: str, units: list[str]) -> None:
        """Make the archive of one library again from its units, and write the reply."""
        archived(self.build / f"lib{name}.a", [self.objects / f"{unit}.o" for unit in units])
        self.units[name] = units
        self.plant()

    def plant(self) -> None:
        """Write the reply of the file API: the cache with the compiler, and a target for each library."""
        cache: dict[str, str | tuple[str, str]] = {
            "CMAKE_BUILD_TYPE": ("STRING", "Debug"),
            "CMAKE_CXX_FLAGS": ("STRING", ""),
            "CMAKE_CXX_COMPILER": ("FILEPATH", self.compiler),
            **self.extra,
        }
        targets = [
            {
                "name": name,
                "type": runner.STATIC_LIBRARY,
                "artifacts": [{"path": f"lib{name}.a"}],
                "paths": {"source": "."},
            }
            for name in [*self.units, *self.missing]
        ]
        cmake.plant(self.build, cache=cache, targets=targets, source=str(self.root), build=str(self.build))

    def run(self) -> tuple[int, list[check_report.Finding], str]:
        """Run the rule on the build directory, and return the status, the findings and the written text."""
        status, text = quiet(lambda: artifact_symbols.run(self.rule, runner.ElfContext(self.root, self.build), None))
        return status, [item for item in map(check_report.parse_line, text.splitlines()) if item], text

    def run_rule(self) -> tuple[int, str]:
        """Run the rule through the runner of the engine, and return the status and the written text."""
        return quiet(lambda: runner.run_rule(self.rule, runner.ElfContext(self.root, self.build), None))

    def write(
        self, mode: rule_ledger.WriteMode = rule_ledger.WriteMode.LOWER, import_log: Path | None = None
    ) -> tuple[int, str]:
        """Write the ledger from the build directory or from a log, and return the status and the written text."""
        request = runner.WriteRequest(self.root, None if import_log else self.build, mode, None, import_log)
        return quiet(lambda: artifact_symbols.write(self.rule, request))

    def ledger_text(self) -> str:
        """Return the text of the ledger."""
        return self.ledger.read_text(encoding="utf-8")

    def rows(self) -> rule_ledger.Ledger:
        """Read the ledger."""
        return rule_ledger.read(self.rule, self.root)

    def tier(self, kind: str) -> str:
        """Return the tier of the configuration of a kind in the ledger, or an empty text."""
        configuration = self.rows().configuration(kind)
        return dict(configuration.facts).get(plugin_runner.TIER_KEY, "") if configuration is not None else ""


def keys_of(ledger: rule_ledger.Ledger, kind: str) -> dict[str, dict[str, int]]:
    """Return the symbols of each library of the configuration of a kind, with their counts."""
    found: dict[str, dict[str, int]] = {}
    for key, count in ledger.expected(kind).items():
        found.setdefault(key.path, {})[key.entity] = count
    return found


def first_write_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold the first write, an equal build, a new use, a symbol that goes and a new allowlist symbol."""
    status, _ = scratch.write()
    expect("a write refuses the first ratchet symbols without --raise", status == 1 and scratch.ledger_text() == HEAD)
    status, _ = scratch.write(rule_ledger.WriteMode.RAISE)
    written = scratch.rows()
    threads = [entity for entity in keys_of(written, KIND).get("@build/libfoundation.a", {}) if "hardware" in entity]
    expect(
        "a write with --raise writes the configuration of the kind as the base, and a ratchet row of the class std",
        status == 0
        and written.base_name == KIND
        and len(threads) == 1
        and artifact_symbols.classify(threads[0]) == "std"
        and f"configuration {KIND} kind {KIND}\n" in scratch.ledger_text(),
    )
    expect("memcpy and atexit of glibc give no row", "@build/libcrucible.a" not in keys_of(written, KIND))
    status, found, _ = scratch.run()
    expect(
        "an equal build passes, with one warning for the library that keeps symbols",
        status == 0
        and errors_of(found) == []
        and [item.message.split(" keeps")[0] for item in found] == ["@build/libfoundation.a"],
    )
    scratch.archive("foundation", ["base", "threads", "text"])
    status, found, _ = scratch.run()
    expect(
        "a planted std::string use adds ratchet symbols, and the rule fails and names the member",
        status == 1
        and errors_of(found) != []
        and all("text.o" in message for message in errors_of(found))
        and any("A change can only remove" in message for message in errors_of(found)),
    )
    status, text = scratch.write()
    expect("a write refuses the planted symbols", status == 1 and "not written" in text)
    scratch.archive("foundation", ["base", "threads"])
    expect("the build with no plant passes again", scratch.run()[0] == 0)
    scratch.archive("foundation", ["base"])
    status, found, _ = scratch.run()
    expect(
        "a symbol that goes fails until a write",
        status == 1 and any("hardware" in message and "guard write" in message for message in errors_of(found)),
    )
    status, _ = scratch.write()
    expect(
        "after a write the build passes, and the ledger keeps no row of std::thread",
        status == 0 and scratch.run()[0] == 0 and "hardware_concurrency" not in scratch.ledger_text(),
    )
    scratch.archive("foundation", ["base", "guard"])
    status, found, _ = scratch.run()
    guard_errors = [message for message in errors_of(found) if "__cxa_guard" in message]
    expect(
        "a new allowlist symbol fails until a write, and the message names the allowlist",
        status == 1 and guard_errors != [] and all("allowlist" in message for message in guard_errors),
    )
    status, _ = scratch.write()
    guard_rows = {key.entity: key.kind for key in scratch.rows().expected(KIND) if key.path == "@build/libfoundation.a"}
    expect(
        "a write writes the allowlist symbols without --raise, with the mark allow, and no row of libgcc",
        status == 0
        and [guard_rows.get(name) for name in ("__cxa_guard_acquire", "__gxx_personality_v0")]
        == [artifact_symbols.ALLOW, artifact_symbols.ALLOW]
        and "_Unwind_Resume" not in guard_rows,
    )
    expect("after a write the build with the guard passes", scratch.run()[0] == 0)


def ledger_row_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold a row with an incorrect mark, a row of a library that the build does not make, and a different tier."""
    good = scratch.ledger_text()
    scratch.ledger.write_text(
        good.replace(
            "libfoundation.a | allow | 1 | __cxa_guard_acquire |",
            "libfoundation.a | ratchet | 1 | __cxa_guard_acquire |",
        ),
        encoding="utf-8",
    )
    status, found, _ = scratch.run()
    expect(
        "a row with an incorrect mark is an error",
        status == 1 and any("ratchet" in message and "__cxa_guard_acquire" in message for message in errors_of(found)),
    )
    scratch.ledger.write_text(good + "@build/libgone.a | ratchet | 1 | _ZSt9terminatev | -\n", encoding="utf-8")
    status, found, _ = scratch.run()
    expect(
        "a row of a library that the build does not make is an error",
        status == 1 and any("libgone.a" in message for message in errors_of(found)),
    )
    tier = scratch.tier(KIND)
    scratch.ledger.write_text(
        good.replace(f"configuration {KIND} tier {tier}\n", f"configuration {KIND} tier other\n"), encoding="utf-8"
    )
    status, _, text = scratch.run()
    expect(
        "a build of a different tier judges only the throws and the classes, and one line gives the two tiers",
        status == 0
        and f"on the tier {tier}" in text
        and f"{KIND} on other" in text
        and f"tier is {tier} in the build and other in the ledger" in text
        and rule_ledger.ROW_MARK not in text,
    )
    scratch.ledger.write_text(good, encoding="utf-8")


def configuration_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold a kind that the ledger does not hold, its import from a log, and the write of each configuration."""
    good = scratch.ledger_text()
    tier = scratch.tier(KIND)

    def build_of(kind: str, units: list[str]) -> None:
        """Set the kind of the build directory, and make the archive of the library foundation again."""
        scratch.set_kind(kind)
        scratch.archive("foundation", units)

    build_of(OTHER, ["base"])
    status, _, text = scratch.run()
    mark = rule_ledger.ROW_MARK
    expect(
        "a kind that the ledger does not hold prints its configuration rows and its difference rows",
        status == 0
        and f"{mark}configuration {OTHER} tier {tier}" in text
        and f"{mark}@{OTHER} | @build/libfoundation.a | allow | -1 | __cxa_guard_acquire" in text,
    )
    log = scratch.root / "ci.log"
    log.write_text(
        "".join(f"{LOG_PREFIX}{line}\n" for line in [*text.splitlines(), f"{mark}configuration {OTHER} extra fact"]),
        encoding="utf-8",
    )
    imported, said = scratch.write(import_log=log)
    expect(
        "--import refuses a configuration with a fact other than the kind and the tier, and writes no ledger",
        imported == 1 and "no other" in said and scratch.ledger_text() == good,
    )
    log.write_text("".join(f"{LOG_PREFIX}{line}\n" for line in text.splitlines()), encoding="utf-8")
    imported, _ = scratch.write(import_log=log)
    status, found, _ = scratch.run()
    expect(
        "--import puts the rows of the log into the ledger, and the build of the kind then passes",
        imported == 0
        and status == 0
        and errors_of(found) == []
        and [item.name for item in scratch.rows().configurations] == [KIND, OTHER],
    )
    build_of(KIND, ["base", "threads", "guard"])
    scratch.write(rule_ledger.WriteMode.RAISE)
    build_of(OTHER, ["base"])
    status, found, _ = scratch.run()
    expect("a write of the base keeps the symbols of the other kind", status == 0 and errors_of(found) == [])
    build_of(OTHER, ["base", "guard"])
    status, _ = scratch.write()
    build_of(KIND, ["base", "threads", "guard"])
    status_base, found, _ = scratch.run()
    expect(
        "a write of the other kind writes its differences and keeps the symbols of the base",
        status == 0 and status_base == 0 and errors_of(found) == [],
    )
    scratch.ledger.write_text(good, encoding="utf-8")
    build_of(KIND, ["base", "guard"])


def tier_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold an aarch64 compiler with no -march value, and the builds that do not apply."""
    good = scratch.ledger_text()
    real = scratch.compiler
    wrapper = scratch.root / "aarch64-g++"
    wrapper.write_text(
        f"#!{sys.executable}\n" + AARCH64_WRAPPER.format(help=build_target.AARCH64_HELP, real=real), encoding="utf-8"
    )
    wrapper.chmod(0o755)
    scratch.compiler = str(wrapper)
    scratch.plant()
    status, _ = scratch.write()
    expect(
        "a compiler with no -march value has a tier, and the build of an aarch64 compiler is read",
        status == 0 and scratch.run()[0] == 0 and scratch.tier(KIND).startswith(build_target.DEFAULT_NAME + "-"),
    )
    scratch.compiler = real
    scratch.ledger.write_text(good, encoding="utf-8")
    scratch.missing = ["unbuilt"]
    scratch.plant()
    status, _, text = scratch.run()
    expect("a library that the build has not made does not apply", status == runner.SKIPPED and "libunbuilt.a" in text)
    scratch.missing = []
    scratch.extra = {"CRUCIBLE_UBSAN_STRICT": ("BOOL", "ON")}
    scratch.plant()
    status, _, text = scratch.run()
    expect("a build of the UBSan-strict preset does not apply", status == runner.SKIPPED and "UBSan-strict" in text)
    scratch.extra = {}
    saved = dict(scratch.units)
    scratch.units = {name: units for name, units in saved.items() if name != "crucible"}
    scratch.plant()
    status, text = scratch.run_rule()
    expect(
        "a root with no library crucible is an error of the build",
        status == 1 and "no static library crucible" in text,
    )
    scratch.units = saved
    scratch.plant()
    status, text = scratch.write(rule_ledger.WriteMode.ADMIT)
    expect("the mode admit does not apply to the write", status == 2 and "reason" in text)


def admit_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold the rows throw: a row that admits a throw, a row that admits nothing, and their write."""
    good = scratch.ledger_text()
    scratch.archive("fixy", ["base", "thrower"])
    scratch.write(rule_ledger.WriteMode.RAISE)
    admitted = "@build/libfixy.a | throw | admit | thrower_entry | a planted throw\n"
    stale = "@build/libfixy.a | throw | admit | no_such_function | a planted row that admits nothing\n"
    scratch.ledger.write_text(scratch.ledger_text() + admitted + stale, encoding="utf-8")
    status, found, _ = scratch.run()
    expect(
        "a row throw admits its throw, and a row throw that admits nothing is an error",
        status == 1
        and [message.split(" (")[0] for message in errors_of(found)] == ["the row of @build/libfixy.a"]
        and any("no_such_function" in message for message in errors_of(found)),
    )
    status, _ = scratch.write()
    written = scratch.ledger_text()
    expect(
        "a write keeps a row throw that admits a throw, with its reason, and removes a row that admits nothing",
        status == 0 and admitted in written and "no_such_function" not in written and scratch.run()[0] == 0,
    )
    scratch.archive("fixy", ["base"])
    status, found, _ = scratch.run()
    expect(
        "a row throw of a throw that goes is an error in each build",
        status == 1 and any("thrower_entry" in message for message in errors_of(found)),
    )
    scratch.ledger.write_text(good, encoding="utf-8")


def build_cases(expect: testing.Expect) -> None:
    """Run each case of the planted build directory, in sequence."""
    compiler = runner.plant_compiler()
    if compiler is None:
        expect("ASSAY_TEST_CXX or CRUCIBLE_GCC16_PREFIX names the patched compiler", False)
        return
    with TemporaryDirectory(prefix="artifact-symbols-build-") as scratch_text:
        scratch = Scratch(Path(scratch_text) / "tree", compiler, planted_rule())
        first_write_cases(expect, scratch)
        ledger_row_cases(expect, scratch)
        configuration_cases(expect, scratch)
        tier_cases(expect, scratch)
        admit_cases(expect, scratch)


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (class_cases, ledger_cases, artifact_cases, build_cases))
