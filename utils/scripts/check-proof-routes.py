#!/usr/bin/env python3
"""check-proof-routes.py: refuse the source shapes that build a proof type without its door.

The rule
--------
A proof type (a context, a key, a capability, a permission, a pin, a
reading, a placement) is built only by the door that its header names.
The language refuses most other routes: a constructor is private, the
class is not trivially copyable, and it is not an implicit-lifetime type.
The shapes below are legal C++ and pass each of those refusals, so this
guard refuses them in the source text instead:

  union                          a union may hold any object type, and the
                                 address of a member that is not active
                                 points at an object whose lifetime never
                                 started.  Each union definition in scope,
                                 a union template too, needs a reviewed
                                 entry that says it holds no proof type.
  pointer-cast                   a static_cast, reinterpret_cast or
                                 bit_cast to a pointer or a reference whose
                                 target names a proof type.  A void
                                 pointer from std::malloc, ::operator new,
                                 an arena or a buffer becomes a pointer to
                                 a proof with no constructor call.
  allocator                      std::allocator, allocator_traits or
                                 polymorphic_allocator of a proof type.
                                 allocate gives a typed pointer with no
                                 object in it.
  raw-allocation                 ::operator new, malloc, calloc, realloc
                                 or aligned_alloc with the size of a proof
                                 type.
  explicit-instantiation         an explicit instantiation turns off the
                                 access check for the names in it, so a
                                 pointer to a private member of a proof type
                                 can escape through one.
  function-specialization        an explicit specialization of a function
                                 template is a friend of each class that
                                 befriends the template, so a
                                 specialization of a door builds the proof
                                 with no call to the kernel.
  member-pointer-specialization  an explicit specialization turns off the
                                 access check for its template arguments,
                                 so a pointer to a private member escapes
                                 through one.
  friend-template-specialization a specialization of a class template
                                 that a proof type befriends is a friend of
                                 that proof type.  The file that defines the
                                 primary template, with a body, may
                                 specialize it.  A declaration with no body
                                 makes no file the owner, because any file
                                 can declare the template again.

The proof names
---------------
The pointer-cast, allocator and raw-allocation rules need the names of
the proof types.  They come from two sources, and the guard uses their
union.  utils/scripts/witness-roster.txt names each type that attests to a fact
it cannot see, and check-witness-roster.py refuses a door that the roster
does not name.  test_forgeable_proofs --proof-names prints each class that
the reflection walk of that test finds to have the shape of a proof, and
each template in its witness list: one line each, the name that a source
spells, a tab, and the qualified name.  The guard matches the spelled
name, and a report gives the qualified names that the spelling can refer
to.  A type alias or a typedef whose definition names a proof type becomes
a proof name as well, in its own file.  An alias or a typedef at namespace
scope in a header becomes a proof name in every file, because each file
that includes the header can cast to it.  The parse tree of the pinned
tree-sitter kit (utils/scripts/tsast.py) tells a namespace-scope alias from a
member alias, so an alias in a class body stays in its own file.  An alias
name carries no namespace here, so an alias of another namespace with the
same spelling becomes a proof name too, which errs on the side that
refuses.  A header that the parser cannot read fails the run.

An identifier that the template parameter list of an enclosing template
declaration or of an enclosing generic lambda binds names that parameter,
not a proof type of the same spelling, so it is not a proof name inside
that declaration.  A default argument binds nothing.

Two passes
----------
Both passes read parse trees of the pinned tree-sitter kit
(utils/scripts/tsast.py): each rule is a node shape, and a comment or a literal is
its own node, so neither can form a site.  The lexical pass parses each C
and C++ source file that git tracks.  An untracked file is out of scope:
the export and the build of a guard run, or the scratch file of another
tool, can appear under the tree and vanish while this guard reads it.  The
facts of each file text stay in the results of the store of
utils/scripts/preprocessed.py, so a warm run parses no file.  The
preprocessed pass runs with
--compile-db: it runs each entry of the compile database through the
preprocessor with the flags of the build, and it parses each distinct
expansion of each file, so a shape that a macro or token pasting forms is
seen too.  The output comes from the shared store of
utils/scripts/preprocessed.py, so every guard, build directory and work tree
preprocesses each translation unit one time.  The records of an expansion
stay in the results of the store, under the key of the file's chunk list and
a name that hashes this guard and the parser, so a header that many units
expand the same way is parsed one time, and a warm run parses no expansion.
For each key the count is the larger count of the two passes.  A
preprocessor failure, and a file or an expansion that the parser cannot
read, refuses the run.

A negative fixture of test/layer compiles against a staged layer root, an
include directory that links only to the layers below it, and one of them
includes a higher layer so that it fails there.  The store reads such a
unit with the include directory of the repository instead of the staged
root, so the fixture is read like any other unit and a route in it counts.
The directory is not skipped.

Out of scope, stated rather than implied
----------------------------------------
- A negative-compile fixture, a file under a test directory named neg or
  *_neg.  It must fail to compile, so it builds nothing.
- The BPF programs under include/crucible/*/bpf/ and each *.bpf.c file.
  They are C for the BPF target, and no C++ header reaches them.
- A route through a template parameter.  `static_cast<T*>(buffer)` in an
  arena or a container names T, not a proof type, and the guard cannot know
  which argument reaches T.  A parameter spelled like a proof type is the
  same route.  test/fixy/test_forgeable_proofs.cpp pins this route with its
  argument.
- A pointer value copied with std::memcpy into a variable whose type is a
  pointer to a proof type.  The copy names no proof type at the call.  The
  same ledger pins it.

The allowlist
-------------
utils/scripts/proof-routes-allowlist.txt admits a reviewed site.  An entry is
`path:rule:key`, or `path:rule:key xN` for N sites, and a comment above
each paragraph gives the reason.  An entry above the count that the tree
has, or an entry for a key that the tree does not have, is stale.

Exit codes
  0  each site in scope has an entry, and each entry admits its sites
  1  a site with no entry, or more sites than its entry admits
  2  a stale entry, a preprocessor failure, a file or an expansion that the
     parser cannot read, a bad invocation, or a failed self-test
  3  the pinned tree-sitter kit is not installed, or the proof-name binary
     or the compile database that the arguments name is not built, which
     ctest reports as a skip

Usage
  check-proof-routes.py [--compile-db PATH] [--proof-names-binary PATH]
  check-proof-routes.py ... --list          print each admitted site
  check-proof-routes.py --self-test         plant each route and prove the verdicts
"""

import contextlib
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cache_dir  # noqa: E402
import throwaway_repo  # noqa: E402
import tsast  # noqa: E402
from preprocessed import Expansion, Store, chunk_text, text_results  # noqa: E402
from repo_root import REPO_ROOT  # noqa: E402

SOURCE_SUFFIXES = frozenset({".c", ".h", ".cc", ".hh", ".cpp", ".hpp", ".cxx", ".hxx", ".inl", ".ipp", ".tpp",
                             ".tcc", ".inc", ".ixx", ".cppm"})
# A namespace-scope alias of a header reaches each file that includes it.
# A source file is one translation unit, and its own aliases stay in it.
HEADER_SUFFIXES = frozenset({".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp", ".tpp", ".tcc", ".inc"})
NAMESPACE_SCOPE = frozenset({"translation_unit", "namespace_definition", "declaration_list", "linkage_specification",
                             "template_declaration", "preproc_if", "preproc_ifdef", "preproc_else", "preproc_elif",
                             "preproc_elifdef"})
FIXTURE = re.compile(r"^test/(?:[^/]+/)*(?:neg|[^/]+_neg)/[^/]+$")
BPF = re.compile(r"^include/crucible/[^/]+/bpf/|\.bpf\.c$")
ENTRY = re.compile(r"^(?P<key>.*?)(?: x(?P<count>[1-9][0-9]*))?$")

CASTS = frozenset({"static_cast", "reinterpret_cast", "bit_cast"})
ALLOCATORS = frozenset({"allocator", "allocator_traits", "polymorphic_allocator"})
RAW_ALLOCATORS = frozenset({"malloc", "calloc", "realloc", "aligned_alloc"})
CLASS_HEADS = ("class_specifier", "struct_specifier", "union_specifier")
# The leaves that spell one part of a name.
NAME_LEAVES = ("identifier", "type_identifier", "namespace_identifier", "field_identifier")
# The declarators that sit between a declared name and its type without
# changing what the name is.
PASS_THROUGH = frozenset({"init_declarator", "attributed_declarator", "parenthesized_declarator"})


def is_word(character: str) -> bool:
    """True for a character that can continue an identifier or a number."""
    return character.isalnum() or character == "_"


def canonical(text: str) -> str:
    """Reduce white space to the key form: one space between two word characters, none elsewhere.

    The key is an identity for the allowlist, and this form keeps each key
    of the allowlist stable.  Complexity: linear in the length of the text.
    """
    out: list[str] = []
    pending_space = False
    for character in text.strip():
        if character.isspace():
            pending_space = True
            continue
        if pending_space and out and is_word(out[-1][-1]) and is_word(character):
            out.append(" ")
        pending_space = False
        out.append(character)
    return "".join(out)


def span_key(node: tsast.Node, start: tuple[int, int] | None = None, end: tuple[int, int] | None = None) -> str:
    """Return the key form of the source that a node spans, or part of it, with every comment left out.

    A comment is its own node, so its span is cut out before the white
    space is reduced.
    """
    begin = node.start if start is None else start
    finish = node.end if end is None else end
    pieces: list[str] = []
    cursor = begin
    for comment in node.descendants("comment"):
        if comment.start < begin or comment.end > finish:
            continue
        pieces.append(node.tree.slice(cursor, comment.start))
        pieces.append(" ")
        cursor = comment.end
    pieces.append(node.tree.slice(cursor, finish))
    return canonical("".join(pieces))


def last_name(node: tsast.Node | None) -> str | None:
    """Return the last part of a name, a template-id or a call, or None."""
    return None if node is None else tsast.leaf_name(node)


def names_in(node: tsast.Node) -> list[str]:
    """Return the distinct names that a subtree spells, sorted, less the template parameters bound around it."""
    bound = bound_parameters(node)
    return sorted({leaf.text for leaf in node.descendants(*NAME_LEAVES)} - bound
                  | ({node.text} - bound if node.type in NAME_LEAVES else set()))


def leading_operator(node: tsast.Node) -> str:
    """Return the operator token of a unary pointer expression: `&` or `*`."""
    tokens = node.gap_tokens()
    return tokens[0] if tokens else ""


def is_member_pointer(node: tsast.Node) -> bool:
    """True for `&A::m`: the address of a qualified name, which names a member."""
    argument = node.child_by_field("argument")
    return (node.type == "pointer_expression" and argument is not None
            and argument.type == "qualified_identifier" and leading_operator(node) == "&")


def top_declarator_kinds(descriptor: tsast.Node) -> set[str]:
    """Return the kinds of the abstract declarators that wrap a type descriptor, outermost first."""
    kinds: set[str] = set()
    node = descriptor.child_by_field("declarator")
    while node is not None:
        kinds.add(node.type)
        node = node.child_by_field("declarator")
    return kinds


def cast_record(call: tsast.Node) -> list | None:
    """Return the pointer-cast record of a call, or None when the call is no cast to a pointer or a reference.

    A cast to a function pointer selects an overload, and a cast of nullptr
    gives a null pointer.  Neither gives a pointer to storage.
    """
    function = call.child_by_field("function")
    if function is not None and function.type == "qualified_identifier":
        function = function.child_by_field("name")
    if function is None or function.type != "template_function":
        return None
    cast = last_name(function)
    arguments = function.child_by_field("arguments")
    if cast not in CASTS or arguments is None:
        return None
    descriptor = next((c for c in arguments.children if c.type == "type_descriptor"), None)
    if descriptor is None:
        return None
    kinds = top_declarator_kinds(descriptor)
    call_arguments = call.child_by_field("arguments")
    values = [] if call_arguments is None else [c for c in call_arguments.children if c.type != "comment"]
    is_nullptr = len(values) == 1 and values[0].type == "null"
    if not kinds & {"abstract_pointer_declarator", "abstract_reference_declarator"}:
        return None
    if is_nullptr or "abstract_function_declarator" in kinds:
        return None
    return ["pointer-cast", f"{cast}<{span_key(descriptor)}>", call.line, names_in(descriptor), None]


def specialized_id(declaration: tsast.Node) -> tsast.Node | None:
    """Return the template-id that an explicit or partial specialization names, or None."""
    if declaration.type in CLASS_HEADS:
        name = declaration.child_by_field("name")
        return name if name is not None and name.type == "template_type" else None
    node = declaration.child_by_field("declarator")
    while node is not None:
        if node.type in ("template_function", "template_type"):
            return node
        if node.type == "qualified_identifier":
            node = node.child_by_field("name")
            continue
        node = node.child_by_field("declarator")
    return None


def function_declarator_of(declaration: tsast.Node) -> tsast.Node | None:
    """Return the function declarator of a declaration, or None for a variable or a type."""
    node = declaration.child_by_field("declarator")
    while node is not None and node.type in PASS_THROUGH:
        node = node.child_by_field("declarator")
    return node if node is not None and node.type == "function_declarator" else None


def specialization_records(template: tsast.Node) -> list[list]:
    """Return the records of one template declaration that specializes: explicit (`template <>`) or partial."""
    parameters = template.child_by_field("parameters")
    declared = [c for c in template.children if c.field is None and c.type not in ("comment", "requires_clause")]
    inner = declared[-1] if declared else None
    if parameters is None or inner is None:
        return []
    explicit = not any(c.type != "comment" for c in parameters.children)
    target = specialized_id(inner)
    records: list[list] = []
    if explicit:
        body = inner.child_by_field("body")
        head = span_key(inner, end=body.start if body is not None else None)[:160]
        arguments = None if target is None else target.child_by_field("arguments")
        if arguments is not None and any(is_member_pointer(p) for p in arguments.descendants("pointer_expression")):
            records.append(["member-pointer-specialization", head, template.line, None, None])
        if inner.type in CLASS_HEADS and target is not None:
            records.append(["friend-template-specialization", head, template.line, None, last_name(target)])
        elif target is not None and target.type == "template_function" and function_declarator_of(inner) is not None:
            parameters_list = function_declarator_of(inner).child_by_field("parameters")
            key = span_key(inner, end=parameters_list.start if parameters_list is not None else None)
            records.append(["function-specialization", key[:160], template.line, None, None])
    elif inner.type in CLASS_HEADS and target is not None:
        arguments = target.child_by_field("arguments")
        if arguments is not None:
            key = span_key(template, end=arguments.start) + "<"
            records.append(["friend-template-specialization", key[:160], template.line, None, last_name(target)])
    return records


def alias_names(node: tsast.Node) -> list[str]:
    """Return the names that one alias declaration or typedef declares."""
    if node.type == "alias_declaration":
        named = node.child_by_field("name")
        return [named.text] if named is not None else []
    return [leaf.text for child in node.children if child.field == "declarator"
            for leaf in ([child] if child.type == "type_identifier" else child.descendants("type_identifier"))]


# A record is [rule, key, line, names, subject].  names is the list of
# identifiers that the rule tests against the proof names, or None when the
# rule tests none.  subject is the class name of a class specialization, or
# None.  A record holds no proof name, so a cached record stays valid when
# the list of proof names changes.
def extract(tree: tsast.Tree) -> list[list]:
    """Each candidate site and each alias of one parse.  Complexity: linear in the number of nodes."""
    records: list[list] = []
    for node in tree.find("alias_declaration", "type_definition"):
        body = node.child_by_field("type")
        if body is not None:
            records += [["alias", name, node.line, names_in(body), None] for name in alias_names(node)]
    for node in tree.find("union_specifier"):
        if node.child_by_field("body") is not None:
            name = node.child_by_field("name")
            records.append(["union", name.text if name is not None else "<anonymous>", node.line, None, None])
    for call in tree.find("call_expression"):
        cast = cast_record(call)
        if cast is not None:
            records.append(cast)
            continue
        callee = call.child_by_field("function")
        name = last_name(callee)
        if name in RAW_ALLOCATORS or name == "operator new":
            arguments = call.child_by_field("arguments")
            sizes = [] if arguments is None else list(arguments.descendants("sizeof_expression"))
            if sizes:
                names = sorted({n for size in sizes for n in names_in(size)})
                records.append(["raw-allocation", name, call.line, names, None])
    for node in tree.find("template_type", "template_function"):
        name = last_name(node)
        arguments = node.child_by_field("arguments")
        if name in ALLOCATORS and arguments is not None:
            records.append(["allocator", f"{name}{span_key(arguments)}", node.line, names_in(arguments), None])
    for node in tree.find("template_instantiation"):
        named = [c for c in node.children if c.type != "comment"]
        if named:
            records.append(["explicit-instantiation", span_key(node, named[0].start, named[-1].end)[:160],
                            node.line, None, None])
    for template in tree.find("template_declaration"):
        records += specialization_records(template)
    return records


def alias_closure(proofs: frozenset[str], aliases: list[tuple[str, list[str]]]) -> frozenset[str]:
    """The proof names, and each alias whose definition names one, to a fixed point.

    An alias name has no namespace here, so an alias of another scope with
    the same spelling becomes a proof name too.  That errs on the side that
    refuses.  Complexity: linear in the alias count for each round, and a
    round adds at least one name."""
    names = set(proofs)
    pending = list(aliases)
    grew = True
    while grew:
        grew = False
        remaining = []
        for alias, parts in pending:
            if alias in names:
                continue
            if any(part in names for part in parts):
                names.add(alias)
                grew = True
            else:
                remaining.append((alias, parts))
        pending = remaining
    return frozenset(names)


def aliases_of(records: list[list]) -> list[tuple[str, list[str]]]:
    """Each alias record of a file as (alias, the identifiers of its definition)."""
    return [(r[1], r[3]) for r in records if r[0] == "alias"]


def judge(records: list[list], proofs: frozenset[str], friends: frozenset[str], primaries: frozenset[str]):
    """The sites among the records, as (rule, key, offset), for these proof and friend names.

    An alias whose definition names a proof name is a proof name in the
    file, to a fixed point.  The caller passes the closure over the aliases
    of every file, because a header can declare the alias that another
    file casts to.  Complexity: linear in the number of aliases of the file
    for each round of the closure, and in the number of records."""
    local = alias_closure(proofs, aliases_of(records))
    sites: list[tuple[str, str, int]] = []
    for rule, key, offset, names, subject in records:
        if rule == "alias":
            continue
        if names is not None and not any(name in local for name in names):
            continue
        if rule == "friend-template-specialization" and (subject not in friends or subject in primaries):
            continue
        sites.append((rule, key, offset))
    return sites


def in_scope(path: str) -> bool:
    """True when the guard reads the file: a source outside the fixtures and the BPF programs."""
    return Path(path).suffix in SOURCE_SUFFIXES and not FIXTURE.match(path) and not BPF.search(path)


def listed_files(root: Path) -> list[str]:
    """The files git tracks under the root, or every file under it outside a work tree (tsast.tracked_files).

    An untracked file is out of scope, because a guard run or another tool
    can write one under the tree while this guard reads it."""
    return [path for path in tsast.tracked_files(root) if (root / path).is_file()]


def roster_names(root: Path) -> dict[str, set[str]]:
    """The class name of each line of the witness roster, closed or open, with the name the roster writes."""
    names: dict[str, set[str]] = defaultdict(set)
    roster = root / "utils" / "scripts" / "witness-roster.txt"
    if roster.is_file():
        for raw in roster.read_text().splitlines():
            line = raw.strip()
            if line and not line.startswith("#") and "|" in line:
                written = line.split("|", 1)[0].strip()
                names[written.split("<", 1)[0].rsplit("::", 1)[-1]].add(written)
    return names


def reflected_names(binary: str | None) -> dict[str, set[str]]:
    """The names that test_forgeable_proofs --proof-names prints, each with its qualified names.

    Each line is the spelled name, a tab and the qualified name."""
    names: dict[str, set[str]] = defaultdict(set)
    if not binary:
        return names
    result = subprocess.run([binary, "--proof-names"], capture_output=True, text=True, check=True)
    for line in result.stdout.splitlines():
        spelled, _, qualified = line.strip().partition("\t")
        if spelled:
            names[spelled].add(qualified or spelled)
    return names


def key_words(key: str) -> list[str]:
    """The identifiers that a key spells, in order, for a report."""
    words: list[str] = []
    current: list[str] = []
    for character in key + " ":
        if is_word(character):
            current.append(character)
            continue
        if current and not current[0].isdigit():
            words.append("".join(current))
        current = []
    return words


def qualified_names(key: str, qualified: dict[str, set[str]]) -> str:
    """The qualified names of the proof names that a key spells, for a report."""
    found = [f"{name} = {' or '.join(sorted(qualified[name]))}" for name in dict.fromkeys(key_words(key))
             if name in qualified]
    return f"  The key names {', and '.join(found)}." if found else ""


def template_friends(tree: tsast.Tree) -> set[str]:
    """The class templates that a template friend declaration of the parse befriends, by their last name."""
    found: set[str] = set()
    for friend in tree.find("friend_declaration"):
        if friend.parent is None or friend.parent.type != "template_declaration":
            continue
        named = next((c for c in friend.children if c.type in ("type_identifier", "qualified_identifier",
                                                                 "template_type")), None)
        name = last_name(named)
        if name is not None:
            found.add(name)
    return found


def defined_primaries(tree: tsast.Tree) -> set[str]:
    """The class templates whose primary definition, with a body, the parse holds.

    A forward declaration defines nothing, so it does not let its file
    specialize the template.
    """
    found: set[str] = set()
    for template in tree.find("template_declaration"):
        parameters = template.child_by_field("parameters")
        if parameters is None or not any(c.type != "comment" for c in parameters.children):
            continue
        for head in template.children_of_type(*CLASS_HEADS):
            name = head.child_by_field("name")
            if head.child_by_field("body") is not None and name is not None and name.type == "type_identifier":
                found.add(name.text)
    return found


def tree_facts(tree: tsast.Tree) -> dict | None:
    """Return what the guard reads from one parse, or None when the parser cannot read it.

    The facts are the records of the file, the templates that it befriends,
    the primaries that it defines and its namespace-scope aliases.  They
    depend on the text of the file alone.
    """
    if tree.diagnostic is not None:
        return None
    return {"records": extract(tree), "friends": sorted(template_friends(tree)),
            "primaries": sorted(defined_primaries(tree)),
            "aliases": [[alias, names] for alias, names in namespace_aliases(tree)]}


def file_facts(root: Path, scope: list[str]) -> list[tuple[str, dict | None]]:
    """Return the facts of each file in scope, in order, from the store or from one parse.

    The store of utils/scripts/preprocessed.py keeps the facts of each text
    under its SHA-256 and a name that hashes this guard and the parser, so
    a warm run parses no file.  Complexity: one hash of each file, plus one
    parse of each file that the store does not hold.
    """
    results = text_results("proof-routes-lexical", Path(__file__), tsast.parser_identity())
    found: dict[str, dict | None] = {}
    missed: list[str] = []
    for rel in scope:
        stored = None if results is None else results.get(hashlib.sha256((root / rel).read_bytes()).hexdigest())
        if isinstance(stored, dict) and "facts" in stored:
            found[rel] = stored["facts"]
        else:
            missed.append(rel)
    for rel, tree in zip(missed, tsast.parse([root / rel for rel in missed], strict=False), strict=True):
        facts = tree_facts(tree)
        found[rel] = facts
        if results is not None:
            results.put(hashlib.sha256(tree.source).hexdigest(), {"facts": facts})
    return [(rel, found[rel]) for rel in scope]


def preprocessed_records(root: Path, compile_db: Path, failures: list[str],
                         tracked: frozenset[str]) -> list[tuple[str, list[list]]]:
    """The records of each file in the preprocessed output of each database entry, as (path, records).

    Only a file that the lexical pass also reads counts, so a file that the
    build generates under the root has no key that depends on the name of
    the build directory.  The records of a file name no proof type, so a
    change to the list of proof names does not invalidate their cache.  Each
    distinct expansion is parsed one time, and an expansion that the parser
    cannot read refuses the run.

    Complexity: linear in the number of units times the files each reads,
    plus one parse for each new distinct expansion text."""
    store = Store(compile_db, root, int(os.environ.get("PROOF_ROUTES_JOBS", "0") or 0))
    results = store.results("proof-routes", Path(__file__), tsast.parser_identity())
    records_of: dict[str, list[list]] = {}
    pending: dict[str, Expansion] = {}
    expansions: list[tuple[str, str]] = []
    seen: set[tuple[str, str]] = set()
    for expansion in store.expansions():
        path = expansion.path
        if path not in tracked or not in_scope(path) or (path, expansion.key) in seen:
            continue
        seen.add((path, expansion.key))
        expansions.append((path, expansion.key))
        if expansion.key in records_of or expansion.key in pending:
            continue
        cached = results.get(expansion.key)
        if isinstance(cached, list):
            records_of[expansion.key] = cached
        else:
            pending[expansion.key] = expansion
    failures.extend(store.failures)
    items = [(str(store.directory), key, expansion.path, [chunk.digest for chunk in expansion.chunks])
             for key, expansion in pending.items()]
    for (_store, key, path, _digests), found in zip(items, store.map_batches(expansion_records, items), strict=True):
        if found is None:
            failures.append(f"{path}: the parser cannot read one expansion of this file, so its routes are unknown")
            continue
        records_of[key] = found
        results.put(key, found)
    return [(path, records_of[key]) for path, key in expansions if key in records_of]


def expansion_records(batch: list[tuple[str, str, str, list[str]]]) -> list[list[list] | None]:
    """Return the records of each expansion text of a batch, or None for a text that the parser cannot read.

    Store.map_batches runs this function in a worker process.  Each item is
    (store directory, key, path, chunk digests).  Complexity: one parse of
    each text, linear in its nodes.
    """
    texts = [(f"{path} (expansion {key[:12]})", "\n".join(chunk_text(Path(store), digest) for digest in digests))
             for store, key, path, digests in batch]
    return [None if tree.diagnostic is not None else extract(tree)
            for tree in tsast.parse_texts(texts, strict=False)]


def preprocessed_counts(expansions: list[tuple[str, list[list]]], proofs, friends, primaries) -> dict[str, int]:
    """The count of each key over the preprocessed expansions, the largest over the entries of a file."""
    merged: dict[str, int] = defaultdict(int)
    for path, records in expansions:
        counts: dict[str, int] = defaultdict(int)
        for rule, key, _ in judge(records, proofs, friends, frozenset(primaries_for(path, primaries))):
            counts[f"{path}:{rule}:{key}"] += 1
        for key, count in counts.items():
            merged[key] = max(merged[key], count)
    return merged


def at_namespace_scope(node: tsast.Node) -> bool:
    """True when each node that encloses the declaration is a namespace, a linkage block, a template head or an #if arm."""
    owner = node.parent
    while owner is not None:
        if owner.type not in NAMESPACE_SCOPE:
            return False
        owner = owner.parent
    return True


def parameter_name(parameter: tsast.Node) -> str | None:
    """The name that one template parameter binds, or None for a parameter with no name.

    A default argument names another entity, so it binds nothing.  A
    template template parameter binds the name of its inner parameter.
    """
    if parameter.type == "template_template_parameter_declaration":
        inner = next((c for c in parameter.children if c.field != "parameters" and c.type != "comment"), None)
        return None if inner is None else parameter_name(inner)
    named = parameter.child_by_field("name")
    if named is not None:
        return named.text
    declarator = parameter.child_by_field("declarator")
    while declarator is not None and declarator.type not in NAME_LEAVES:
        inner = declarator.child_by_field("declarator")
        declarator = inner if inner is not None else next(
            (c for c in declarator.children if c.type in NAME_LEAVES), None)
    if declarator is not None:
        return declarator.text
    bare = next((c for c in parameter.children if c.field is None and c.type in ("type_identifier", "identifier")),
                None)
    return None if bare is None else bare.text


def bound_parameters(node: tsast.Node) -> set[str]:
    """The names that the template parameter lists around a node bind: of a declaration or of a lambda."""
    bound: set[str] = set()
    owner = node.ancestor_of_type("template_declaration", "lambda_expression")
    while owner is not None:
        field = "parameters" if owner.type == "template_declaration" else "template_parameters"
        parameters = owner.child_by_field(field)
        if parameters is not None:
            bound.update(name for name in map(parameter_name, parameters.children) if name is not None)
        owner = owner.ancestor_of_type("template_declaration", "lambda_expression")
    return bound


def namespace_aliases(tree: tsast.Tree) -> list[tuple[str, list[str]]]:
    """Each namespace-scope alias and typedef of one parse, as (alias, the names of its definition).

    The parse tree tells a namespace-scope alias from a member alias.
    Complexity: linear in the number of nodes.
    """
    aliases: list[tuple[str, list[str]]] = []
    for node in tree.find("alias_declaration", "type_definition"):
        body = node.child_by_field("type")
        if body is not None and at_namespace_scope(node):
            aliases += [(name, names_in(body)) for name in alias_names(node)]
    return aliases


def primaries_for(path: str, primaries: dict[str, set[str]]) -> set[str]:
    """The class templates whose primary definition is in the file, which may specialize them."""
    return {name for name, files in primaries.items() if path in files}


def read_allowlist(path: Path) -> tuple[dict[str, tuple[int, int]], list[str]]:
    """Each entry key, the count it admits and its line, and each malformed entry."""
    entries: dict[str, tuple[int, int]] = {}
    errors: list[str] = []
    if not path.is_file():
        return entries, errors
    paragraph_has_comment = False
    for number, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.strip()
        if not line:
            paragraph_has_comment = False
            continue
        if line.startswith("#"):
            paragraph_has_comment = True
            continue
        match = ENTRY.match(line)
        key = match.group("key")
        if key.count(":") < 2:
            errors.append(f"{path.name}:{number}: `{line}` is not path:rule:key")
        if not paragraph_has_comment:
            errors.append(f"{path.name}:{number}: `{line}` has no comment above its paragraph")
        entries[key] = (int(match.group("count") or 1), number)
    return entries, errors


def scan(root: Path, compile_db: Path | None, binary: str | None, allowlist: Path, mode: str) -> int:
    """Scan the tree under the root and compare each site with the allowlist."""
    files = listed_files(root)
    qualified: dict[str, set[str]] = defaultdict(set)
    for source in (roster_names(root), reflected_names(binary)):
        for name, spellings in source.items():
            qualified[name] |= spellings
    # One parse of each file in scope gives its records, the templates that
    # include/ befriends, the files that define each primary, and the
    # namespace-scope aliases of each header.  A file that the parser cannot
    # read refuses the run, because a route in it is unknown.
    scope = sorted(path for path in files if in_scope(path))
    records_of: dict[str, list[list]] = {}
    friend_set: set[str] = set()
    primaries: dict[str, set[str]] = defaultdict(set)
    exported: list[tuple[str, list[str]]] = []
    failures: list[str] = []
    for rel, facts in file_facts(root, scope):
        if facts is None:
            if rel not in tsast.UNPARSEABLE:
                failures.append(f"{rel}: the parser cannot read this file, so its routes are unknown")
            continue
        records_of[rel] = facts["records"]
        if rel.startswith("include/"):
            friend_set.update(facts["friends"])
            for name in facts["primaries"]:
                primaries[name].add(rel)
        if Path(rel).suffix in HEADER_SUFFIXES:
            exported += [(alias, names) for alias, names in facts["aliases"]]
    friends = frozenset(friend_set)
    # A header can declare the alias that another file casts to, so each
    # namespace-scope alias of a header joins the proof names before any
    # file is judged.  An alias of a class body stays in its own file.
    proofs = alias_closure(frozenset(qualified), exported)
    lexical: dict[str, list[int]] = defaultdict(list)
    for path, records in records_of.items():
        admitted = frozenset(primaries_for(path, primaries))
        for rule, key, line in judge(records, proofs, friends, admitted):
            lexical[f"{path}:{rule}:{key}"].append(line)
    preprocessed: dict[str, int] = {}
    if compile_db is not None:
        preprocessed = preprocessed_counts(preprocessed_records(root, compile_db, failures, frozenset(files)),
                                           proofs, friends, primaries)
    entries, errors = read_allowlist(allowlist)
    unreviewed = 0
    for key in sorted(set(lexical) | set(preprocessed)):
        count = max(len(lexical.get(key, [])), preprocessed.get(key, 0))
        admitted = entries.get(key, (0, 0))[0]
        lines = ", ".join(map(str, lexical.get(key, []))) or "a macro expansion"
        if count > admitted:
            unreviewed += 1
            print(f"PROOF-ROUTE violation: {key} at line(s) {lines}: {count} site(s), {admitted} admitted.  "
                  f"Remove the route, or add a reviewed entry `{key}{' x' + str(count) if count > 1 else ''}` "
                  f"to {allowlist.name}.{qualified_names(key.split(':', 2)[-1], qualified)}", file=sys.stderr)
        elif mode == "list":
            print(f"REVIEWED  {key}  ({count} site(s), line(s) {lines})")
    stale = 0
    for key, (admitted, number) in sorted(entries.items(), key=lambda item: item[1][1]):
        found = max(len(lexical.get(key, [])), preprocessed.get(key, 0))
        if found < admitted:
            stale += 1
            print(f"PROOF-ROUTE stale: {allowlist.name}:{number} admits {admitted} site(s) of {key}, and the tree "
                  f"has {found}.  The list only shrinks: lower the count or delete the entry.", file=sys.stderr)
    for error in errors:
        print(f"PROOF-ROUTE malformed: {error}", file=sys.stderr)
    for failure in failures:
        print(f"PROOF-ROUTE unread input: {failure}.  A translation unit or a file that the guard cannot read can "
              f"hold a route, so the run is refused.", file=sys.stderr)
    print(f"check-proof-routes: {len(proofs)} proof name(s), {len(friends)} befriended template(s), "
          f"{len(set(lexical) | set(preprocessed))} key(s), {unreviewed} unreviewed, {stale} stale, "
          f"{len(errors)} malformed, {len(failures)} unread input(s).", file=sys.stderr)
    if unreviewed:
        return 1
    return 2 if stale or errors or failures else 0


SELF_TEST_SOURCE = r"""
#include <memory>
namespace forge {
class Door;
template <class T> class Guarded { template <class U> friend class Opener; template <class V> friend class ::forge::Other; };
union Loose { unsigned char byte; Door* door; };
template <class P> union Held { unsigned char byte; P proof; };
using Alias = Door;
void route(void* raw) {
    auto* first = static_cast<Door*>(raw);
    auto* second = static_cast<Alias*>(raw);
    auto& third = *reinterpret_cast<const Door*>(raw);
    std::allocator<Door> pool;
    void* bytes = ::operator new(sizeof(Door));
    (void)first; (void)second; (void)third; (void)pool; (void)bytes;
}
template class Guarded<int>;
template <> void route_twice<Door>(void*);
template <auto M> struct Steal {};
template <> struct Steal<&Door::secret> {};
template <> class Opener<int> {};
template <class U> class Opener<U*> {};
// union Commented { int a; };
const char* text = "union InString { int a; };";
void fine(int* p) { auto* q = static_cast<long*>(static_cast<void*>(p)); (void)q; x.template get<0>(); }
void also_fine() { auto* n = static_cast<Door*>(nullptr); auto f = static_cast<Door (*)(int)>(&make); }
template <class Door> Door* rebind(void* raw) { return static_cast<Door*>(raw); }
Door* after_template(void* raw) { return static_cast<Door*>(raw); }
template <class T, class Door = T> struct Box { Door* get(void* raw) { return static_cast<Door*>(raw); } };
template <class Door> concept Castable = requires(void* raw) { static_cast<Door*>(raw); };
template <template <class> class Door> void* hold(void* raw) { return static_cast<Door<int>*>(raw); }
template <class Door>
void* grab() { return ::operator new(sizeof(Door)); }
template <> class Other<long> {};
void conditional(void* raw) { auto* c = static_cast<std::conditional_t<(1 > 0), Door, int>*>(raw); (void)c; }
template <class T> using Rebound = typename T::
    template rebind<int>;
auto lam = []<class Door>(void* raw) { return static_cast<Door*>(raw); };
template <class T, class X = Door> struct Defaulted { Door* get(void* raw) { return static_cast<Door*>(raw); } };
}
"""


def planted_records(text: str) -> tuple[list[list], frozenset[str]]:
    """Parse one planted source, and return its records and the templates it befriends."""
    with tempfile.TemporaryDirectory() as scratch:
        path = Path(scratch) / "planted.cpp"
        path.write_text(text, encoding="utf-8")
        tree = next(iter(tsast.parse([path], strict=False)))
        if tree.diagnostic is not None:
            raise ValueError(f"the planted source does not parse: {tree.diagnostic}")
        return extract(tree), frozenset(template_friends(tree))


def self_test() -> int:
    """Plant each route in a scratch tree, with a scratch cache root, and prove the verdicts."""
    with cache_dir.scratch_root() as caches:
        return planted_cases(caches)


def planted_cases(caches: Path) -> int:
    """Plant each route in a scratch tree and prove the verdicts.

    Args:
        caches: The scratch root of the caches, where the store keeps its manifests and results
    """
    failures: list[str] = []
    proofs = frozenset({"Door"})
    records, friends = planted_records(SELF_TEST_SOURCE)
    if friends != {"Opener", "Other"}:
        failures.append(f"the befriended templates are {sorted(friends)}, not Opener and the qualified Other")
    by_rule: dict[str, list[str]] = defaultdict(list)
    for rule, key, _ in judge(records, proofs, friends, frozenset()):
        by_rule[rule].append(key)
    expected = {
        "union": ["Loose", "Held"],
        # The fourth is the cast after the template: a parameter named Door
        # binds only inside its own declaration.  The fifth closes its
        # template argument list after a parenthesized `>`, and the sixth is
        # in a template whose default argument names Door, which binds
        # nothing.  The lambda's own parameter named Door binds.
        "pointer-cast": ["static_cast<Door*>", "static_cast<Alias*>", "reinterpret_cast<const Door*>",
                         "static_cast<Door*>", "static_cast<std::conditional_t<(1>0),Door,int>*>",
                         "static_cast<Door*>"],
        "allocator": ["allocator<Door>"],
        "raw-allocation": ["operator new"],
        # The dependent `template rebind<int>` after a line break is no
        # instantiation.
        "explicit-instantiation": ["class Guarded<int>"],
        "function-specialization": ["void route_twice<Door>"],
        "member-pointer-specialization": ["struct Steal<&Door::secret>"],
        # Other is befriended by its qualified name.
        "friend-template-specialization": ["class Opener<int>", "template<class U>class Opener<",
                                           "class Other<long>"],
    }
    for rule, keys in expected.items():
        if sorted(by_rule.get(rule, [])) != sorted(keys):
            failures.append(f"rule {rule}: found {sorted(by_rule.get(rule, []))}, expected {sorted(keys)}")
    unexpected = set(by_rule) - set(expected)
    if unexpected:
        failures.append(f"a rule fired that the plant does not hold: {sorted(unexpected)}")
    # The defining file of a primary may specialize it.
    if any(r == "friend-template-specialization" and "Opener" in k
           for r, k, _ in judge(records, proofs, friends, frozenset({"Opener"}))):
        failures.append("a specialization in the file that defines the primary template was refused")
    # The allowlist admits a site, refuses a surplus and reports a stale entry.
    with tempfile.TemporaryDirectory() as scratch:
        root = Path(scratch)
        (root / "utils" / "scripts").mkdir(parents=True)
        (root / "utils" / "scripts" / "witness-roster.txt").write_text(
            "forge::Door | h | forge::Door{} | private | closed\n")
        (root / "a.cpp").write_text("union Plain { int a; };\nunion Other { int b; };\n")
        allow = root / "utils" / "scripts" / "allow.txt"
        allow.write_text("# plain scalars\na.cpp:union:Plain\n")
        if scan(root, None, None, allow, "check") != 1:
            failures.append("a union with no entry did not exit 1")
        allow.write_text("# plain scalars\na.cpp:union:Plain\na.cpp:union:Other\na.cpp:union:Gone\n")
        if scan(root, None, None, allow, "check") != 2:
            failures.append("a stale entry did not exit 2")
        allow.write_text("# plain scalars\na.cpp:union:Plain\na.cpp:union:Other\n")
        if scan(root, None, None, allow, "check") != 0:
            failures.append("a tree whose sites all have entries did not exit 0")
        allow.write_text("a.cpp:union:Plain\na.cpp:union:Other\n")
        if scan(root, None, None, allow, "check") != 2:
            failures.append("an entry with no comment above it was accepted")
        # A report names the qualified class that the dump gives for a
        # spelled name, and the class that the roster writes.
        (root / "b.cpp").write_text("void f(void* raw) { auto* key = static_cast<Key*>(raw); (void)key; }\n"
                                    "void g(void* raw) { auto* door = static_cast<Door*>(raw); (void)door; }\n")
        dump = root / "proof-names.sh"
        dump.write_text("#!/bin/sh\nprintf 'Key\\t::forge::detail::Key\\n'\n")
        dump.chmod(0o755)
        allow.write_text("# plain scalars\na.cpp:union:Plain\na.cpp:union:Other\n")
        report = io.StringIO()
        with contextlib.redirect_stderr(report):
            verdict = scan(root, None, str(dump), allow, "check")
        for needle in ("b.cpp:pointer-cast:static_cast<Key*>", "The key names Key = ::forge::detail::Key.",
                       "b.cpp:pointer-cast:static_cast<Door*>", "The key names Door = forge::Door."):
            if verdict != 1 or needle not in report.getvalue():
                failures.append(f"the report of a cast to a proof type lacks `{needle}`:\n{report.getvalue()}")
        (root / "b.cpp").unlink()
        # A header declares an alias of a proof type, and another file casts
        # to the alias.  Each file alone names no proof type at the cast.
        (root / "include").mkdir()
        (root / "include" / "A.h").write_text("namespace forge { class Door; using DoorHandle = Door; }\n")
        (root / "y.cpp").write_text('#include "include/A.h"\n'
                                    "void f(void* raw) { auto* d = static_cast<forge::DoorHandle*>(raw); (void)d; }\n")
        report = io.StringIO()
        with contextlib.redirect_stderr(report):
            verdict = scan(root, None, None, allow, "check")
        if verdict != 1 or "y.cpp:pointer-cast:static_cast<forge::DoorHandle*>" not in report.getvalue():
            failures.append(f"a cast through an alias that another file declares was not refused:\n{report.getvalue()}")
        (root / "y.cpp").unlink()
        (root / "include" / "A.h").unlink()
        # A forward declaration of a befriended template defines nothing, so
        # its file may not specialize the template.
        (root / "include" / "F.h").write_text("namespace forge {\ntemplate <class T> class Box {\n"
                                              "    template <class U> friend class Opener;\n};\n"
                                              "template <class T> class Opener;\ntemplate <> class Opener<int> {};\n}\n")
        report = io.StringIO()
        with contextlib.redirect_stderr(report):
            verdict = scan(root, None, None, allow, "check")
        if verdict != 1 or "include/F.h:friend-template-specialization:class Opener<int>" not in report.getvalue():
            failures.append(f"a specialization beside a forward declaration of its primary was admitted:\n"
                            f"{report.getvalue()}")
        (root / "include" / "F.h").unlink()
        # A file that the parser cannot read can hold a route, so it refuses
        # the run even when every site it can read has an entry.
        (root / "bad.cpp").write_text("#define JOIN(a, b) a##b\nJOIN(uni, on) Hidden { int x; };\n")
        report = io.StringIO()
        with contextlib.redirect_stderr(report):
            verdict = scan(root, None, None, allow, "check")
        if verdict != 2 or "PROOF-ROUTE unread input: bad.cpp" not in report.getvalue():
            failures.append(f"a file that the parser cannot read did not refuse the run:\n{report.getvalue()}")
        (root / "bad.cpp").unlink()
        # Token pasting forms a union that the lexical pass cannot see, and
        # the preprocessed pass sees it.  A second run reads the cache.  The
        # file parses clean, so the lexical pass reads it and finds nothing.
        compiler = os.environ.get("CXX") or shutil.which("c++") or shutil.which("g++")
        if compiler is None:
            failures.append("no C++ compiler to run the preprocessed pass")
        else:
            (root / "a.cpp").write_text("#define JOIN(a, b) a##b\n#define HIDE(n) JOIN(uni, on) n { int x; }\n"
                                        "HIDE(Hidden);\n")
            database = root / "compile_commands.json"
            database.write_text(json.dumps([{"directory": str(root), "file": "a.cpp",
                                             "command": f"{compiler} -std=c++20 -c a.cpp -o a.o"}]))
            allow.write_text("# none\n")
            for attempt in ("cold", "cached"):
                report = subprocess.run([sys.executable, __file__, "--scan-root", str(root), "--compile-db",
                                         str(database), "--allowlist", str(allow)], capture_output=True, text=True)
                if (report.returncode != 1 or "a.cpp:union:Hidden" not in report.stderr
                        or "PROOF-ROUTE unread input" in report.stderr):
                    failures.append(f"the {attempt} preprocessed pass missed a union formed by token pasting:\n"
                                    f"{report.stderr}")
            if not any((caches / "preprocessed" / "results").glob("proof-routes-*/*/*.json")):
                failures.append("the preprocessed pass wrote no result to the store")
            if not any((caches / "preprocessed" / "units").glob("*/*.json")):
                failures.append("the preprocessed pass did not use the shared store")
            # A layer fixture compiles against a staged root that links only to
            # a lower layer, and it includes a higher one.  The pass reads it
            # with the include directory, so the union that its macro forms is
            # found and no preprocessor failure refuses the run.
            (root / "include" / "lower").mkdir(parents=True, exist_ok=True)
            (root / "include" / "upper").mkdir()
            (root / "include" / "upper" / "Up.h").write_text("#pragma once\n#define GLUE(a, b) a##b\n"
                                                             "#define STAGE(n) GLUE(uni, on) n { int x; }\n")
            (root / "layer-stage").mkdir()
            (root / "layer-stage" / "lower").symlink_to(root / "include" / "lower")
            (root / "fixture.cpp").write_text("#include <upper/Up.h>\nSTAGE(Staged);\n")
            database.write_text(json.dumps([{"directory": str(root), "file": "fixture.cpp",
                                             "command": f"{compiler} -std=c++20 -Ilayer-stage -c fixture.cpp "
                                                        f"-o fixture.o"}]))
            report = subprocess.run([sys.executable, __file__, "--scan-root", str(root), "--compile-db",
                                     str(database), "--allowlist", str(allow)], capture_output=True, text=True)
            if "fixture.cpp:union:Staged" not in report.stderr or "PROOF-ROUTE unread input" in report.stderr:
                failures.append(f"a layer fixture under a staged root was not read with the include directory:\n"
                                f"{report.stderr}")
        missing = subprocess.run([sys.executable, __file__, "--scan-root", str(root), "--proof-names-binary",
                                  str(root / "not-built"), "--allowlist", str(allow)], capture_output=True, text=True)
        if missing.returncode != 3 or "is not built" not in missing.stderr:
            failures.append(f"a proof-name binary that is not built did not skip with exit 3:\n{missing.stderr}")
    # In a work tree, an untracked file is out of scope, because the export
    # of a guard run can appear under the tree while the guard reads it.
    with tempfile.TemporaryDirectory() as scratch:
        root = Path(scratch)
        throwaway_repo.init(root)
        (root / "utils" / "scripts").mkdir(parents=True)
        allow = root / "utils" / "scripts" / "allow.txt"
        allow.write_text("# none\n")
        (root / "clean.cpp").write_text("int clean = 0;\n")
        subprocess.run(["git", "-C", str(root), "add", "-A"], check=True, capture_output=True)
        (root / "grun" / "change").mkdir(parents=True)
        (root / "grun" / "change" / "export.cpp").write_text("union Exported { int a; float b; };\n")
        if scan(root, None, None, allow, "check") != 0:
            failures.append("an untracked file was read")
        subprocess.run(["git", "-C", str(root), "add", "-A"], check=True, capture_output=True)
        report = io.StringIO()
        with contextlib.redirect_stderr(report):
            verdict = scan(root, None, None, allow, "check")
        if verdict != 1 or "grun/change/export.cpp:union:Exported" not in report.getvalue():
            failures.append(f"the same file was not refused once git tracks it:\n{report.getvalue()}")
    for failure in failures:
        print(f"check-proof-routes: SELF-TEST FAILED: {failure}", file=sys.stderr)
    if failures:
        return 2
    print(f"check-proof-routes: self-test passed ({sum(len(v) for v in expected.values())} planted sites found)")
    return 0


def main(argv: list[str]) -> int:
    """Parse the arguments and run the scan or the self-test."""
    try:
        return run(argv)
    except tsast.KitMissing as exc:
        print(f"check-proof-routes: {exc}", file=sys.stderr)
        return 3


def run(argv: list[str]) -> int:
    """Run the mode that the arguments name."""
    if argv == ["--self-test"]:
        return self_test()
    root = REPO_ROOT
    compile_db: Path | None = None
    binary: str | None = None
    allowlist: Path | None = None
    mode = "check"
    while argv:
        flag = argv.pop(0)
        if flag == "--compile-db" and argv:
            compile_db = Path(argv.pop(0)).resolve()
        elif flag == "--proof-names-binary" and argv:
            binary = argv.pop(0)
        elif flag == "--list":
            mode = "list"
        # The self-test scans a scratch tree with its own allowlist.
        elif flag == "--scan-root" and argv:
            root = Path(argv.pop(0)).resolve()
        elif flag == "--allowlist" and argv:
            allowlist = Path(argv.pop(0)).resolve()
        else:
            print(__doc__, file=sys.stderr)
            return 2
    for named, what in ((binary, "the proof-name binary"), (compile_db, "the compile database")):
        if named is not None and not Path(named).is_file():
            print(f"check-proof-routes: {what} {named} is not built, so the guard cannot run.  Build the tree "
                  f"first.", file=sys.stderr)
            return 3
    return scan(root, compile_db, binary, allowlist or root / "utils" / "scripts" / "proof-routes-allowlist.txt",
                mode)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
