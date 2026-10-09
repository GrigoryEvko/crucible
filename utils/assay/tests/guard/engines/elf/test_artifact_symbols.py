"""The test of the rule artifact-symbols (assay.guard.engines.elf.artifact_symbols).

THE CASES
    The cases hold these parts of the rule:
      * The class of a symbol, and the mark and the entity of an import
      * The providers of the compiler and of a link line, and the owner of a
        name
      * The shapes of the ledger
      * The throws, the downcasts and the classes of planted archives
      * The hidden imports of planted objects.  Each unit plants the section
        of the quarantine plugin with an asm statement (HIDDEN_FIXTURES)
      * The run and the write of the rule on a planted build directory.
    The compile cases use the compiler that ASSAY_TEST_CXX names.  The
    registration of the test in test/CMakeLists.txt gives the compiler of the
    build.

THE PLANTED BUILD DIRECTORY
    Scratch compiles the units of UNITS, and it makes the archives of the
    libraries planted_tree, foundation, fixy and crucible from them.  It also
    makes a shared library and an archive of LINKED_UNITS in a directory that
    is not the build directory.  It writes a build kind, a reply of the file
    API and a ledger.
    The reply holds the libraries, the compiler, and a program with a link
    line.  A case changes the units of a library, the link line, the kind or
    the ledger, and then it runs or writes the rule.
"""

import sys
from collections import Counter
from collections.abc import Callable, Iterable
from functools import partial
from pathlib import Path
from tempfile import TemporaryDirectory

from assay.core import configuration as build_config
from assay.core import ledger as rule_ledger
from assay.core import proc, testing
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core import target as build_target
from assay.guard.engines.elf import artifact_symbols, runner
from assay.readers import cmake, elf, sections
from assay.tests.guard.engines.elf.test_runner import quiet

RULE = artifact_symbols.RULE
REGISTRY_ROWS = (
    "rule artifact-symbols engine=elf scope=builddir ledger=count doc=XII | a planted symbol rule | lower it",
)
HEAD = f"# a planted ledger\nformat {rule_ledger.FORMAT}\nrule {RULE}\n"
KIND = "planted-kind"
OTHER = "planted-other"
THIRD = "planted-third"
ROWS_K = "configuration k kind k\nconfiguration k tier t\n"
ROWS_KM = f"{ROWS_K}configuration m kind m\nconfiguration m tier t\n"
# The head of an asm statement that plants the section of the quarantine plugin, and its end.  Each record of a unit
# is one more .ascii line between them.
SECTION_HEAD = (
    'asm(".pushsection .crucible.quarantine,\\"e\\",%progbits\\n"\n'
    '    ".ascii \\"# crucible-guard 2 stamp=planted\\\\n\\"\\n"\n'
)
SECTION_END = '    ".popsection\\n");\n'
# The units of the cases of the hidden imports, each with the symbols of the records of its section.
HIDDEN_FIXTURES = {
    # A call with a record of its symbol.
    "recorded": (("getenv",), '#include <cstdlib>\nconst char* home_entry() { return std::getenv("HOME"); }\n'),
    # A call with no record.
    "unrecorded": ((), '#include <cstdlib>\nconst char* home_entry() { return std::getenv("HOME"); }\n'),
    # A call in a local function that no section names.
    "dead": (
        (),
        (
            '#include <cstdlib>\n[[gnu::used]] static const char* dead_entry() { return std::getenv("HOME"); }\n'
            "int live_entry(int value) { return value + 1; }\n"
        ),
    ),
    # A call in a function that a record names, and the record connects to the call.
    "reached": (
        ("_Z12helper_entryv",),
        (
            '#include <cstdlib>\n[[gnu::noinline]] const char* helper_entry() { return std::getenv("HOME"); }\n'
            "const char* outer_entry() { return helper_entry(); }\n"
        ),
    ),
    # A typeinfo that only the type table of a catch names, and the calls of the catch with their records.
    "caught": (
        ("__cxa_begin_catch", "__cxa_end_catch"),
        (
            "#include <exception>\nextern int may_fail(int);\nint caught_entry(int value) noexcept {\n    try {\n"
            "        return may_fail(value);\n    } catch (const std::exception&) {\n        return -1;\n    }\n}\n"
        ),
    ),
    # A named variable with an initializer that names a library function: a data table of the tree.
    "table": ((), "#include <cstdlib>\nvoid* home_table[] = {reinterpret_cast<void*>(&std::getenv)};\n"),
    # A section with no named symbol that names a library function: data that the compiler makes.
    "anonymous": ((), 'asm(".pushsection .data.rel.ro.planted,\\"a\\"\\n.quad getenv\\n.popsection\\n");\n'),
}
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
    # A call of std::string_view::substr, which throws through the helper std::__throw_out_of_range_fmt.
    "helper": "#include <cstddef>\n#include <string_view>\n"
    "std::size_t helper_entry(std::string_view text, std::size_t start) {\n"
    "    return text.substr(start).size();\n}\n",
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
    # The definitions of the names that the other units reference.  The library planted_tree of the root holds them.
    "externs": 'extern "C" int crucible_symbols_extern(int value) { return value; }\n'
    'extern "C" int crucible_symbols_seed() { return 7; }\nextern "C" void crucible_symbols_at_exit() {}\n',
    # The users of a function of a shared library, of an archive of a link line, and of no provider.
    "host_user": 'extern "C" int crucible_host_call(int);\nint host_entry(int value) { return crucible_host_call(value); }\n',
    "static_user": 'extern "C" int crucible_static_call(int);\n'
    "int static_entry(int value) { return crucible_static_call(value); }\n",
    "nowhere": 'extern "C" int crucible_symbols_nowhere(int);\n'
    "int nowhere_entry(int value) { return crucible_symbols_nowhere(value); }\n",
}
# The definitions of the shared library and of the archive of a link line.
LINKED_UNITS = {
    "host_impl": 'extern "C" int crucible_host_call(int value) { return value + 1; }\n',
    "static_impl": 'extern "C" int crucible_static_call(int value) { return value + 2; }\n',
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


def bare(entity: str) -> str:
    """Return the name of an import with no version (THE VERSIONS)."""
    return entity.partition(artifact_symbols.VERSION_MARK)[0]


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
    support = frozenset({"__udivti3"})
    module = artifact_symbols
    for library, symbol, family, wanted in (
        ("foundation", "memcpy", module.RUNTIME, module.LIBC_ALLOW),
        ("foundation", "__udivti3", module.RUNTIME, module.LIBC_ALLOW),
        ("foundation", "getenv", module.RUNTIME, module.LIBC_RATCHET),
        ("foundation", "pthread_create", module.RUNTIME, module.LIBC_RATCHET),
        ("fixy", "pthread_create", module.RUNTIME, module.LIBC_ALLOW),
        ("crucible", "pthread_create", module.RUNTIME, module.LIBC_RATCHET),
        ("crucible", "__cxa_atexit", module.RUNTIME, module.LIBC_ALLOW),
        ("fixy", "__cxa_atexit", module.RUNTIME, module.LIBC_RATCHET),
        ("crucible_vigil", "__cxa_atexit", module.RUNTIME, module.LIBC_ALLOW),
        ("crucible_vigil", "getenv", module.RUNTIME, module.LIBC_RATCHET),
        ("crucible", "__cxa_guard_acquire", module.LIBSTDCXX, module.ALLOW),
        ("crucible", "_ZSt9terminatev", module.LIBSTDCXX, module.RATCHET),
        ("crucible_vigil", "__dso_handle", module.CRT, module.CRT_ALLOW),
        ("fixy", "__dso_handle", module.CRT, module.CRT_RATCHET),
        ("crucible", "data_start", module.CRT, module.CRT_RATCHET),
        ("crucible_perf", "bpf_object__open", module.HOST, module.HOST_RATCHET),
        ("crucible", "_ZNSt12__basic_fileIcED1Ev", module.STATIC, module.STATIC_RATCHET),
    ):
        expect(
            f"an import {symbol} of the family {family} in {library} has the mark {wanted}",
            module.mark_for(library, symbol, family, support) == wanted,
        )
    expect(
        "an import of a shared library has the entity SYMBOL@VERSION, and an import of an archive the name alone",
        module.entity_of("memcpy", "GLIBC_2.14") == "memcpy@GLIBC_2.14" and module.entity_of("atexit", "") == "atexit",
    )
    expect(
        "the names of the linker have no provider",
        module.is_linker_name("_GLOBAL_OFFSET_TABLE_")
        and module.is_linker_name("__start_crucible_probes")
        and not module.is_linker_name("memcpy"),
    )


def provider_cases(expect: testing.Expect) -> None:
    """Hold the providers of the compiler and of a link line, and the owner of each kind of name (THE PROVIDERS)."""
    compiler = runner.plant_compiler()
    expect("ASSAY_TEST_CXX or CRUCIBLE_GCC16_PREFIX names the patched compiler", compiler is not None)
    if compiler is None:
        return
    providers = artifact_symbols.providers_of(compiler, ("-lstdc++exp", "-lm"))
    names = ("memcpy", "atexit", "sqrt", "__udivti3", "_Unwind_Resume", "__dso_handle", "__cxa_guard_acquire")

    def owner(name: str) -> tuple[str, str, str]:
        """Return the family, the file name and the version of the owner of a name, or empty texts."""
        found = providers.owners.get(name)
        return (found.family, found.label, found.versions[name]) if found is not None else ("", "", "")

    owners = {name: owner(name)[:2] for name in names}
    expect(
        "the owner of a name is the first provider in the sequence of a link",
        owners
        == {
            "memcpy": (artifact_symbols.RUNTIME, "libc.so.6"),
            "atexit": (artifact_symbols.RUNTIME, "libc_nonshared.a"),
            "sqrt": (artifact_symbols.RUNTIME, "libm.so.6"),
            "__udivti3": (artifact_symbols.RUNTIME, "libgcc_s.so.1"),
            "_Unwind_Resume": (artifact_symbols.RUNTIME, "libgcc_s.so.1"),
            "__dso_handle": (artifact_symbols.CRT, "crtbegin.o"),
            "__cxa_guard_acquire": (artifact_symbols.LIBSTDCXX, owners["__cxa_guard_acquire"][1]),
        },
    )
    expect(
        "a shared library gives the default version of a name, and an archive no version",
        owner("memcpy")[2] == "GLIBC_2.14" and owner("atexit")[2] == "" and owner("__udivti3")[2].startswith("GCC_"),
    )
    expect("the dynamic loader of libc.so.6 owns __tls_get_addr", owner("__tls_get_addr")[1].startswith("ld-linux"))
    expect(
        "the support library holds the helpers and the unwinder, and no function of the C library",
        {"__udivti3", "_Unwind_Resume"} <= providers.support and "memcpy" not in providers.support,
    )
    expect(
        "a sanitizer runtime owns only a name that no other provider defines",
        owner("__asan_report_load8")[0] == artifact_symbols.SANITIZER and owner("malloc")[1] == "libc.so.6",
    )
    expect(
        "a word -l of a link line gives a provider of the family static for an archive, and -lm gives none",
        any(
            item.family == artifact_symbols.STATIC and item.label == "libstdc++exp.a"
            for item in providers.owners.values()
        )
        and not any(
            item.label.startswith("libm.so") and item.family != artifact_symbols.RUNTIME
            for item in providers.owners.values()
        ),
    )
    said = raised(partial(artifact_symbols.runtime_file, compiler, "libc.so"))
    expect("the linker script libc.so is refused", "linker script" in said)
    said = raised(partial(artifact_symbols.runtime_file, compiler, "libno-such-runtime.so.9"))
    expect("a name that the compiler does not find is refused", "names no file" in said)
    said = raised(partial(artifact_symbols.providers_of, compiler, ("-lno_such_planted_library",)))
    expect("a word -l that the compiler does not find is refused", "finds no libno_such_planted_library.so" in said)
    said = raised(partial(artifact_symbols.providers_of, compiler, ("/no/such/planted/libx.so",)))
    expect("a path of a link line that is not a file is refused", "not a file" in said)


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
        "@build/lib.a | libc-allow | 1 | memcpy | -\n@build/lib.a | libc-ratchet | 1 | getenv | -\n"
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
        wanted |= {reference.symbol for artifact in artifacts.values() for reference in artifact.references}
        wanted |= {symbol for artifact in artifacts.values() for _, symbol in artifact.defined}
        ordered = sorted(wanted)
        names = dict(zip(ordered, elf.demangle(ordered), strict=True))
        ledger = parsed(ADMIT_ROWS)
        rows = list(ledger.admits())
        counted: Counter[str] = Counter()

        def found(unit: str, keys: list[rule_ledger.Key] = rows, is_base: bool = True) -> list[check_report.Finding]:
            """Return the findings of the archive of one unit, as an archive of the base or of a different library."""
            archive = f"lib{unit}.a"
            base = frozenset({archive}) if is_base else frozenset()
            return artifact_symbols.class_findings({archive: artifacts[archive]}, names, keys, counted, base)

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
        helper_symbol = "_ZSt24__throw_out_of_range_fmtPKcz"
        in_base = found("helper")
        expect(
            "a throw helper in an archive of the base is a throw finding in its function",
            [item.kind for item in in_base] == [artifact_symbols.THROW]
            and "std::__throw_out_of_range_fmt(char const*, ...) in helper.o, in helper_entry(" in in_base[0].message,
        )
        expect("a throw helper in a different archive gives no throw finding", found("helper", is_base=False) == [])
        helper_archive = {"libhelper.a": artifacts["libhelper.a"]}
        outside = artifact_symbols.helper_findings(helper_archive, frozenset(), names)
        expect(
            "a throw helper in a different archive is a finding helper-throw with the key of its archive and helper",
            [(item.path, item.kind, item.entity) for item in outside]
            == [("@build/libhelper.a", artifact_symbols.HELPER_THROW, helper_symbol)],
        )
        expect(
            "an archive of the base gives no finding helper-throw",
            artifact_symbols.helper_findings(helper_archive, frozenset({"libhelper.a"}), names) == [],
        )
        expect(
            "the mangled names of the throw helpers and of the throws are recognized",
            artifact_symbols.is_throw_helper(helper_symbol)
            and artifact_symbols.is_throw_helper("_ZSt20__throw_length_errorPKc")
            and not artifact_symbols.is_throw_helper("_ZSt4swapIiEvRT_S1_")
            and not artifact_symbols.is_throw_helper("_ZSt__throw_x")
            and artifact_symbols.is_throw("__cxa_rethrow"),
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


def hidden_cases(expect: testing.Expect) -> None:
    """Plant the records of the library code in objects, and hold the names that each object hides (THE HIDDEN IMPORTS).

    The undefined name getenv is hidden only when a live section of a function or a named variable names it.  No
    record of the object then connects to it.
    """
    compiler = runner.plant_compiler()
    expect("ASSAY_TEST_CXX or CRUCIBLE_GCC16_PREFIX names the patched compiler", compiler is not None)
    if compiler is None:
        return
    hidden: dict[str, set[str]] = {}
    with TemporaryDirectory(prefix="artifact-symbols-hidden-") as scratch_text:
        scratch = Path(scratch_text)
        for unit, (symbols, source) in HIDDEN_FIXTURES.items():
            records = "".join(
                f'    ".ascii \\"library-code symbol {unit}.cpp:1:1 {name}\\\\n\\"\\n"\n' for name in symbols
            )
            (scratch / f"{unit}.cpp").write_text(f"{SECTION_HEAD}{records}{SECTION_END}{source}", encoding="utf-8")
            compiled(compiler, scratch / f"{unit}.cpp", scratch / f"{unit}.o")
            (member,) = elf.members(scratch / f"{unit}.o")
            section = sections.of_member(member)
            expect(f"the unit {unit} holds the planted section", section is not None)
            if section is not None:
                relocations = member.relocations(artifact_symbols.UNREAD_SECTIONS)
                hidden[unit] = artifact_symbols.hidden_names(member, relocations, section.symbols())
    is_hidden = {unit: "getenv" in names for unit, names in hidden.items() if unit != "caught"}
    expect(
        "a record explains a call, a dead function and data that the compiler makes hide nothing, and a call with no "
        "record and a data table of the tree hide getenv",
        is_hidden
        == {
            "recorded": False,
            "unrecorded": True,
            "dead": False,
            "reached": False,
            "table": True,
            "anonymous": False,
        },
    )
    caught = hidden.get("caught", {"(no unit)"})
    expect(
        "the type table of a catch connects to its typeinfo, and the records connect to the calls of the catch",
        not caught & {"_ZTISt9exception", "__cxa_begin_catch", "__cxa_end_catch"},
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
        self.links: list[str] = []
        for unit, source in UNITS.items():
            (self.objects / f"{unit}.cpp").write_text(source, encoding="utf-8")
            compiled(compiler, self.objects / f"{unit}.cpp", self.objects / f"{unit}.o")
        for unit, source in LINKED_UNITS.items():
            (self.objects / f"{unit}.cpp").write_text(source, encoding="utf-8")
        self.host = root / "linked" / "libplantedhost.so"
        self.static = root / "linked" / "libplantedstatic.a"
        self.host.parent.mkdir()
        command = [compiler, "-shared", "-fPIC", str(self.objects / "host_impl.cpp"), "-o", str(self.host)]
        proc.require(proc.run(proc.Command(command, timeout_s=proc.TOOL_S)))
        compiled(compiler, self.objects / "static_impl.cpp", self.objects / "static_impl.o")
        archived(self.static, [self.objects / "static_impl.o"])
        self.units: dict[str, list[str]] = {}
        self.set_kind(KIND)
        self.archive("planted_tree", ["externs"])
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
        targets: list[dict[str, object]] = [
            {
                "name": name,
                "type": runner.STATIC_LIBRARY,
                "artifacts": [{"path": f"lib{name}.a"}],
                "paths": {"source": "."},
            }
            for name in [*self.units, *self.missing]
        ]
        fragments = [{"fragment": item, "role": runner.LIBRARY_ROLE} for item in ["libcrucible.a", *self.links]]
        targets.append(
            {
                "name": "planted_program",
                "type": "EXECUTABLE",
                "artifacts": [{"path": "planted_program"}],
                "paths": {"source": "."},
                "link": {"commandFragments": fragments},
            }
        )
        cmake.plant(self.build, cache=cache, targets=targets, source=str(self.root), build=str(self.build))

    def run(self) -> tuple[int, list[check_report.Finding], str]:
        """Run the rule on the build directory, and return the status, the findings and the written text."""
        status, text = quiet(lambda: artifact_symbols.run(self.rule, runner.ElfContext(self.root, self.build), None))
        return status, [item for item in map(check_report.parse_line, text.splitlines()) if item], text

    def run_rule(self) -> tuple[int, str]:
        """Run the rule through the runner of the engine, and return the status and the written text."""
        return quiet(lambda: runner.run_rule(self.rule, runner.ElfContext(self.root, self.build), None))

    def write(
        self,
        mode: rule_ledger.WriteMode = rule_ledger.WriteMode.LOWER,
        import_log: Path | None = None,
        add_configuration: bool = False,
    ) -> tuple[int, str]:
        """Write the ledger from the build directory or from a log, and return the status and the written text."""
        request = runner.WriteRequest(
            self.root, None if import_log else self.build, mode, None, import_log, add_configuration
        )
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
        return dict(configuration.facts).get(build_config.TIER_KEY, "") if configuration is not None else ""


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
    crucible_rows = {key.entity: key.kind for key in written.expected(KIND) if key.path == "@build/libcrucible.a"}
    expect(
        "memcpy of glibc gets the mark libc-allow with its version, and atexit of libc_nonshared.a the mark "
        "libc-ratchet with no version",
        {bare(entity): kind for entity, kind in crucible_rows.items()}
        == {"memcpy": artifact_symbols.LIBC_ALLOW, "atexit": artifact_symbols.LIBC_RATCHET}
        and any(entity.startswith("memcpy@GLIBC_") for entity in crucible_rows)
        and "atexit" in crucible_rows,
    )
    expect(
        "a reference to a name that a different library of the root defines gives no row",
        "crucible_symbols_extern" not in scratch.ledger_text() and "libplanted_tree.a" not in scratch.ledger_text(),
    )
    status, found, _ = scratch.run()
    expect(
        "an equal build passes, with one warning for each library that keeps symbols",
        status == 0
        and errors_of(found) == []
        and sorted(item.message.split(" keeps")[0] for item in found)
        == ["@build/libcrucible.a", "@build/libfoundation.a"],
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
    guard_rows = {
        bare(key.entity): key.kind for key in scratch.rows().expected(KIND) if key.path == "@build/libfoundation.a"
    }
    expect(
        "a write writes the allowlist symbols without --raise: the mark allow, and libc-allow for the unwinder",
        status == 0
        and [guard_rows.get(name) for name in ("__cxa_guard_acquire", "__gxx_personality_v0", "_Unwind_Resume")]
        == [artifact_symbols.ALLOW, artifact_symbols.ALLOW, artifact_symbols.LIBC_ALLOW],
    )
    expect("after a write the build with the guard passes", scratch.run()[0] == 0)


def ledger_row_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold a row with an incorrect mark, a row of a library that the build does not make, and a different tier."""
    good = scratch.ledger_text()
    scratch.ledger.write_text(
        good.replace(
            "libfoundation.a | allow | 1 | __cxa_guard_acquire@",
            "libfoundation.a | ratchet | 1 | __cxa_guard_acquire@",
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
        "the rows of its name judge a build of a different tier, and one line gives the two tiers",
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

    build_of(OTHER, ["base", "guard", "text"])
    status, found, _ = scratch.run()
    expect(
        "a kind that the ledger does not hold fails on a symbol that no configuration counts",
        status == 1 and errors_of(found) != [] and all("text.o" in message for message in errors_of(found)),
    )
    status, text = scratch.write()
    expect(
        "a write of a kind that the ledger does not hold writes nothing, and names --add-configuration",
        status == 1 and "--add-configuration" in text and scratch.ledger_text() == good,
    )
    build_of(OTHER, ["base"])
    status, _, text = scratch.run()
    mark = rule_ledger.ROW_MARK
    expect(
        "a kind that the ledger does not hold is judged against the largest count of each symbol, passes with fewer "
        "symbols, and prints its configuration rows and its difference rows",
        status == 0
        and f"the largest count of each key over the configurations {KIND}" in text
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
    build_of(KIND, ["base", "guard"])
    scratch.write()
    build_of(OTHER, ["base", "guard", "threads"])
    scratch.write(rule_ledger.WriteMode.RAISE)
    build_of(THIRD, ["base", "guard", "threads"])
    status, found, _ = scratch.run()
    expect(
        "a kind that the ledger does not hold passes with the symbols of an arm that only a different kind counts",
        status == 0 and errors_of(found) == [],
    )
    status, _ = scratch.write(add_configuration=True)
    expect(
        "--add-configuration writes the configuration of a kind that the ledger does not hold",
        status == 0 and [item.name for item in scratch.rows().configurations] == [KIND, OTHER, THIRD],
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
    status, text = scratch.write()
    expect(
        "a write of the base name with other facts writes nothing, and names --add-configuration",
        status == 1 and "--add-configuration" in text and scratch.ledger_text() == good,
    )
    scratch.ledger.write_text(HEAD, encoding="utf-8")
    status, _ = scratch.write(rule_ledger.WriteMode.RAISE)
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
    status, _, _ = scratch.run()
    expect("a build of the UBSan-strict preset is judged as each other build", status == 0)
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


def link_line_cases(expect: testing.Expect, scratch: Scratch) -> None:
    """Hold an import with no provider, an import of a shared library and of an archive of a link line."""
    good = scratch.ledger_text()

    def kinds() -> dict[str, str]:
        """Return the kind of each error of the build by its entity."""
        context = runner.ElfContext(scratch.root, scratch.build)
        return {item.entity: item.kind for item in artifact_symbols.findings(context) if item.level == "error"}

    scratch.archive("crucible", ["glibc", "base", "nowhere"])
    status, _, _ = scratch.run()
    expect(
        "an import that no provider defines is an error of the kind unknown-provider",
        status == 1 and kinds().get("crucible_symbols_nowhere") == artifact_symbols.UNKNOWN_PROVIDER,
    )
    status, _ = scratch.write(rule_ledger.WriteMode.RAISE)
    expect(
        "a write with --raise writes no row for an import with no provider, and the run still fails",
        status == 0 and "crucible_symbols_nowhere" not in scratch.ledger_text() and scratch.run()[0] == 1,
    )
    scratch.archive("crucible", ["glibc", "base", "host_user", "static_user"])
    found = kinds()
    expect(
        "an import of a library that no link line names has no provider",
        [found.get(name) for name in ("crucible_host_call", "crucible_static_call")]
        == [artifact_symbols.UNKNOWN_PROVIDER] * 2,
    )
    scratch.links = [str(scratch.host), str(scratch.static), "-Wl,-rpath,/planted/run"]
    scratch.plant()
    found = kinds()
    expect(
        "the shared library of a link line gives a host-ratchet import, and its archive a static-ratchet import",
        found.get("crucible_host_call") == artifact_symbols.HOST_RATCHET
        and found.get("crucible_static_call") == artifact_symbols.STATIC_RATCHET,
    )
    status, text = scratch.write()
    expect("a write refuses a new host or static import without --raise", status == 1 and "not written" in text)
    status, _ = scratch.write(rule_ledger.WriteMode.RAISE)
    expect(
        "after a write with --raise the build with the link line passes",
        status == 0 and "| host-ratchet | 1 | crucible_host_call |" in scratch.ledger_text() and scratch.run()[0] == 0,
    )
    scratch.links = []
    scratch.ledger.write_text(good, encoding="utf-8")
    scratch.archive("crucible", ["glibc", "base"])


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
        link_line_cases(expect, scratch)


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(
        __name__.removeprefix("assay."),
        argv,
        (class_cases, provider_cases, ledger_cases, artifact_cases, hidden_cases, build_cases),
    )
