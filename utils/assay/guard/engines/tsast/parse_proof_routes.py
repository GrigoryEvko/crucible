"""parse-proof-routes — no file holds an explicit instantiation, or an allocation with `sizeof` of a proof type.

THE SHAPES
    A proof type is a context, a key, a capability, a permission, a pin or
    a placement.  Only the door that its header names builds one.  The
    quarantine plugin refuses the shapes that build a proof type with no call
    to its door, on the semantic tree of each unit
    (assay.guard.engines.plugin.proof_routes).  GCC keeps no tree of two
    shapes that a plugin can read.  This rule reads these two shapes on the
    parse tree of each file:
      explicit-instantiation  an explicit instantiation does no access check
                              of the names in it.  A pointer to a private
                              member of a proof type can then become available
                              through one.  At the end of a unit, an explicit
                              instantiation of a class has the location of its
                              template.  An explicit instantiation of a
                              function is on no list of the plugin interface.
      raw-allocation          a call of `::operator new`, `malloc`, `calloc`,
                              `realloc` or `aligned_alloc` with `sizeof` of a
                              proof type.  In a function that is not a
                              template, GCC folds each `sizeof` in the
                              arguments of a call to a constant before a plugin
                              reads the body.
    The entity of a finding is `SHAPE: KEY`.  The key of an explicit
    instantiation is its declaration.  The key of an allocation is the name
    of the function that it calls.

THE PROOF NAMES
    The kind raw-allocation reads the names of the proof types from two
    sources.  The witness rows of utils/rule-table.txt name the proof types
    of the base.  With a build directory, the program test_forgeable_proofs
    of that build prints each class that its reflection walk finds with the
    shape of a proof (`--proof-names`).  A type alias or a typedef that names
    a proof type is a proof name too, in its file.  An alias at namespace
    scope in a header is a proof name in each file, because each file that
    includes the header can name it.  An alias name has no namespace here.
    An alias of a different namespace with the same name is then a proof name
    too, and the rule refuses more and not less.

    A template parameter of an enclosing template or of an enclosing generic
    lambda hides a proof type with the same name in its declaration.  A
    default argument declares no name.

THE TWO PARSES
    The rule parses each C++ file of its scope with the kit of
    assay.readers.tsast.  With a build directory, the store of
    assay.guard.preprocessed also runs each unit of the compile database
    through the preprocessor with the flags of the build.  The rule then
    parses each expansion of each file of its scope.  It also finds a shape
    that a macro or the operator `##` makes.  The count of a key is the larger
    count of the two parses.  The store keeps the records of an expansion
    with the key of its chunk list, and a warm run parses no expansion.  A
    preprocessor failure and an expansion that the kit cannot read are errors
    of the kind unread, because a shape in them is not known.

THE SCOPE
    The scope holds each C++ file of the tree, but no negative-compile
    fixture and no BPF program.  A fixture is a file of a directory neg or
    NAME_neg in test/.  It must not compile, and it builds nothing.  A BPF
    program is a file of a directory bpf in include/crucible/NAME/.  A shape
    with a template parameter is out of scope: `sizeof(T)` names T and not a
    proof type, and the rule cannot know which argument T gets.

THE LEDGER
    utils/ledgers/parse-proof-routes.txt admits a reviewed site with a count
    and a reason (assay.core.ledger).  The reason tells why the site builds
    no proof type.

Complexity: linear in the bytes of the files and of the expansions, plus one alias walk for each new proof name.
"""

from collections import defaultdict
from collections.abc import Iterable, Sequence
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

from assay.core import proc, ruletable
from assay.core import report as check_report
from assay.core.paths import REPO_ROOT
from assay.guard.engines.meta import runner as meta_runner
from assay.guard.engines.tsast import banned_calls, header_checks
from assay.guard.engines.tsast.runner import TsastContext
from assay.guard.preprocessed import Store, chunk_text
from assay.readers import cmake, tsast

RULE = "parse-proof-routes"
INSTANTIATION_KIND = "explicit-instantiation"
ALLOCATION_KIND = "raw-allocation"
UNREAD_KIND = "unread"
# The kind of a record of an alias declaration or a typedef, which names no shape.
ALIAS_KIND = "alias"
# The functions that give raw storage, by the last part of the name that a call spells.
RAW_ALLOCATORS = frozenset({"malloc", "calloc", "realloc", "aligned_alloc", "operator new"})
# The nodes that spell one part of a name.
NAME_LEAVES = ("identifier", "type_identifier", "namespace_identifier", "field_identifier")
# The suffixes of a header: a namespace-scope alias of a header is a proof name in each file.
HEADER_SUFFIXES = (".h", ".hpp")
# The longest key, in characters.
KEY_LIMIT = 160
# The program that prints the reflected proof names for its argument --proof-names.
PROOF_NAMES_TARGET = "test_forgeable_proofs"
TABLE = ruletable.TABLE.relative_to(REPO_ROOT)
WHY = {
    INSTANTIATION_KIND: "an explicit instantiation does no access check of the names in it, so a pointer to a "
    "private member of a proof type can become available through it",
    ALLOCATION_KIND: "the call gives raw storage with the size of a proof type, and a cast of that storage makes a "
    "proof with no call to its door",
}


@dataclass(frozen=True, slots=True)
class Record:
    """One record of a parse: a shape or an alias, with its key, its line and the names that it tests.

    names is None for a shape that tests no name.  A record holds no proof
    name.  The cache of a record then stays correct when the proof names
    change.
    """

    kind: str
    key: str
    line: int
    names: tuple[str, ...] | None

    def to_json(self) -> list[object]:
        """Return the record as a JSON list for the store of results."""
        return [self.kind, self.key, self.line, None if self.names is None else list(self.names)]


def record_of(raw: object) -> Record | None:
    """Return the record of a JSON list that to_json() wrote, or None for a value of a different shape."""
    if not isinstance(raw, list) or len(raw) != 4:
        return None
    kind, key, line, names = raw
    if not isinstance(kind, str) or not isinstance(key, str) or not isinstance(line, int):
        return None
    if names is None:
        return Record(kind, key, line, None)
    if isinstance(names, list) and all(isinstance(name, str) for name in names):
        return Record(kind, key, line, tuple(names))
    return None


def records_of(raw: object) -> list[Record] | None:
    """Return the records of a JSON list of records, or None when one item has a different shape."""
    if not isinstance(raw, list):
        return None
    found = [record_of(item) for item in raw]
    return None if any(item is None for item in found) else [item for item in found if item is not None]


def is_word(character: str) -> bool:
    """Return True for a character that can continue an identifier or a number."""
    return character.isalnum() or character == "_"


def canonical(text: str) -> str:
    """Return the key of a text: one space between two word characters, and no other white space.

    The key is an identity for the ledger, and this change of the white space keeps each key stable.  Complexity:
    linear in the length of the text.
    """
    out: list[str] = []
    has_space = False
    for character in text.strip():
        if character.isspace():
            has_space = True
            continue
        if has_space and out and is_word(out[-1][-1]) and is_word(character):
            out.append(" ")
        has_space = False
        out.append(character)
    return "".join(out)


def span_key(node: tsast.Node, start: tuple[int, int], end: tuple[int, int]) -> str:
    """Return the key of the source from start to end in a node, with no comment.

    A comment is its own node, and the function removes its text before it changes the white space.
    """
    pieces: list[str] = []
    cursor = start
    for comment in node.descendants("comment"):
        if comment.start < start or comment.end > end:
            continue
        pieces.append(node.tree.slice(cursor, comment.start))
        pieces.append(" ")
        cursor = comment.end
    pieces.append(node.tree.slice(cursor, end))
    return canonical("".join(pieces))


def hidden_names(node: tsast.Node) -> set[str]:
    """Return the names that the template parameter lists around a node declare: of a template or of a lambda."""
    hidden: set[str] = set()
    owner = node.ancestor_of_type("template_declaration", "lambda_expression")
    while owner is not None:
        field = "parameters" if owner.type == "template_declaration" else "template_parameters"
        hidden |= banned_calls.template_parameter_names(owner.child_by_field(field))
        owner = owner.ancestor_of_type("template_declaration", "lambda_expression")
    return hidden


def names_in(node: tsast.Node) -> tuple[str, ...]:
    """Return the names that a subtree spells, sorted, less the template parameters around it."""
    spelled = {leaf.text for leaf in node.descendants(*NAME_LEAVES)}
    if node.type in NAME_LEAVES:
        spelled.add(node.text)
    return tuple(sorted(spelled - hidden_names(node)))


def alias_names(node: tsast.Node) -> list[str]:
    """Return the names that one alias declaration or typedef declares."""
    if node.type == "alias_declaration":
        named = node.child_by_field("name")
        return [] if named is None else [named.text]
    return [
        leaf.text
        for child in node.children
        if child.field == "declarator"
        for leaf in ([child] if child.type == "type_identifier" else child.descendants("type_identifier"))
    ]


def extract(tree: tsast.Tree) -> list[Record]:
    """Return each shape and each alias of one parse.  Complexity: linear in the number of nodes."""
    records: list[Record] = []
    for node in tree.find("alias_declaration", "type_definition"):
        body = node.child_by_field("type")
        if body is not None:
            records += [Record(ALIAS_KIND, name, node.line, names_in(body)) for name in alias_names(node)]
    for call in tree.find("call_expression"):
        function = call.child_by_field("function")
        name = None if function is None else tsast.leaf_name(function)
        arguments = call.child_by_field("arguments")
        if name not in RAW_ALLOCATORS or arguments is None:
            continue
        sizes = list(arguments.descendants("sizeof_expression"))
        if sizes:
            spelled = tuple(sorted({part for size in sizes for part in names_in(size)}))
            records.append(Record(ALLOCATION_KIND, str(name), call.line, spelled))
    for node in tree.find("template_instantiation"):
        parts = [child for child in node.children if child.type != "comment"]
        if parts:
            key = span_key(node, parts[0].start, parts[-1].end)[:KEY_LIMIT]
            records.append(Record(INSTANTIATION_KIND, key, node.line, None))
    return records


def namespace_aliases(tree: tsast.Tree) -> list[tuple[str, tuple[str, ...]]]:
    """Return each alias and typedef at namespace scope of one parse, with the names of its definition.

    Complexity: linear in the number of nodes.
    """
    found: list[tuple[str, tuple[str, ...]]] = []
    for node in tree.find("alias_declaration", "type_definition"):
        body = node.child_by_field("type")
        if body is not None and header_checks.is_at_eager_scope(node):
            found += [(name, names_in(body)) for name in alias_names(node)]
    return found


def alias_closure(proofs: frozenset[str], aliases: Iterable[tuple[str, tuple[str, ...]]]) -> frozenset[str]:
    """Return the proof names, with each alias of a type that names a proof name, until no alias joins them.

    Complexity: linear in the number of aliases for each turn of the loop, and each turn but the last adds a name.
    """
    names = set(proofs)
    pending = list(aliases)
    has_grown = True
    while has_grown:
        has_grown = False
        remaining: list[tuple[str, tuple[str, ...]]] = []
        for alias, parts in pending:
            if alias in names:
                continue
            if any(part in names for part in parts):
                names.add(alias)
                has_grown = True
            else:
                remaining.append((alias, parts))
        pending = remaining
    return frozenset(names)


def judge(records: Sequence[Record], proofs: frozenset[str]) -> list[Record]:
    """Return the shapes in the records for these proof names, with the aliases of the file of the records.

    Complexity: linear in the number of records, plus the alias closure of the file.
    """
    local = alias_closure(proofs, ((item.key, item.names or ()) for item in records if item.kind == ALIAS_KIND))
    return [
        item
        for item in records
        if item.kind != ALIAS_KIND and (item.names is None or any(name in local for name in item.names))
    ]


def is_in_scope(path: str) -> bool:
    """Return True for a file that the rule reads: no negative-compile fixture and no BPF program (THE SCOPE)."""
    parts = PurePosixPath(path).parts
    is_fixture = len(parts) > 2 and parts[0] == "test" and (parts[-2] == "neg" or parts[-2].endswith("_neg"))
    is_bpf = len(parts) > 4 and parts[:2] == ("include", "crucible") and parts[3] == "bpf"
    return not is_fixture and not is_bpf


def roster_names(root: Path) -> set[str]:
    """Return the last part of the name of each class that a witness row of the rule table names."""
    table = root / TABLE
    rows = ruletable.load(table if table.is_file() else REPO_ROOT / TABLE).witnesses
    return {row.name.rsplit("::", 1)[-1] for row in rows}


def reflected_names(build_dir: Path | None) -> set[str]:
    """Return the names that test_forgeable_proofs --proof-names of a build prints, or none with no build.

    Each line of the program is a name as a source spells it, a tab and the full name.

    Raises:
        NotApplicable: If the build has no such program, or the program is not built
    """
    if build_dir is None:
        return set()
    try:
        targets = cmake.codemodel(build_dir).targets
    except cmake.CmakeError as problem:
        raise meta_runner.NotApplicable(f"the code model of {build_dir} cannot be read: {problem}") from None
    found = next((target for target in targets if target.name == PROOF_NAMES_TARGET and target.artifacts), None)
    if found is None or not Path(found.artifacts[0]).is_file():
        raise meta_runner.NotApplicable(
            f"the program {PROOF_NAMES_TARGET} of {build_dir} is not built.  Build the tree"
        )
    result = proc.require(proc.run(proc.Command([found.artifacts[0], "--proof-names"], timeout_s=proc.QUICK_S)))
    return {line.partition("\t")[0].strip() for line in result.out.splitlines() if line.strip()}


def parse_expansions(batch: list[tuple[str, str, str, list[str]]]) -> list[list[list[object]] | None]:
    """Return the records of each expansion of a batch as JSON lists, or None for a text that the kit cannot read.

    Store.map_batches runs this function in a worker process.  Each item is
    (store directory, key, path, chunk digests).  Complexity: one parse of
    each text, linear in its nodes.
    """
    texts = [
        (f"{path} (expansion {key[:12]})", "\n".join(chunk_text(Path(store), digest) for digest in digests))
        for store, key, path, digests in batch
    ]
    return [
        None if tree.diagnostic is not None else [item.to_json() for item in extract(tree)]
        for tree in tsast.parse_texts(texts, strict=False)
    ]


def expansion_records(
    build_dir: Path, root: Path, scope: frozenset[str], unread: list[tuple[str, str]]
) -> list[tuple[str, list[Record]]]:
    """Return the records of each expansion of each file of the scope, as (path, records).

    The function adds to unread each unit that the preprocessor refuses and
    each expansion that the kit cannot read, as (path, reason).

    Complexity: linear in the units times the files that each one reads, plus one parse for each new expansion.
    """
    store = Store(build_dir, root)
    results = store.results(RULE, Path(__file__), tsast.parser_identity())
    known: dict[str, list[Record]] = {}
    pending: dict[str, tuple[str, list[str]]] = {}
    expansions: list[tuple[str, str]] = []
    seen: set[tuple[str, str]] = set()
    for expansion in store.expansions():
        path = expansion.path
        if path not in scope or (path, expansion.key) in seen:
            continue
        seen.add((path, expansion.key))
        expansions.append((path, expansion.key))
        if expansion.key in known or expansion.key in pending:
            continue
        cached = records_of(results.get(expansion.key))
        if cached is not None:
            known[expansion.key] = cached
        else:
            pending[expansion.key] = (path, [chunk.digest for chunk in expansion.chunks])
    # Each failure of the store is "UNIT: REASON".
    unread += [
        (unit or str(build_dir), reason) for unit, _, reason in (item.partition(": ") for item in store.failures)
    ]
    items = [(str(store.directory), key, path, digests) for key, (path, digests) in pending.items()]
    for (_store, key, path, _digests), found in zip(items, store.map_batches(parse_expansions, items), strict=True):
        records = None if found is None else records_of(found)
        if records is None:
            unread.append((path, "the kit cannot read one expansion of this file"))
            continue
        known[key] = records
        results.put(key, [item.to_json() for item in records])
    return [(path, known[key]) for path, key in expansions if key in known]


def findings(context: TsastContext) -> list[check_report.Finding]:
    """Give an error for each explicit instantiation and each raw allocation of a proof type, in the two parses."""
    scope = [path for path in context.scope_files() if is_in_scope(path)]
    file_records: dict[str, list[Record]] = {}
    exported: list[tuple[str, tuple[str, ...]]] = []
    for tree in context.trees(scope):
        path = tree.path.as_posix()
        file_records[path] = extract(tree)
        if path.endswith(HEADER_SUFFIXES):
            exported += namespace_aliases(tree)
    # A header can declare the alias that a different file names.  Each alias at namespace scope of a header is then
    # a proof name before the rule judges a file.
    proofs = alias_closure(frozenset(roster_names(context.root) | reflected_names(context.build_dir)), exported)
    lines: dict[tuple[str, str, str], list[int]] = defaultdict(list)
    for path, records in file_records.items():
        for item in judge(records, proofs):
            lines[(path, item.kind, item.key)].append(item.line)
    expanded: dict[tuple[str, str, str], int] = defaultdict(int)
    unread: list[tuple[str, str]] = []
    if context.build_dir is not None:
        for path, records in expansion_records(context.build_dir, context.root, frozenset(scope), unread):
            counts: dict[tuple[str, str, str], int] = defaultdict(int)
            for item in judge(records, proofs):
                counts[(path, item.kind, item.key)] += 1
            for site, count in counts.items():
                expanded[site] = max(expanded[site], count)
    found: list[check_report.Finding] = []
    for site in sorted(set(lines) | set(expanded)):
        path, kind, key = site
        written = lines.get(site, [])
        places = written + [0] * max(0, expanded.get(site, 0) - len(written))
        for line in places:
            where = "" if line else "  The site is in a macro expansion of the file."
            found.append(
                check_report.Finding(
                    "error",
                    path,
                    line,
                    RULE,
                    f"{WHY[kind]} ({key}).{where}  Remove the shape, or give the site a reviewed row with its reason "
                    f"in the ledger",
                    kind,
                    f"{kind}: {key}",
                )
            )
    for path, reason in unread:
        found.append(
            check_report.Finding(
                "error",
                path,
                0,
                RULE,
                f"{reason}.  A shape in an input that the rule cannot read is not known, so the rule refuses the run",
                UNREAD_KIND,
                reason,
            )
        )
    return found
