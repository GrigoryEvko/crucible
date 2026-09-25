#!/usr/bin/env python3
"""check-trait-injection — a substrate trait or a fail-closed relation is specialized only where it is declared.

C++ has no orphan rule.  Any translation unit may specialize any template for
any type, including types it does not own.  Every fail-closed relation in the
tree is only as strong as the discipline that keeps its specializations beside
the declarations they name, and that discipline needs a scanner.

FAMILY A: SUBSTRATE TRAITS
    is_graded_specialization, value_type_decoupled, graded_modality and
    is_numerical_tier_impl assert an algebraic property of a Graded wrapper.
    A specialization from a foreign file forges a property that the wrapper
    never proved.

FAMILY B: FAIL-CLOSED RELATIONS
    Each relation defaults to false, and every legal edge is one explicit
    specialization.  Forging an edge is forging authority:

      retag_policy<From, To>        a provenance or trust transition.  A
                                    forged edge launders an untrusted value
                                    into a Sanitized or Verified tag.
      machine_transition<From, To>  a state-machine edge, also reached
                                    through CRUCIBLE_ALLOW_MACHINE_TRANSITION.
      predicate_implies<P, Q>       a refinement subsumption.
      survivor_registry<DeadTag>    who inherits the permissions of a dead
                                    peer on crash-stop recovery.
      is_subsort<T, U>              a payload subtype, which widens what a
                                    session channel accepts.

    can_split_into and can_split_into_pack have their own guard,
    scripts/check-splits-orphan.py.

FAMILY C: FAIL-CLOSED NAMESPACES
    The new tree states a relation as a namespace of
    foundation::fail_closed::edge<From, To> variables (foundation/diag/FailClosed.h).
    A namespace is open by definition, so the guard refuses each definition
    of a namespace with one of these names outside its authoring set:
    admitted_retags (fixy/Tagged.h), admitted_policies (fixy/Secret.h),
    admitted_transitions (fixy/Machine.h) and admitted_implications
    (fixy/Refined.h).  A namespace alias adds no member and does not count.

WHAT READS THE SITES
    The parse tree of the pinned tree-sitter kit (scripts/tsast.py), over each
    C and C++ file that git lists and that names a relation.  A specialization
    is a template declaration whose class names a template-id of the relation,
    written plain or qualified (`struct ::crucible::safety::retag_policy<...>`).
    A reopening is a namespace definition whose last name is the relation
    namespace.  A comment, a string, a raw string and a prose ledger hold no
    node.  A macro body is one preproc_arg of raw text, and a file in
    tsast.UNPARSEABLE has no tree, so the guard reads those with the patterns
    of the old text scan, over text with the comments and the literals
    blanked.  A file that the parser cannot read fails.

WHAT IT CANNOT SEE
    A name that a macro of another file forms, such as `#define R retag_policy`
    and then `template <> struct R<...>`.  The file that uses the macro does
    not spell the relation, and the kit does not expand macros.  A variable
    template that a relation exposes is not a relation of this table.

AUTHORING SETS ARE PER RELATION
    A single shared set would admit a forged is_graded_specialization from
    each directory that may declare is_subsort.  Each relation carries its own
    globs in RELATIONS below, and `*` spans `/`.  Widening a set is a one-line
    edit that a reviewer sees.  test/ may declare Family B and C edges, because
    the negative-compile fixtures and the sentinels are the witnesses that the
    relations stay fail-closed.

Usage
    check-trait-injection.py              scan the tree
    check-trait-injection.py --self-test  plant each forgery and each exemption

Exit 0 clean, 1 on a forged specialization or a file the parser cannot read,
2 on a bad invocation or a failed self-test, 3 when the kit is not installed.
"""

from __future__ import annotations

import contextlib
import fnmatch
import io
import os
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import cxx_lex  # noqa: E402  (the path insert above has to come first)
import tsast  # noqa: E402

SOURCE_SUFFIXES = frozenset({".c", ".h", ".cc", ".hh", ".cpp", ".hpp", ".cxx", ".hxx", ".inl", ".ipp", ".tpp",
                             ".tcc", ".inc", ".ixx", ".cppm"})
EXCLUDED_DIRS = ("build", "cmake-build-", "third_party", "external", "vendor", ".git", ".tools")
SUBSTRATE_PATHS = ("include/crucible/algebra/*", "include/foundation/algebra/*", "include/fixy/*",
                   "include/crucible/safety/*", "include/crucible/permissions/*", "include/crucible/handles/*",
                   "test/test_concept_cheat_probe.cpp", "test/fixy/test_cheat_probe.cpp",
                   "test/fixy/neg/neg_cheat_graded_modality_injection.cpp")
MACHINE_MACRO = "CRUCIBLE_ALLOW_MACHINE_TRANSITION"
SPEC = r"template\s*<[^{};]*>\s*(?:struct|class)\s+(?:\[\[[^\]]*\]\]\s*|alignas\s*\([^)]*\)\s*)*(?:::)?(?:\w+::)*"


@dataclass(frozen=True)
class Relation:
    """One scanned relation: what forges it and where it may be authored."""

    label: str
    kind: str
    names: tuple[str, ...]
    globs: tuple[str, ...]


RELATIONS = (
    Relation("substrate", "specialization",
             ("is_graded_specialization", "value_type_decoupled", "graded_modality", "is_numerical_tier_impl"),
             SUBSTRATE_PATHS),
    Relation("retag_policy", "specialization", ("retag_policy",),
             ("include/crucible/safety/_Tagged.h", "include/crucible/safety/source/*.h", "test/*")),
    Relation("machine_transition", "specialization", ("machine_transition",),
             ("include/crucible/safety/_Machine.h", "include/fixy/Machine.h", "test/*")),
    Relation("predicate_implies", "specialization", ("predicate_implies",),
             ("include/crucible/safety/_Refined.h", "include/crucible/safety/_RefinedAlgebra.h", "test/*")),
    Relation("survivor_registry", "specialization", ("survivor_registry",),
             ("include/crucible/permissions/PermissionInherit.h", "test/*")),
    Relation("is_subsort", "specialization", ("is_subsort",), ("include/crucible/sessions/*.h", "test/*")),
    Relation("admitted_retags", "reopening", ("admitted_retags",), ("include/fixy/Tagged.h", "test/*")),
    Relation("admitted_policies", "reopening", ("admitted_policies",), ("include/fixy/Secret.h", "test/*")),
    Relation("admitted_transitions", "reopening", ("admitted_transitions",), ("include/fixy/Machine.h", "test/*")),
    Relation("admitted_implications", "reopening", ("admitted_implications",), ("include/fixy/Refined.h", "test/*")),
)
MACRO_OF = {MACHINE_MACRO: "machine_transition"}
NAMES = {name: relation for relation in RELATIONS for name in relation.names}
BY_LABEL = {relation.label: relation for relation in RELATIONS}


def text_patterns() -> list[tuple[str, re.Pattern[str]]]:
    """Return the pattern of each relation for text that has no tree: a macro body or an unparseable file."""
    patterns = []
    for relation in RELATIONS:
        names = "|".join(relation.names)
        if relation.kind == "specialization":
            patterns.append((relation.label, re.compile(rf"{SPEC}(?:{names})\s*<")))
        else:
            patterns.append((relation.label, re.compile(rf"\bnamespace\s+(?:\w+::)*(?:{names})\s*\{{")))
    for macro, label in MACRO_OF.items():
        patterns.append((label, re.compile(rf"\b{macro}\s*\(")))
    return patterns


TEXT_PATTERNS = text_patterns()
NEEDLES = tuple(name.encode() for name in [*NAMES, *MACRO_OF])


@dataclass(frozen=True)
class Site:
    """One forged specialization or reopening."""

    label: str
    path: str
    line: int
    text: str


def listed_files(root: Path) -> list[str]:
    """Return the C and C++ files under the root that name a relation, relative to the root.

    Git lists the files of a work tree, which keeps build trees out.  Outside
    a work tree the walk skips each build, vendor and tool directory.
    """
    try:
        out = subprocess.run(["git", "-C", str(root), "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
                             check=True, capture_output=True).stdout.decode(errors="replace")
        candidates = [path for path in out.split("\0") if path]
    except (OSError, subprocess.CalledProcessError):
        candidates = []
        for directory, dirs, names in os.walk(root):
            dirs[:] = sorted(d for d in dirs if not d.startswith(EXCLUDED_DIRS))
            candidates += [str((Path(directory) / name).relative_to(root)) for name in names]
    found = []
    for path in sorted(candidates):
        full = root / path
        if Path(path).suffix in SOURCE_SUFFIXES and full.is_file():
            data = full.read_bytes()
            if any(needle in data for needle in NEEDLES):
                found.append(path)
    return found


def authored(relation: Relation, path: str) -> bool:
    """Report whether a path lies in the authoring set of a relation."""
    return any(fnmatch.fnmatchcase(path, glob) for glob in relation.globs)


def template_name(node: tsast.Node) -> str | None:
    """Return the template name that a class head names as a template-id, plain or qualified, or None."""
    while node.type == "qualified_identifier":
        inner = node.child_by_field("name")
        if inner is None:
            return None
        node = inner
    if node.type != "template_type":
        return None
    named = node.child_by_field("name")
    return named.text if named is not None else None


def tree_sites(tree: tsast.Tree, path: str) -> list[Site]:
    """Return the sites that the parse tree of one file holds."""
    sites: list[Site] = []
    for node in tree.find("template_declaration"):
        for head in node.children_of_type("class_specifier", "struct_specifier", "union_specifier"):
            named = head.child_by_field("name")
            name = template_name(named) if named is not None else None
            relation = NAMES.get(name or "")
            if relation is not None and relation.kind == "specialization":
                sites.append(Site(relation.label, path, node.line, node.text.splitlines()[0].strip()))
    for node in tree.find("namespace_definition"):
        named = node.child_by_field("name")
        if named is None:
            continue
        parts = [part.text for part in named.descendants("namespace_identifier")] \
            if named.type == "nested_namespace_specifier" else [named.text]
        relation = NAMES.get(parts[-1])
        if relation is not None and relation.kind == "reopening":
            sites.append(Site(relation.label, path, node.line, node.text.splitlines()[0].strip()))
    for node in tree.find("macro_invocation", "call_expression"):
        callee = node.child_by_field("name") or node.child_by_field("function")
        if callee is not None and callee.text in MACRO_OF:
            sites.append(Site(MACRO_OF[callee.text], path, node.line, node.text.splitlines()[0].strip()))
    return sites


def text_sites(text: str, path: str, first_line: int) -> list[Site]:
    """Return the sites of text that has no tree, read with the patterns of the old text scan."""
    blanked, _ = cxx_lex.blank(text, blank_literals=True)
    sites = []
    for label, pattern in TEXT_PATTERNS:
        for match in pattern.finditer(blanked):
            line = first_line + blanked.count("\n", 0, match.start())
            sites.append(Site(label, path, line, match.group(0).split("\n")[0].strip()))
    return sites


def scan(root: Path) -> tuple[list[Site], list[str]]:
    """Return each site outside its authoring set, and each file the parser cannot read.

    Complexity: linear in the size of the files that name a relation.

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    files = listed_files(root)
    sites: list[Site] = []
    unread: list[str] = []
    for tree in tsast.parse([root / path for path in files], strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            if rel in tsast.UNPARSEABLE:
                sites += text_sites(tree.source.decode("utf-8", "replace"), rel, 1)
            else:
                unread.append(f"trait_guard: {rel} does not parse, so the relations it may specialize are unknown."
                              f"\n  {tree.diagnostic}")
            continue
        sites += tree_sites(tree, rel)
        for body in tree.find("preproc_arg"):
            sites += text_sites(body.text, rel, body.line)
    forged = sorted({site for site in sites if not authored(BY_LABEL[site.label], site.path)},
                    key=lambda site: (site.path, site.line, site.label))
    return forged, unread


def run(root: Path) -> int:
    """Scan, print each finding, and return the exit code."""
    forged, unread = scan(root)
    for site in forged:
        relation = BY_LABEL[site.label]
        print(f"trait_guard[{site.label}]: forbidden specialization at {site.path}:{site.line}", file=sys.stderr)
        print(f"trait_guard[{site.label}]: {site.text}", file=sys.stderr)
        print(f"trait_guard[{site.label}]: authoring set is: {' '.join(relation.globs)}", file=sys.stderr)
    for line in unread:
        print(line, file=sys.stderr)
    if forged or unread:
        print("trait_guard: each scanned relation is specialized only inside its own authoring set.  C++ has no "
              "orphan rule, so a specialization outside the file that declares the named types forges a property "
              "or an authority that was never granted.  If the new place is legitimate, widen that relation's row "
              "in scripts/check-trait-injection.py so the widening is reviewed.", file=sys.stderr)
        return 1
    print(f"check-trait-injection: clean — {len(RELATIONS)} relations, each specialized only in its authoring set.",
          file=sys.stderr)
    return 0


def self_test() -> int:
    """Plant each forgery under a path outside its set and each edge inside its set, and examine each verdict.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []
    negatives = 0

    def expect(name: str, ok: bool, negative: bool = False) -> None:
        """Record one case result and print it."""
        nonlocal negatives
        negatives += negative
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(name)

    planted = {
        "src/planted/trait.cpp": (
            "namespace crucible {\n"
            "template <>\n"
            "struct is_graded_specialization<planted::Probe<int>> { static constexpr bool is_specialized = true; };\n"
            "}\n"),
        "src/planted/retag.cpp": (
            "namespace crucible::safety {\n"
            "template <>\n"
            "struct retag_policy<source::FromDb, trust::Verified> { static constexpr bool allowed = true; };\n"
            "}\n"
            "// struct retag_policy<source::FromUser, trust::Verified> is prose.\n"
            "/* template <> struct retag_policy<A, B> {}; in a block comment\n"
            "   template <> struct retag_policy<C, D> {}; */\n"
            "const char* text = R\"(template <> struct retag_policy<E, F> {};)\";\n"),
        "src/planted/qualified.cpp": (
            "template <>\n"
            "struct ::crucible::safety::retag_policy<source::FromDb, trust::Verified> : std::true_type {};\n"
            "template <>\n"
            "struct [[deprecated]] crucible::safety::predicate_implies<A, B> : std::true_type {};\n"),
        "src/planted/machine.cpp": (
            "CRUCIBLE_ALLOW_MACHINE_TRANSITION(Authenticated, Disconnected)\n"
            "namespace crucible::safety {\n"
            "template <>\n"
            "struct machine_transition<Authenticated, Disconnected> : std::true_type {};\n"
            "}\n"),
        "src/planted/implies.cpp": (
            "namespace crucible::safety {\n"
            "template <class T>\n"
            "struct predicate_implies<T, PlantedNarrow> : std::true_type {};\n"
            "}\n"),
        "src/planted/survivor.cpp": (
            "namespace crucible::safety {\n"
            "template <>\n"
            "struct survivor_registry<PlantedDeadTag> { using type = inheritance_list<PlantedForeignTag>; };\n"
            "}\n"),
        "src/planted/subsort.cpp": (
            "namespace crucible::safety::proto {\n"
            "template <>\n"
            "struct is_subsort<PlantedNarrow, PlantedWide> : std::true_type {};\n"
            "}\n"),
        "src/planted/retags.cpp": (
            "namespace fixy::tags::admitted_retags {\n"
            "inline constexpr ::foundation::fail_closed::edge<source::Sanitized, source::External> planted{};\n"
            "}\n"
            "namespace fixy::tags {\n"
            "namespace admitted_retags {\n"
            "}\n"
            "}\n"
            "namespace planted_alias = fixy::tags::admitted_retags;\n"),
        "src/planted/policies.cpp": "namespace fixy::tags::secret_policy::admitted_policies {\n}\n",
        "src/planted/transitions.cpp": "namespace fixy::machine::admitted_transitions {\n}\n",
        "src/planted/implications.cpp": "namespace fixy::refined::admitted_implications {\n}\n",
        "src/planted/macro.cpp": (
            "#define FORGE(T) template <> struct retag_policy<T, trust::Verified> : std::true_type {}\n"
            "#define MENTION retag_policy is named here, and nothing is specialized\n"),
    }
    expected = {
        ("substrate", "src/planted/trait.cpp", 2),
        ("retag_policy", "src/planted/retag.cpp", 2),
        ("retag_policy", "src/planted/qualified.cpp", 1),
        ("predicate_implies", "src/planted/qualified.cpp", 3),
        ("machine_transition", "src/planted/machine.cpp", 1),
        ("machine_transition", "src/planted/machine.cpp", 3),
        ("predicate_implies", "src/planted/implies.cpp", 2),
        ("survivor_registry", "src/planted/survivor.cpp", 2),
        ("is_subsort", "src/planted/subsort.cpp", 2),
        ("admitted_retags", "src/planted/retags.cpp", 1),
        ("admitted_retags", "src/planted/retags.cpp", 5),
        ("admitted_policies", "src/planted/policies.cpp", 1),
        ("admitted_transitions", "src/planted/transitions.cpp", 1),
        ("admitted_implications", "src/planted/implications.cpp", 1),
        ("retag_policy", "src/planted/macro.cpp", 1),
    }
    exempt = {
        "include/crucible/algebra/planted.h": (
            "namespace crucible { template <> struct is_graded_specialization<planted::Exempt<int>> {}; }\n"),
        "include/crucible/safety/source/Planted.h": (
            "namespace crucible::safety { template <> struct retag_policy<source::Raw, source::Sanitized> {}; }\n"),
        "include/crucible/sessions/Planted.h": (
            "namespace crucible::safety::proto { template <> struct is_subsort<A, B> : std::true_type {}; }\n"),
        "test/planted_survivor.cpp": (
            "namespace crucible::safety { template <> struct survivor_registry<Dead> {}; }\n"),
        "include/crucible/safety/_Machine.h": (
            "CRUCIBLE_ALLOW_MACHINE_TRANSITION(PlantedFrom, PlantedTo)\n"
            "namespace crucible::safety { template <> struct machine_transition<From, To> : std::true_type {}; }\n"),
        "include/crucible/safety/_Refined.h": (
            "namespace crucible::safety { template <> struct predicate_implies<A, B> : std::true_type {}; }\n"),
        "include/fixy/Tagged.h": "namespace fixy::tags::admitted_retags {\n}\n",
        "include/fixy/Secret.h": "namespace fixy::tags::secret_policy::admitted_policies {\n}\n",
        "include/fixy/Machine.h": "namespace fixy::machine::admitted_transitions {\n}\n",
        "include/fixy/Refined.h": "namespace fixy::refined::admitted_implications {\n}\n",
        "test/planted_test.cpp": "namespace crucible::safety { template <> struct retag_policy<X, Y> {}; }\n",
    }
    ledger = ("crucible/safety/_Tagged.h:kCatalogRosterTuple  — The retag catalog became the namespace admitted_retags; "
              "struct retag_policy<From, To> went with it, and struct is_graded_specialization<W> too.\n")

    def captured(action) -> tuple[int, str]:
        """Run an action and return its code and its stderr."""
        buffer = io.StringIO()
        with contextlib.redirect_stderr(buffer):
            code = action()
        return code, buffer.getvalue()

    print("check-trait-injection --self-test")
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        for rel, text in {**planted, **exempt}.items():
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text, encoding="utf-8")
        (root / "scripts").mkdir()
        (root / "scripts" / "planted-ledger.txt").write_text(ledger, encoding="utf-8")
        (root / "build" / "Testing").mkdir(parents=True)
        (root / "build" / "Testing" / "stale.cpp").write_text(planted["src/planted/retag.cpp"], encoding="utf-8")

        forged, unread = scan(root)
        found = {(site.label, site.path, site.line) for site in forged}
        expect("every file parses", not unread)
        for label, path, line in sorted(expected):
            expect(f"caught: {label} at {path}:{line}", (label, path, line) in found, True)
        expect("nothing else is reported", found == expected, True)
        expect("a comment, a block comment and a raw string are no specialization",
               not {site for site in forged if site.path == "src/planted/retag.cpp" and site.line > 2})
        expect("a namespace alias is no reopening", ("admitted_retags", "src/planted/retags.cpp", 8) not in found)
        expect("a macro body that only names a relation is no specialization",
               ("retag_policy", "src/planted/macro.cpp", 2) not in found)
        for rel in exempt:
            expect(f"exempt in its authoring set: {rel}", not any(site.path == rel for site in forged))
        expect("a prose ledger is not scanned", not any(site.path.startswith("scripts/") for site in forged))
        expect("a build tree is not scanned", not any(site.path.startswith("build/") for site in forged))

        code, report = captured(lambda: run(root))
        expect("a forged tree exits 1", code == 1 and "forbidden specialization at src/planted/trait.cpp:2" in report,
               True)
        previous = Path.cwd()
        os.chdir(root / "build" / "Testing")
        try:
            from_build = captured(lambda: run(root))
        finally:
            os.chdir(previous)
        expect("the report from inside the build tree equals the report from the root", from_build == (code, report))

        (root / "src" / "planted" / "broken.cpp").write_text("void f() { g(1) { } } // retag_policy\n",
                                                            encoding="utf-8")
        code, report = captured(lambda: run(root))
        expect("a file the parser cannot read fails", code == 1 and "src/planted/broken.cpp does not parse" in report,
               True)
        for rel in [*planted, "src/planted/broken.cpp"]:
            (root / rel).unlink()
        expect("a tree with only authored edges exits 0", captured(lambda: run(root))[0] == 0)

    if failures:
        print(f"check-trait-injection --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"check-trait-injection --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run one mode."""
    try:
        if argv == []:
            return run(tsast.REPO_ROOT)
        if argv == ["--self-test"]:
            return self_test()
    except tsast.KitMissing as exc:
        print(f"check-trait-injection: {exc}", file=sys.stderr)
        return 3
    print("usage: check-trait-injection.py [--self-test]", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
