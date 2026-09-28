#!/usr/bin/env python3
"""check-ct-disassembly.py: no secret-steered branch, call or indexed load in a compiled constant-time primitive.

The taint tests run under valgrind, which cannot decode AVX-512, so they
build for x86-64-v3.  This guard covers the code that the release build
really emits.  It compiles one wrapper per constant-time primitive of
fixy::ct and per unsigned width, at -O2 and -O3, for -march=native,
x86-64-v3 and the x86-64 baseline.  Then it reads the disassembly of every
wrapper and refuses:

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

Two censuses keep the case list complete.  A reflection walk in the
generated translation unit stops the compile when a namespace of
primitives holds a function this guard does not wrap.  A parse-tree scan
(scripts/tsast.py) stops the guard when production code states the
constant-time grade, through role::CtCrypto or through atom::constant_time,
in a file with no case in CTCRYPTO_CASES.  The scan reads name nodes, so a
comment or a string literal that spells the grade is not a statement, and
a name split over two lines still is one.  A namespace alias of fixy::atom
resolves.  A case is a wrapper that the guard compiles and checks with the
primitives.  The map is empty, because no production function states the
grade.

Exit 0: every wrapper is clean.  Exit 1: a finding, printed per wrapper.
Exit 2: the guard could not run (no compiler, no objdump, no pinned
tree-sitter kit, a compile error), which is a failure, never a pass.
"""

from __future__ import annotations

import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

REPO = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "scripts"))

import tsast  # noqa: E402

WIDTHS = (("u8", "std::uint8_t"), ("u16", "std::uint16_t"), ("u32", "std::uint32_t"), ("u64", "std::uint64_t"))
CT_NAMESPACE = "::fixy::ct"
STATIC_EQ_LENGTHS = (16, 32)
MARCHES = ("native", "x86-64-v3", "x86-64")
OPTS = ("-O2", "-O3")

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
    "include/crucible/fixy/_Fn.h",
}

# A binding states the grade through the role or through the atom.  The
# role is a name whose part is CtCrypto, unless the part before it is the
# old tree's stance alias, which carries no binding.  The atom is a name
# whose part is constant_time after a qualifier that is atom, at, or a
# namespace alias of fixy::atom.
CTCRYPTO_ROLE = "CtCrypto"
CTCRYPTO_STANCE = "stance"
CTCRYPTO_ATOM = "constant_time"
CTCRYPTO_ATOM_QUALIFIERS = frozenset({"atom", "at"})
CTCRYPTO_ROOTS = ("include", "src", "vessel")
# The name leaves of the kit's grammar that can spell a grade.
NAME_LEAVES = ("identifier", "type_identifier", "namespace_identifier", "field_identifier")
# The nodes that a name leaf is part of, up to the name that holds it whole.
NAME_OWNERS = frozenset({"template_type", "template_function", "template_method"})

PREFIXES = {"rep", "repz", "repe", "repnz", "repne", "lock", "notrack", "bnd", "data16", "cs", "ds"}


def default_compiler() -> str:
    """Return the GCC 16 driver that the toolchain file selects."""
    if os.environ.get("CRUCIBLE_CXX"):
        return os.environ["CRUCIBLE_CXX"]
    prefix = pathlib.Path(os.environ.get("CRUCIBLE_GCC16_PREFIX", pathlib.Path.home() / ".local/gcc16-patched"))
    for name in ("g++-16p", "g++-16", "g++"):
        candidate = prefix / "usr/bin" / name
        if candidate.exists():
            return str(candidate)
    return "g++"


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
        "scripts/check-ct-disassembly.py does not wrap. Add its wrappers and its name to COVERED_NAMES, "
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


def compile_object(cxx: str, source: pathlib.Path, obj: pathlib.Path, march: str, opt: str) -> None:
    """Compile `source` to `obj`; stop the guard with exit 2 on a compile error."""
    cmd = [cxx, "-std=c++26", "-freflection", "-fcontracts", "-fcontract-evaluation-semantic=observe", "-DNDEBUG",
           opt, f"-march={march}", "-fno-asynchronous-unwind-tables", f"-I{REPO / 'include'}",
           "-c", str(source), "-o", str(obj)]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(f"check-ct-disassembly: the compile failed for -march={march} {opt}:\n{result.stderr}")
        sys.exit(2)


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
    """Return {function: [instruction]} for every function in `obj`, relocations attached."""
    result = subprocess.run(["objdump", "-dr", "--no-show-raw-insn", "-M", "intel", str(obj)],
                            capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(f"check-ct-disassembly: objdump failed:\n{result.stderr}")
        sys.exit(2)
    functions: dict[str, list[Insn]] = {}
    current: str | None = None
    head = re.compile(r"^[0-9a-f]+ <([^>]+)>:$")
    reloc = re.compile(r"^\s+[0-9a-f]+:\s+R_\w+\s+(\S+)")
    insn = re.compile(r"^\s+[0-9a-f]+:\s+(.*)$")
    for line in result.stdout.splitlines():
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


def check_object(cxx: str, source: pathlib.Path, expected: dict[str, bool], workdir: pathlib.Path) -> list[str]:
    """Compile `source` for every target and return the findings, each tagged with its target."""
    findings: list[str] = []
    for march in MARCHES:
        for opt in OPTS:
            obj = workdir / f"ct_{march}_{opt[1:]}.o"
            compile_object(cxx, source, obj, march, opt)
            functions = disassemble(obj)
            missing = [n for n in expected if n not in functions]
            if missing:
                sys.stderr.write(f"check-ct-disassembly: -march={march} {opt} emitted no code for {missing}\n")
                sys.exit(2)
            for name, takes_pointers in expected.items():
                for finding in findings_of(name, functions[name], takes_pointers):
                    findings.append(f"-march={march} {opt} {name}: {finding}")
    return findings


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
        if part == CTCRYPTO_ROLE and before[-1:] != (CTCRYPTO_STANCE,):
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


def run_guard(cxx: str) -> int:
    """Check every wrapper; return the exit code."""
    source_text, names = wrapper_source()
    with tempfile.TemporaryDirectory(prefix="ct-disassembly-") as tmp:
        workdir = pathlib.Path(tmp)
        source = workdir / "ct_wrappers.cpp"
        source.write_text(source_text)
        findings = check_object(cxx, source, names, workdir)
    problems = ctcrypto_problems(REPO, CTCRYPTO_CASES)
    for line in findings + problems:
        print(f"check-ct-disassembly: {line}")
    if findings or problems:
        print(f"check-ct-disassembly: FAIL, {len(findings)} findings, {len(problems)} case-list problems")
        return 1
    print(f"check-ct-disassembly: clean, {len(names)} wrappers x {len(MARCHES) * len(OPTS)} targets")
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
extern "C" std::uint32_t ct_plant_clean(std::uint32_t x, std::uint32_t y) noexcept { return (x * 5u + y) ^ 0xFFu; }
"""


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
    stance = "using Old = ::crucible::safety::stance::CtCrypto;\n"
    wrapper = ("ct_case_seal", False, 'extern "C" int ct_case_seal(int x) noexcept { return x; }')
    trials = (
        ("an atom binding with no case", {"include/a.h": binding}, {}, 1),
        ("a role binding with no case", {"src/b.cpp": role}, {}, 1),
        ("a comment only", {"include/c.h": comment}, {}, 0),
        ("a block comment only", {"include/c.h": block_comment}, {}, 0),
        ("a string literal only", {"include/c.h": literal}, {}, 0),
        ("an atom binding split over two lines", {"include/a.h": split}, {}, 1),
        ("an atom binding through a namespace alias", {"include/a.h": aliased}, {}, 1),
        ("the old stance alias", {"include/c.h": stance}, {}, 0),
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


def run_self_test(cxx: str) -> int:
    """Plant one violation of each kind beside a clean function; each must be found, the clean one must pass."""
    expect_flagged = {
        "ct_plant_branch": "conditional branch",
        "ct_plant_index": "computed address",
        "ct_plant_call": "call",
        "ct_plant_divide": "operand-dependent latency",
    }
    with tempfile.TemporaryDirectory(prefix="ct-disassembly-self-") as tmp:
        workdir = pathlib.Path(tmp)
        source = workdir / "planted.cpp"
        source.write_text(PLANTED)
        names = {name: False for name in expect_flagged} | {"ct_plant_clean": False}
        findings = check_object(cxx, source, names, workdir)
    failures = 0
    for name, kind in expect_flagged.items():
        for march in MARCHES:
            for opt in OPTS:
                tag = f"-march={march} {opt} {name}: "
                if not any(f.startswith(tag) and kind in f for f in findings):
                    print(f"self-test: {name} must be flagged for '{kind}' at -march={march} {opt}")
                    failures += 1
    clean_hits = [f for f in findings if " ct_plant_clean: " in f]
    if clean_hits:
        print(f"self-test: the clean function was flagged: {clean_hits}")
        failures += 1
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
            result = subprocess.run([cxx, "-std=c++26", "-freflection", "-fsyntax-only", str(source)],
                                    capture_output=True, text=True)
        fired = result.returncode != 0 and "census fired" in result.stderr
        if fired != must_fail:
            print(f"self-test: the census over {fake} {'did not fire' if must_fail else 'fired'}: {result.stderr[:400]}")
            failures += 1
    failures += self_test_case_list()
    print("self-test:", "PASS" if failures == 0 else f"FAIL ({failures})")
    return 0 if failures == 0 else 1


def main() -> int:
    """Parse the arguments and run the guard or its self-test."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--cxx", default=default_compiler(), help="the GCC 16 driver")
    parser.add_argument("--self-test", action="store_true", help="check the checker on planted code")
    args = parser.parse_args()
    if shutil.which(args.cxx) is None and not pathlib.Path(args.cxx).exists():
        sys.stderr.write(f"check-ct-disassembly: no compiler at {args.cxx}\n")
        return 2
    if shutil.which("objdump") is None:
        sys.stderr.write("check-ct-disassembly: objdump is missing\n")
        return 2
    try:
        return run_self_test(args.cxx) if args.self_test else run_guard(args.cxx)
    except tsast.KitMissing as exc:
        sys.stderr.write(f"check-ct-disassembly: the census cannot run: {exc}\n")
        return 2


if __name__ == "__main__":
    sys.exit(main())
