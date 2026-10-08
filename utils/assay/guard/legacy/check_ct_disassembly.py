"""check-ct-disassembly: no secret-steered branch, call or indexed load in a compiled constant-time primitive.

The taint tests run under valgrind, which cannot decode AVX-512, so they
build for x86-64-v3.  This guard covers the code that each build of the
tree really emits.  It compiles one wrapper per constant-time primitive of
fixy::ct and per unsigned width, and one wrapper of the static-extent eq per
length of STATIC_EQ_LENGTHS, with the flags of each variant of
BUILD_VARIANTS, for -march=native, x86-64-v3 and the x86-64 baseline.  Then
it reads the disassembly of every wrapper and refuses:

  - a conditional branch (any jcc, jrcxz, loop): its direction would leak
    the operand it tests
  - a call, or a jump to another function: the guard cannot see the code
    it reaches
  - div or idiv: their latency depends on the operands
  - a memory operand with an index register: a load indexed by a secret
    leaks the index through the cache

The dynamic-length eq is not a wrapper here: its loop branches on the
length, which is public, and the guard cannot tell that branch from a
secret one.  The static-extent eq has no loop, so it is a wrapper and
must carry no branch at all.  The taint tests cover the dynamic form.

THE FLAGS
    The guard reads the compile database of the build directory, and it
    takes the code generation flags of the units of the foundation library.
    It removes the flags that change with the kind of build (PER_BUILD), the
    flags that it does not model (DROPPED), and the flags that do not change
    the code.  The rest is the set of flags that each build takes.  Each
    variant of BUILD_VARIANTS adds its own flags and its levels to that set.
    So each run compiles the wrappers with the flags of each kind of build,
    also when a preset of one kind configured the build directory.  A
    Release build adds -fharden-compares.  That flag puts a test and a branch
    to a trap after each compare that gives a value.

    The per-build flags of the build directory, without the level and the
    target, must be the flags of one variant.  So a flag that the tree adds
    to one kind of build fails the guard in a build of that kind until a
    variant has it.  A sanitizer puts a branch and a call at each load, so a
    sanitized build is not the code that ships, and the guard drops its
    flags.  A profile of the tree does not cover the generated wrappers, so
    the guard drops the profile flags too.

Two censuses keep the case list complete.  A reflection walk in the
generated translation unit stops the compile when a namespace of
primitives holds a function this guard does not wrap.  A parse-tree scan
(utils/assay/readers/tsast.py) stops the guard when production code states the
constant-time grade, through role::CtCrypto or through atom::constant_time,
in a file with no case in CTCRYPTO_CASES.  The scan reads name nodes, so a
comment or a string literal that spells the grade is not a statement, and
a name split over two lines still is one.  A namespace alias of fixy::atom
resolves.  A case is a wrapper that the guard compiles and checks with the
primitives.  The map is empty, because no production function states the
grade.

Exit 0: every wrapper is clean.  Exit 1: a finding, printed per wrapper.
Exit 2: the guard could not run (no objdump, no compile database, no row of
a foundation object, no pinned tree-sitter kit, a compile error), which is a
failure, never a pass.
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import re
import shlex
import shutil
import sys
import tempfile
from collections.abc import Iterable, Iterator
from dataclasses import dataclass

from assay.core import proc
from assay.core.paths import REPO_ROOT
from assay.readers import tsast

WIDTHS = (("u8", "std::uint8_t"), ("u16", "std::uint16_t"), ("u32", "std::uint32_t"), ("u64", "std::uint64_t"))
CT_NAMESPACE = "::fixy::ct"
# 8 is the tag length of include/fixy/SipHash.h.  12 has bytes after its
# last word of eight bytes.  16 and 32 are whole vectors of SSE and AVX.
STATIC_EQ_LENGTHS = (8, 12, 16, 32)
MARCHES = ("native", "x86-64-v3", "x86-64")


@dataclass(frozen=True, slots=True)
class Variant:
    """The flags and the levels that one kind of build adds to the flags that each build takes."""

    name: str
    presets: str
    levels: tuple[str, ...]
    flags: tuple[str, ...]


# The kinds of build.  The release variant also compiles at -O2, the level
# of a build that a person can choose for a deployment.
BUILD_VARIANTS: tuple[Variant, ...] = (
    Variant("debug", "default, tsan, ubsan-strict and analyzer", ("-O1",),
            ("-D_GLIBCXX_ASSERTIONS=1", "-D_GLIBCXX_SANITIZE_VECTOR=1")),
    Variant("release", "release, bench, pgo and pgo-release", ("-O2", "-O3"),
            ("-DNDEBUG", "-fharden-compares", "-fharden-conditional-branches",
             "-fcontract-evaluation-semantic=observe")),
    Variant("verify", "verify", ("-O3",),
            ("-fharden-compares", "-fharden-conditional-branches", "-fcontract-evaluation-semantic=enforce")),
)

# The flags whose presence or value changes with the kind of build.  The
# guard removes them from the flags of the compile database, and each
# variant puts its own flags in their place.
PER_BUILD = ("-O", "-march=", "-mtune=", "-mcpu=", "-fharden-", "-fno-harden-", "-fcontract-evaluation-semantic=",
             "-DNDEBUG", "-UNDEBUG", "-D_GLIBCXX_ASSERTIONS", "-D_GLIBCXX_SANITIZE_VECTOR")
# The per-build flags that the guard sweeps with the levels of a variant and with MARCHES.
SWEPT = ("-O", "-march=", "-mtune=", "-mcpu=")
# The flags that the guard does not model: the sanitizers, the profile
# flags, the analyzer, and the define of the quarantine plugin.
DROPPED = ("-fsanitize", "-fno-sanitize", "-fprofile-", "-fno-profile-", "-fbranch-probabilities", "-fauto-profile",
           "-DCRUCIBLE_PGO", "-fanalyzer", "-DCRUCIBLE_QUARANTINE_ACTIVE")
# The words of a compile command that do not change the code.  An option of
# SEPARATE_VALUE takes the next word as its value.
SEPARATE_VALUE = frozenset({"-o", "-c", "-MF", "-MT", "-MQ", "-I", "-isystem", "-iquote", "-include", "-x"})
NOT_CODE_WORDS = frozenset({"-MD", "-MMD"})
NOT_CODE_PREFIXES = ("-W", "-pedantic", "-fdiagnostics-", "-fno-diagnostics-", "-ftemplate-backtrace-limit=",
                     "-fconstexpr-ops-limit=", "-fplugin=", "-fplugin-arg-", "-fdebug-prefix-map=",
                     "-ffile-prefix-map=", "-fmacro-prefix-map=", "-fmessage-length=", "-g", "-I", "-isystem",
                     "-iquote")

# The functions each namespace of primitives may hold.  A new one fails the
# reflection census until it has a wrapper below.
COVERED_NAMES = ("mask_from_bit", "select", "eq", "less", "is_zero", "cswap")

# The production files that state the constant-time grade, each with the
# wrappers that exercise its functions.  A case is (wrapper name, whether
# the wrapper takes a pointer, the C++ definition of an extern "C"
# function of that name).  The guard includes the file, compiles every
# case with the primitives and checks it the same way.  The map is empty:
# no production function states the grade yet.  A new statement stops
# the guard until it has a case here, and an entry with no case, or for a
# file that no longer states the grade, stops it too.
CTCRYPTO_CASES: dict[str, tuple[tuple[str, bool, str], ...]] = {}

# The files that define the role, the atom or the rules that read them.
# A statement there is a definition or a self-test, not a binding.
CTCRYPTO_DEFINITION_FILES = {
    "include/fixy/Atom.h",
    "include/fixy/Collision.h",
    "include/fixy/Role.h",
}

# A binding states the grade through the role or through the atom.  The
# role is a name whose part is CtCrypto.  The atom is a name whose part is
# constant_time after a qualifier that is atom, at, or a namespace alias of
# fixy::atom.
CTCRYPTO_ROLE = "CtCrypto"
CTCRYPTO_ATOM = "constant_time"
CTCRYPTO_ATOM_QUALIFIERS = frozenset({"atom", "at"})
CTCRYPTO_ROOTS = ("include", "src", "vessel")
# The name leaves of the kit's grammar that can spell a grade.
NAME_LEAVES = ("identifier", "type_identifier", "namespace_identifier", "field_identifier")
# The nodes that a name leaf is part of, up to the name that holds it whole.
NAME_OWNERS = frozenset({"template_type", "template_function", "template_method"})

PREFIXES = {"rep", "repz", "repe", "repnz", "repne", "lock", "notrack", "bnd", "data16", "cs", "ds"}


class CannotRun(RuntimeError):
    """The guard cannot run: an input is missing, or a tool failed."""


@dataclass(frozen=True, slots=True)
class Plan:
    """The compiler and the flags that each build takes, from the compile database."""

    compiler: str
    base: tuple[str, ...]

    def targets(self) -> Iterator[tuple[str, list[str]]]:
        """Yield the name and the flags of each compile: each level of each variant, for each target."""
        for variant in BUILD_VARIANTS:
            for level in variant.levels:
                for march in MARCHES:
                    yield f"{variant.name} -march={march} {level}", [*self.base, *variant.flags, level,
                                                                      f"-march={march}"]


def code_flags(words: Iterable[str], source: str) -> list[str]:
    """Return the words of a compile command that change the code, in order, with no compiler and no source."""
    flags: list[str] = []
    words = list(words)
    index = 1
    while index < len(words):
        word = words[index]
        if word in SEPARATE_VALUE:
            index += 2
            continue
        index += 1
        if word == source or word in NOT_CODE_WORDS or word.startswith(NOT_CODE_PREFIXES):
            continue
        flags.append(word)
    return flags


def split_flags(flags: Iterable[str]) -> tuple[tuple[str, ...], frozenset[str]]:
    """Return the flags that each build takes, and the per-build flags that the guard does not sweep."""
    flags = list(flags)
    base = tuple(flag for flag in flags if not flag.startswith(PER_BUILD + DROPPED))
    per_build = frozenset(flag for flag in flags if flag.startswith(PER_BUILD) and not flag.startswith(SWEPT))
    return base, per_build


def flag_problems(bases: list[tuple[str, ...]], per_builds: list[frozenset[str]]) -> list[str]:
    """Return a problem for units that do not agree, and for per-build flags that no variant has.

    Args:
        bases: The flags that each build takes, of each foundation unit
        per_builds: The per-build flags of each foundation unit
    """
    problems: list[str] = []
    if any(base != bases[0] for base in bases) or any(per_build != per_builds[0] for per_build in per_builds):
        problems.append("the foundation units do not have the same code generation flags, so the guard cannot take "
                        "one set from them.  Give each unit of the foundation library the flags of the tree")
    known = [variant for variant in BUILD_VARIANTS if frozenset(variant.flags) == per_builds[0]]
    if not known:
        kinds = "; ".join(f"{variant.name} for {variant.presets}" for variant in BUILD_VARIANTS)
        problems.append(f"the build gives the per-build flags {sorted(per_builds[0])}, and no variant of "
                        f"BUILD_VARIANTS has exactly these flags ({kinds}).  Add the flag to the variant of the kind "
                        f"of build that takes it, or add a variant, so that the guard compiles the wrappers with the "
                        f"flags of this build")
    return problems


def read_plan(build_dir: pathlib.Path, objects: Iterable[pathlib.Path]) -> tuple[Plan, list[str]]:
    """Read the compile database of a build directory, and return the plan and the problems of its flags.

    Complexity: O(rows) for the search of the database.

    Args:
        build_dir: The build directory
        objects: The objects of the foundation library, as absolute paths

    Raises:
        CannotRun: If the database cannot be read, or no row writes one of the objects
    """
    path = build_dir / "compile_commands.json"
    try:
        rows = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise CannotRun(f"the compile database {path} cannot be read: {error}") from None
    wanted = {pathlib.Path(os.path.normpath(obj)) for obj in objects}
    commands: dict[pathlib.Path, tuple[list[str], str]] = {}
    for row in rows:
        directory = row.get("directory", str(build_dir))
        output = row.get("output")
        if output is None:
            continue
        written = pathlib.Path(os.path.normpath(os.path.join(directory, output)))
        if written in wanted:
            commands[written] = (row.get("arguments") or shlex.split(row.get("command", "")), row.get("file", ""))
    missing = sorted(str(obj) for obj in wanted - commands.keys())
    if not wanted or missing:
        raise CannotRun(f"no row of the compile database {path} writes the foundation objects {missing}")
    ordered = [commands[obj] for obj in sorted(commands)]
    splits = [split_flags(code_flags(words, source)) for words, source in ordered]
    problems = flag_problems([base for base, _ in splits], [per_build for _, per_build in splits])
    return Plan(ordered[0][0][0], splits[0][0]), problems


def census_prelude() -> str:
    """Return the reflection census: true when a namespace holds only covered functions."""
    covered = ", ".join(f'"{n}"' for n in COVERED_NAMES)
    return "\n".join([
        "#include <meta>",
        "#include <string_view>",
        "namespace ct_disassembly {",
        "consteval bool is_covered(std::string_view name) {",
        f"    constexpr std::string_view covered[] = {{{covered}}};",
        "    for (std::string_view c : covered) if (c == name) return true;",
        "    return false;",
        "}",
        "consteval bool covers_every_primitive(std::meta::info ns) {",
        "    for (std::meta::info m : std::meta::members_of(ns, std::meta::access_context::unchecked())) {",
        "        if (!std::meta::is_function(m) && !std::meta::is_function_template(m)) continue;",
        "        if (!std::meta::has_identifier(m) || !is_covered(std::meta::identifier_of(m))) return false;",
        "    }",
        "    return true;",
        "}",
        "}  // namespace ct_disassembly",
    ])


def wrapper_source() -> tuple[str, dict[str, bool]]:
    """Return the generated translation unit and {wrapper name: takes pointers}."""
    names: dict[str, bool] = {}
    ns = CT_NAMESPACE
    lines = [
        "#include <fixy/ConstantTime.h>",
        "#include <cstddef>",
        "#include <cstdint>",
        census_prelude(),
        f"static_assert(ct_disassembly::covers_every_primitive(^^{ns}), \"{ns} holds a function that "
        "the rule ct-disassembly (guard check --rule ct-disassembly) does not wrap. Add its wrappers and its name "
        "to COVERED_NAMES, "
        "so the compiled code of the new primitive is checked for branches.\");",
    ]
    for tn, ty in WIDTHS:
        specs = (
            ("mask_from_bit", f"{ty} b", f"return {ns}::mask_from_bit<{ty}>(b);", ty),
            ("select", f"{ty} b, {ty} x, {ty} y", f"return {ns}::select<{ty}>(b, x, y);", ty),
            ("less", f"{ty} x, {ty} y", f"return {ns}::less<{ty}>(x, y);", ty),
            ("is_zero", f"{ty} x", f"return {ns}::is_zero<{ty}>(x);", ty),
            ("cswap", f"{ty} c, {ty}* x, {ty}* y", f"{ns}::cswap<{ty}>(c, *x, *y);", "void"),
        )
        for fn, params, body, ret in specs:
            name = f"ct_fixy_{fn}_{tn}"
            names[name] = "*" in params
            lines.append(f'extern "C" {ret} {name}({params}) noexcept {{ {body} }}')
    for n in STATIC_EQ_LENGTHS:
        name = f"ct_fixy_eq_static_{n}"
        names[name] = True
        lines.append(
            f'extern "C" bool {name}(const std::byte* a, const std::byte* b) noexcept {{ '
            f"return {ns}::eq(std::span<const std::byte, {n}>{{a, {n}}}, "
            f"std::span<const std::byte, {n}>{{b, {n}}}); }}"
        )
    for rel, file_cases in CTCRYPTO_CASES.items():
        # A header is included here.  A case for a source file includes
        # the headers it needs in its own definition.
        if rel.startswith("include/"):
            lines.append(f"#include <{pathlib.PurePosixPath(rel).relative_to('include')}>")
        for name, takes_pointers, definition in file_cases:
            names[name] = takes_pointers
            lines.append(definition)
    return "\n".join(lines) + "\n", names


def compile_object(compiler: str, flags: list[str], source: pathlib.Path, obj: pathlib.Path, target: str) -> None:
    """Compile `source` to `obj` with the flags of one target.

    Raises:
        CannotRun: If the compile fails
    """
    cmd = [compiler, *flags, "-fno-asynchronous-unwind-tables", f"-I{REPO_ROOT / 'include'}",
           "-c", str(source), "-o", str(obj)]
    result = proc.run(proc.Command(cmd, timeout_s=proc.TOOL_S))
    if result.exit != 0:
        raise CannotRun(f"the compile failed for {target}:\n{shlex.join(cmd)}\n{result.err}")


class Insn:
    """One disassembled instruction, with the symbol its relocation names, if any."""

    def __init__(self, mnemonic: str, operands: str) -> None:
        self.mnemonic = mnemonic
        self.operands = operands
        self.relocation: str | None = None

    def text(self) -> str:
        """Return the instruction as objdump prints it, plus its relocation."""
        base = f"{self.mnemonic} {self.operands}".strip()
        return f"{base} (reloc {self.relocation})" if self.relocation else base


def disassemble(obj: pathlib.Path) -> dict[str, list[Insn]]:
    """Return {function: [instruction]} for every function in `obj`, relocations attached.

    Raises:
        CannotRun: If objdump fails
    """
    result = proc.run(proc.Command(["objdump", "-dr", "--no-show-raw-insn", "-M", "intel", str(obj)],
                                   timeout_s=proc.TOOL_S))
    if result.exit != 0:
        raise CannotRun(f"objdump failed:\n{result.err}")
    functions: dict[str, list[Insn]] = {}
    current: str | None = None
    head = re.compile(r"^[0-9a-f]+ <([^>]+)>:$")
    reloc = re.compile(r"^\s+[0-9a-f]+:\s+R_\w+\s+(\S+)")
    insn = re.compile(r"^\s+[0-9a-f]+:\s+(.*)$")
    for line in result.out.splitlines():
        match = head.match(line)
        if match:
            current = match.group(1)
            functions[current] = []
            continue
        if current is None:
            continue
        match = reloc.match(line)
        if match:
            if functions[current]:
                functions[current][-1].relocation = re.split(r"[-+]", match.group(1))[0]
            continue
        match = insn.match(line)
        if match:
            tokens = match.group(1).split()
            while tokens and tokens[0] in PREFIXES:
                tokens = tokens[1:]
            if tokens:
                functions[current].append(Insn(tokens[0], " ".join(tokens[1:])))
    return functions


ADDRESS = re.compile(r"\[([^\]]*)\]")
REGISTER = re.compile(r"\b(r[a-z0-9]+|e[a-z]{2}|[a-z]{2}l|[a-z]{2})\b")


def findings_of(name: str, body: list[Insn], takes_pointers: bool) -> list[str]:
    """Return every refused instruction of one function, as text.

    A function that takes no pointer has no public address to read, so
    any memory operand built from a general register reads an address
    that a secret may have formed.  A function that takes pointers may
    read through them, but not with an index register.
    """
    out: list[str] = []
    for ins in body:
        m = ins.mnemonic
        text = ins.text()
        if m == "nop" or m.startswith("nop"):
            continue
        if (m.startswith("j") and m != "jmp") or m.startswith("loop"):
            out.append(f"conditional branch: {text}")
        elif m == "call":
            out.append(f"call: {text}")
        elif m == "jmp" and ins.relocation is not None and ins.relocation != name:
            out.append(f"jump out of the function: {text}")
        elif m in ("div", "idiv"):
            out.append(f"operand-dependent latency: {text}")
        # lea computes an address and reads no memory, so it is arithmetic.
        if m == "lea":
            continue
        for address in ADDRESS.findall(ins.operands):
            registers = {r for r in REGISTER.findall(address) if r not in ("rsp", "rip", "ptr")}
            if "*" in address:
                out.append(f"indexed load or store: {text}")
            elif registers and not takes_pointers:
                out.append(f"load or store through a computed address: {text}")
    return out


def check_object(plan: Plan, source: pathlib.Path, expected: dict[str, bool], workdir: pathlib.Path) -> list[str]:
    """Compile `source` for every target of the plan and return the findings, each tagged with its target.

    The compiles of the targets run at the same time, one thread for each.

    Raises:
        CannotRun: If a compile or objdump fails, or a target emits no code for a wrapper
    """
    from concurrent.futures import ThreadPoolExecutor

    targets = list(plan.targets())

    def one_target(index: int) -> list[str]:
        target, flags = targets[index]
        obj = workdir / f"ct_{index}.o"
        compile_object(plan.compiler, flags, source, obj, target)
        functions = disassemble(obj)
        missing = [n for n in expected if n not in functions]
        if missing:
            raise CannotRun(f"{target} emitted no code for {missing}")
        return [f"{target} {name}: {finding}" for name, takes_pointers in expected.items()
                for finding in findings_of(name, functions[name], takes_pointers)]

    with ThreadPoolExecutor(max_workers=len(targets)) as pool:
        return [finding for found in pool.map(one_target, range(len(targets))) for finding in found]


def whole_name(leaf: tsast.Node) -> tsast.Node:
    """Return the name that a name leaf is part of: its template name, then the qualified name around it.

    A leaf inside the template arguments of a name is a name of its own, so
    the walk stops at an argument list.
    """
    node = leaf
    while node.parent is not None and (node.parent.type == "qualified_identifier"
                                       or (node.parent.type in NAME_OWNERS and node.field == "name")):
        node = node.parent
    return node


def states_grade(name: tsast.Node, aliases: list) -> bool:
    """Say whether a whole name states the constant-time grade through the role or the atom."""
    qualified = tsast.qualified_parts(name)
    if qualified is None:
        return False
    is_global, parts = qualified
    for index, part in enumerate(parts):
        before = parts[:index]
        if part == CTCRYPTO_ROLE:
            return True
        if part == CTCRYPTO_ATOM and before:
            resolved = tsast.resolve_namespace(before, name, aliases, is_global=is_global)
            if before[-1] in CTCRYPTO_ATOM_QUALIFIERS or resolved[-1:] == ("atom",):
                return True
    return False


def ctcrypto_statements(repo: pathlib.Path) -> tuple[dict[str, list[str]], list[str]]:
    """Map each production file under `repo` to the names in it that state the constant-time grade.

    The scan parses a file only when its bytes contain a spelling of the
    grade, because no name node can spell it otherwise.  Each whole name
    counts once.  Also return each such file that the kit cannot parse,
    because the scan cannot read it.  O(bytes of the scanned trees) for the
    byte filter, and O(size of each file parsed).

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    candidates: list[pathlib.Path] = []
    for root in CTCRYPTO_ROOTS:
        for path in sorted((repo / root).rglob("*")):
            rel = path.relative_to(repo).as_posix()
            if not path.is_file() or not tsast.is_in_cpp_scope(rel) or rel in CTCRYPTO_DEFINITION_FILES:
                continue
            data = path.read_bytes()
            if CTCRYPTO_ROLE.encode() in data or CTCRYPTO_ATOM.encode() in data:
                candidates.append(path)
    statements: dict[str, list[str]] = {}
    unreadable: list[str] = []
    for tree in tsast.parse(candidates, strict=False):
        rel = pathlib.Path(tree.path).resolve().relative_to(repo.resolve()).as_posix()
        if tree.diagnostic is not None:
            unreadable.append(rel)
            continue
        aliases = tsast.namespace_aliases(tree)
        seen: set[int] = set()
        for leaf in tree.find(*NAME_LEAVES):
            if leaf.text not in (CTCRYPTO_ROLE, CTCRYPTO_ATOM):
                continue
            name = whole_name(leaf)
            if name.index in seen or not states_grade(name, aliases):
                continue
            seen.add(name.index)
            statements.setdefault(rel, []).append(f"{rel}:{name.line}: {tsast.spelled(name)}")
    return statements, unreadable


def ctcrypto_problems(repo: pathlib.Path, cases: dict[str, tuple[tuple[str, bool, str], ...]]) -> list[str]:
    """Return each statement of the grade with no case, each case entry that checks nothing, and each
    candidate file that the kit cannot parse."""
    statements, unreadable = ctcrypto_statements(repo)
    problems = [
        f"a production binding states the constant-time grade and has no disassembly case: {line}"
        for rel, lines in statements.items()
        if rel not in cases
        for line in lines
    ]
    problems += [f"the tree-sitter kit cannot parse {rel}, so the census cannot read it" for rel in unreadable]
    for rel, file_cases in cases.items():
        if not file_cases:
            problems.append(f"the case entry for {rel} holds no wrapper, so it checks nothing")
        if rel not in statements:
            problems.append(f"the case entry for {rel} names a file that states no constant-time grade")
    return problems


def run_guard(plan: Plan, problems: list[str]) -> int:
    """Check every wrapper with the plan, and report the problems of the flags too; return the exit code.

    Raises:
        CannotRun: If a compile or objdump fails
    """
    source_text, names = wrapper_source()
    with tempfile.TemporaryDirectory(prefix="ct-disassembly-") as tmp:
        workdir = pathlib.Path(tmp)
        source = workdir / "ct_wrappers.cpp"
        source.write_text(source_text)
        findings = check_object(plan, source, names, workdir)
    problems = problems + ctcrypto_problems(REPO_ROOT, CTCRYPTO_CASES)
    for line in findings + problems:
        print(f"check-ct-disassembly: {line}")
    if findings or problems:
        print(f"check-ct-disassembly: FAIL, {len(findings)} findings, {len(problems)} flag or case-list problems")
        return 1
    print(f"check-ct-disassembly: clean, {len(names)} wrappers x {len(list(plan.targets()))} targets")
    return 0


PLANTED = r"""
#include <cstdint>
extern "C" std::uint32_t external_sink(std::uint32_t) noexcept;
extern "C" std::uint32_t ct_plant_branch(std::uint32_t b, std::uint32_t x) noexcept {
    if (b & 1u) __builtin_trap();
    return x;
}
extern "C" std::uint8_t ct_plant_index(std::uint32_t x) noexcept {
    static const std::uint8_t table[256] = {1, 2, 3, 4, 5};
    return table[x & 255u];
}
extern "C" std::uint32_t ct_plant_call(std::uint32_t x) noexcept { return external_sink(x) + 1u; }
extern "C" std::uint32_t ct_plant_divide(std::uint32_t x, std::uint32_t y) noexcept { return x / (y | 1u); }
extern "C" bool ct_plant_hardened(std::uint8_t x) noexcept { return x == 0u; }
extern "C" std::uint32_t ct_plant_clean(std::uint32_t x, std::uint32_t y) noexcept { return (x * 5u + y) ^ 0xFFu; }
"""

# The variants whose flags put a branch to a trap after the compare of ct_plant_hardened.
HARDENED_VARIANTS = frozenset({"release", "verify"})


def self_test_case_list() -> int:
    """Plant bindings of the grade in a scratch tree; return the number of wrong verdicts of the scan."""
    failures = 0
    binding = "using Seal = ::fixy::fn<int, ::fixy::atom::with<>, ::fixy::atom::constant_time>;\n"
    role = "using Mac = ::fixy::role::CtCrypto<int>;\n"
    comment = "// role::CtCrypto and atom::constant_time, named in a comment only\n"
    block_comment = "/* role::CtCrypto */ int plain = 0;\n"
    literal = 'inline constexpr char spelled[] = "role::CtCrypto and atom::constant_time";\n'
    split = "using Seal = ::fixy::fn<int, ::fixy::atom::\n    constant_time>;\n"
    aliased = "namespace grade = ::fixy::atom;\nusing Seal = ::fixy::fn<int, grade::constant_time>;\n"
    wrapper = ("ct_case_seal", False, 'extern "C" int ct_case_seal(int x) noexcept { return x; }')
    trials = (
        ("an atom binding with no case", {"include/a.h": binding}, {}, 1),
        ("a role binding with no case", {"src/b.cpp": role}, {}, 1),
        ("a comment only", {"include/c.h": comment}, {}, 0),
        ("a block comment only", {"include/c.h": block_comment}, {}, 0),
        ("a string literal only", {"include/c.h": literal}, {}, 0),
        ("an atom binding split over two lines", {"include/a.h": split}, {}, 1),
        ("an atom binding through a namespace alias", {"include/a.h": aliased}, {}, 1),
        ("a binding with a case", {"include/a.h": binding}, {"include/a.h": (wrapper,)}, 0),
        ("a case entry with no wrapper", {"include/a.h": binding}, {"include/a.h": ()}, 1),
        ("a case entry for a file with no binding", {"include/c.h": comment}, {"include/c.h": (wrapper,)}, 1),
        ("a binding in a definition file", {"include/fixy/Role.h": role}, {}, 0),
    )
    for title, files, cases, expected in trials:
        with tempfile.TemporaryDirectory(prefix="ct-disassembly-scan-") as tmp:
            root = pathlib.Path(tmp)
            for rel, text in files.items():
                (root / rel).parent.mkdir(parents=True, exist_ok=True)
                (root / rel).write_text(text)
            problems = ctcrypto_problems(root, cases)
        if len(problems) != expected:
            print(f"self-test: the case-list scan over {title} gave {len(problems)} problems, not {expected}: {problems}")
            failures += 1
    return failures


def self_test_flags() -> int:
    """Plant the flags of foundation units for each kind of flag problem; return the number of wrong verdicts."""
    common = ["g++", "-std=c++26", "-fPIC", "-fno-plt", "-Wall", "-Iinclude", "-fplugin=q.so", "-o", "u.o", "-c",
              "u.cpp"]
    debug = common + ["-O1", "-D_GLIBCXX_ASSERTIONS=1", "-D_GLIBCXX_SANITIZE_VECTOR=1", "-fsanitize=address"]
    release = common + ["-O1", "-march=native", "-DNDEBUG", "-O3", "-DNDEBUG", "-fharden-compares",
                        "-fharden-conditional-branches", "-fcontract-evaluation-semantic=observe"]
    verify = common + ["-O3", "-march=native", "-fharden-compares", "-fharden-conditional-branches",
                       "-fcontract-evaluation-semantic=enforce"]
    profiled = release + ["-fprofile-use=p", "-DCRUCIBLE_PGO=1"]
    cases: tuple[tuple[str, list[list[str]], int], ...] = (
        ("a Debug build with a sanitizer", [debug, debug], 0),
        ("a Release build", [release, release], 0),
        ("a verify build", [verify], 0),
        ("a Release build with a profile", [profiled], 0),
        ("a Release build with a new hardening flag", [release + ["-fharden-control-flow-redundancy"]], 1),
        ("a Release build with no hardening", [[word for word in release if not word.startswith("-fharden-")]], 1),
        ("two units with different base flags", [release, release + ["-fno-common"]], 1),
    )
    failures = 0
    for title, commands, expected in cases:
        splits = [split_flags(code_flags(words, "u.cpp")) for words in commands]
        problems = flag_problems([base for base, _ in splits], [per_build for _, per_build in splits])
        if len(problems) != expected:
            print(f"self-test: the flag case '{title}' gave {len(problems)} problems, not {expected}: {problems}")
            failures += 1
    base, _ = split_flags(code_flags(release, "u.cpp"))
    if base != ("-std=c++26", "-fPIC", "-fno-plt"):
        print(f"self-test: the flags that each build takes are {base}, and the expectation is -std=c++26 -fPIC -fno-plt")
        failures += 1
    return failures


def run_self_test(plan: Plan) -> int:
    """Plant one violation of each kind beside a clean function; each must be found, the clean one must pass.

    Raises:
        CannotRun: If a compile or objdump fails
    """
    # A table read through a position-independent address is an indexed
    # load, and a table read through an absolute address is a computed one.
    expect_flagged = {
        "ct_plant_branch": ("conditional branch",),
        "ct_plant_index": ("computed address", "indexed load"),
        "ct_plant_call": ("call",),
        "ct_plant_divide": ("operand-dependent latency",),
    }
    with tempfile.TemporaryDirectory(prefix="ct-disassembly-self-") as tmp:
        workdir = pathlib.Path(tmp)
        source = workdir / "planted.cpp"
        source.write_text(PLANTED)
        names = {name: False for name in expect_flagged} | {"ct_plant_hardened": False, "ct_plant_clean": False}
        findings = check_object(plan, source, names, workdir)
    failures = 0
    for target, _ in plan.targets():
        for name, kinds in expect_flagged.items():
            if not any(f.startswith(f"{target} {name}: ") and any(kind in f for kind in kinds) for f in findings):
                print(f"self-test: {name} must be flagged for one of {list(kinds)} at {target}")
                failures += 1
        # A compare that gives a value has a branch only under the flags of
        # a hardened variant, so this plant tells the flags of a variant apart.
        is_hardened = target.split()[0] in HARDENED_VARIANTS
        is_flagged = any(f.startswith(f"{target} ct_plant_hardened: conditional branch") for f in findings)
        if is_flagged != is_hardened:
            print(f"self-test: ct_plant_hardened {'must' if is_hardened else 'must not'} be flagged for a "
                  f"conditional branch at {target}")
            failures += 1
    clean_hits = [f for f in findings if " ct_plant_clean: " in f]
    if clean_hits:
        print(f"self-test: the clean function was flagged: {clean_hits}")
        failures += 1
    failures += self_test_flags()
    source_text, names = wrapper_source()
    if len(names) != len(WIDTHS) * 5 + len(STATIC_EQ_LENGTHS):
        print("self-test: the wrapper list lost a primitive")
        failures += 1
    # The census must stop a compile when a namespace of primitives gains
    # a function with no wrapper, and must pass one that holds only
    # covered names.
    for fake, body, must_fail in (
        ("fake_ct_unwrapped", "inline unsigned leak(unsigned x) { return x; }", True),
        ("fake_ct_covered", "inline unsigned select(unsigned x) { return x; }", False),
    ):
        tu = (f"{census_prelude()}\nnamespace {fake} {{ {body} }}\n"
              f"static_assert(ct_disassembly::covers_every_primitive(^^{fake}), \"census fired\");\n")
        with tempfile.TemporaryDirectory(prefix="ct-disassembly-census-") as tmp:
            source = pathlib.Path(tmp) / "census.cpp"
            source.write_text(tu)
            result = proc.run(proc.Command([plan.compiler, "-std=c++26", "-freflection", "-fsyntax-only", str(source)],
                                           timeout_s=proc.TOOL_S))
        fired = result.exit != 0 and "census fired" in result.err
        if fired != must_fail:
            print(f"self-test: the census over {fake} {'did not fire' if must_fail else 'fired'}: {result.err[:400]}")
            failures += 1
    failures += self_test_case_list()
    print("self-test:", "PASS" if failures == 0 else f"FAIL ({failures})")
    return 0 if failures == 0 else 1


def main(argv: list[str]) -> int:
    """Parse the arguments and run the guard or its self-test.

    Args:
        argv: The words after the rule ID

    Returns:
        The exit status
    """
    parser = argparse.ArgumentParser(prog="guard check --rule ct-disassembly", description=__doc__.splitlines()[0])
    parser.add_argument("--build-dir", type=pathlib.Path, required=True,
                        help="the build directory, with compile_commands.json")
    parser.add_argument("--base-objects", type=pathlib.Path, nargs="+", required=True,
                        help="the objects of the foundation library, whose compile commands give the flags")
    parser.add_argument("--self-test", action="store_true", help="check the checker on planted code")
    args = parser.parse_args(argv)
    if shutil.which("objdump") is None:
        sys.stderr.write("check-ct-disassembly: objdump is missing\n")
        return 2
    try:
        plan, problems = read_plan(pathlib.Path(os.path.abspath(args.build_dir)),
                                   [pathlib.Path(os.path.abspath(obj)) for obj in args.base_objects])
        if shutil.which(plan.compiler) is None and not pathlib.Path(plan.compiler).exists():
            raise CannotRun(f"the compile database names the compiler {plan.compiler}, and it does not exist")
        return run_self_test(plan) if args.self_test else run_guard(plan, problems)
    except CannotRun as exc:
        sys.stderr.write(f"check-ct-disassembly: the guard cannot run: {exc}\n")
        return 2
    except tsast.KitMissing as exc:
        sys.stderr.write(f"check-ct-disassembly: the census cannot run: {exc}\n")
        return 2
