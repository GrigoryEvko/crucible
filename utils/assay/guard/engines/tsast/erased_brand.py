"""erased-brand — count each spelling of a branded type that names no brand, and let no count increase.

THE BRAND
    A brand is a template parameter of a region, view, borrow or permission
    template, and it names one instance.  A spelling that names no brand,
    such as `Permission<Tag>`, gets the erased identity DefaultBrand.  Each
    token of that identity is the same as each other token of its tag.
    foundation/Brand.h gives the rules of a brand.

WHY THE RULE READS THE PARSE TREE
    GCC folds a static_assert that is not dependent and a sizeof while it
    parses, and it keeps no use location of a type in them.  Most of the
    spellings of the tree are in a static_assert or a sizeof.  The
    negative-compile fixtures compile in no build.  A spelling is a fact of
    the source, and the rule reads the parse tree of the pinned kit
    (assay.readers.tsast).

THE BRANDED TEMPLATES
    The rule finds the branded templates in the parse trees of include/, and
    it holds no list of names.  A class template or an alias template is
    branded when a template parameter of it has the name Brand.  It is
    branded too when such a parameter has a default that names DefaultBrand.
    The position of that parameter is the
    count of arguments that a spelling with no brand holds.  The rule knows a
    template by its full name.  fixy::Borrowed is then branded, and
    fixy::session::Borrowed is a different template.  Two primary
    declarations of one name with the brand at two positions give an error
    of the kind ambiguous-template.  A spelling of that name then has no one
    meaning.  `guard explain erased-brand` prints each branded template and
    the position of its brand (explained()).

THE TEMPLATE THAT A SPELLING NAMES
    The name lookup of assay.readers.tsast (NameIndex) gives the template:
    the first enclosing scope that declares the name, with the
    using-declarations, the using-directives and the namespace aliases at the
    spelling.  The index holds the declarations of include/ and of the file
    itself.  When no scope knows the head of a qualifier, the spelling can
    name a branded template that the index does not hold.  The rule then
    counts the spelling for each branded template of its last name.  A macro
    body has no enclosing scope until a file expands it, and the rule counts
    its spellings in the same way.

THE FILES OF THE SITES
    The scope of the row of utils/registry.txt: the headers and the tests of
    the two layers, include/foundation/, include/fixy/, test/foundation/ and
    test/fixy/, and the check files of the two layers in test/layer/checks/.
    A check file holds the checks that left its header.  A site that moves
    from a header to its check file then stays a site.  The negative-compile
    fixtures of test/foundation/neg/ and test/fixy/neg/ are in the scope too.
    No build compiles them, and the parse tree holds their sites.

THE SITES
    A site is a spelling of a branded template with an argument list that
    stops at the brand or before it, or names DefaultBrand.  The rule reads
    each node template_type and template_function that names a branded
    template, and the named arguments of its list.  An argument names the
    erased brand when its own last name is DefaultBrand.  Wrap<DefaultBrand>
    at the position of the brand is then a brand of its own.  A comment, a
    string literal and a raw string hold no node.  A list on more than one
    line is one node.  The rule parses the body of each #define as a fragment
    (tsast.macro_bodies), and it reads the sites of that tree.  An alias
    counts one time, at the alias, and not at each use.

THE KINDS
    erased-brand        a site.  The entity is the full name of the branded
                        template.  A spelling that counts for more than one
                        template names each one, with "or" between them
    unreadable          a macro body with more pairs of a branded name and
                        `<` in its preprocessing tokens than nodes of that
                        name in its parse.  The body is a fragment, and
                        `Permission<x` can parse as an expression with the
                        operator <.  The rule does not guess the arguments of
                        such a list
    ambiguous-template  THE BRANDED TEMPLATES
    parse               a file of the scope or of include/ that the kit does
                        not parse (the engine runner)
    REFUSED_KINDS holds the last three kinds: no ledger row admits such a
    finding, because the rule cannot count the sites of its file.

THE LEDGER
    utils/ledgers/erased-brand.txt counts the sites of each file and each
    branded template (assay.core.ledger, the policy count).  A file with more
    sites than its row is an error: write the brand, or a generic brand
    parameter.  A file with fewer sites than its row is an error too, until
    `guard write --rule erased-brand` writes the ledger again in the same
    commit.  The write refuses a count that rose, and `--raise` writes it,
    for a change of the rule that reads more sites.  A ledger with no row
    refuses each site, and that is the end state.

Usage
    guard check --rule erased-brand
    guard write --rule erased-brand [--raise]
    guard explain erased-brand

Complexity: linear in the nodes of the trees of include/ and of the scope, and in the tokens of each macro body.
"""

from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from itertools import pairwise

from assay.core import report as check_report
from assay.guard.engines.meta.runner import NotApplicable
from assay.guard.engines.tsast.runner import PARSE_KIND, TsastContext
from assay.readers import tsast

RULE = "erased-brand"
SITE_KIND = "erased-brand"
UNREADABLE_KIND = "unreadable"
AMBIGUOUS_KIND = "ambiguous-template"
# The kinds that no ledger row admits (THE KINDS).  assay.guard.engines.meta.runner reads this name.
REFUSED_KINDS = frozenset({UNREADABLE_KIND, AMBIGUOUS_KIND, PARSE_KIND})
# The directory of the declarations of the branded templates (THE BRANDED TEMPLATES).
TEMPLATE_SCOPE = ("include/",)
BRAND_PARAMETER = "Brand"
ERASED_BRAND = "DefaultBrand"
# The node types of a primary declaration of a class template or an alias template.
TEMPLATE_ITEMS = ("class_specifier", "struct_specifier", "union_specifier", "alias_declaration")
# The node types of a spelling of a template with an argument list.
SPELLINGS = ("template_type", "template_function")
ADVICE = "Write the brand of the type, or a generic brand parameter"

type QualifiedName = tuple[str, ...]


@dataclass(frozen=True, slots=True)
class Branded:
    """The branded templates of include/, and the index that the name lookup reads.

    `position` gives the position of the brand of each template, by its full
    name.  `by_last_name` gives the full names of the branded templates of
    each last name.
    """

    position: Mapping[QualifiedName, int]
    by_last_name: Mapping[str, tuple[QualifiedName, ...]]
    index: tsast.NameIndex


@dataclass(frozen=True, slots=True)
class Lookup:
    """The facts of one file that the name lookup reads: its namespace aliases, its usings and its own names."""

    aliases: Sequence[tsast.NamespaceAlias]
    usings: Sequence[tsast.UsingDecl]
    local: frozenset[QualifiedName]


def names_erased_brand(node: tsast.Node) -> bool:
    """Return True when a template argument or a parameter default is the erased brand itself.

    The last name of the node gives the answer.  `::foundation::brand::DefaultBrand` then names the erased brand,
    and `Wrap<DefaultBrand>` is a brand of its own.  The rule reads a type_descriptor through its field `type`.
    """
    target = node.child_by_field("type") if node.type == "type_descriptor" else node
    if target is None:
        return False
    parts = tsast.qualified_parts(target)
    return parts is not None and parts[1][-1:] == (ERASED_BRAND,)


def parameter_name(node: tsast.Node) -> str | None:
    """Return the name of one template parameter, or None for a parameter with no name."""
    for field in ("name", "declarator"):
        named = node.child_by_field(field)
        if named is not None:
            return named.text
    return next((child.text for child in node.children if child.type in ("type_identifier", "identifier")), None)


def is_brand_parameter(node: tsast.Node) -> bool:
    """Return True when a template parameter has the name Brand, or a default that is the erased brand."""
    if parameter_name(node) == BRAND_PARAMETER:
        return True
    default = node.child_by_field("default_type")
    return default is not None and names_erased_brand(default)


def declared_template(declaration: tsast.Node) -> str | None:
    """Return the name of the primary class template or alias template of a template declaration.

    The declaration of a specialization names a template_type.  A friend
    declaration and a function declare no class.  The function gives None for
    each of them.
    """
    item = next((child for child in declaration.children if child.type in TEMPLATE_ITEMS), None)
    named = item.child_by_field("name") if item is not None else None
    return named.text if named is not None and named.type == "type_identifier" else None


def derived(trees: Sequence[tsast.Tree]) -> tuple[Branded, list[check_report.Finding]]:
    """Find each branded template in the trees of include/, and the position of its brand.

    Complexity: linear in the nodes of the trees.

    Returns:
        The branded templates, and one error of the kind ambiguous-template for each template with two positions
    """
    positions: dict[QualifiedName, set[int]] = {}
    places: dict[QualifiedName, tuple[str, int]] = {}
    index = tsast.NameIndex()
    for tree in trees:
        index.add(tree, share_aliases=True)
        for declaration in tree.find("template_declaration"):
            name = declared_template(declaration)
            parameters = declaration.child_by_field("parameters")
            if name is None or parameters is None:
                continue
            listed = [child for child in parameters.children if not tsast.is_comment(child)]
            position = next((place for place, item in enumerate(listed) if is_brand_parameter(item)), None)
            if position is None:
                continue
            qualified = tsast.scope_levels(declaration)[0] + (name,)
            positions.setdefault(qualified, set()).add(position)
            places.setdefault(qualified, (tree.path.as_posix(), declaration.line))
    found: list[check_report.Finding] = []
    position_of: dict[QualifiedName, int] = {}
    for qualified in sorted(positions):
        if len(positions[qualified]) == 1:
            position_of[qualified] = next(iter(positions[qualified]))
            continue
        path, line = places[qualified]
        found.append(
            check_report.Finding(
                "error",
                path,
                line,
                RULE,
                f"two primary declarations of {'::'.join(qualified)} have the brand at the positions "
                f"{sorted(positions[qualified])}, and a spelling of the template then has no one meaning.  Make the "
                "positions agree",
                AMBIGUOUS_KIND,
                "::".join(qualified),
            )
        )
    by_last_name: dict[str, list[QualifiedName]] = {}
    for qualified in position_of:
        by_last_name.setdefault(qualified[-1], []).append(qualified)
    return Branded(position_of, {name: tuple(sorted(names)) for name, names in by_last_name.items()}, index), found


def lookup_of(tree: tsast.Tree) -> Lookup:
    """Return the facts of one parsed file for the name lookup."""
    local = tsast.NameIndex()
    local.add(tree)
    return Lookup(tsast.namespace_aliases(tree), tsast.using_names(tree), frozenset(local.names))


def referents(node: tsast.Node, branded: Branded, lookup: Lookup | None) -> tuple[QualifiedName, ...]:
    """Return the branded templates that one template-id can name.

    A spelling that the lookup cannot resolve counts for each branded
    template of its last name.  The rule then counts an unknown case.  A
    spelling that resolves to a template with no brand is no site.

    Args:
        node: A node template_type or template_function
        branded: The branded templates
        lookup: The facts of the file of the node, or None for a macro body.  The scope of a macro body is not known
    """
    name = node.child_by_field("name")
    candidates = branded.by_last_name.get(name.text, ()) if name is not None else ()
    path = tsast.qualified_path(node)
    if not candidates or lookup is None or path is None:
        return candidates
    site = tsast.lookup_site_of_parts(node, path[0], path[1], lookup.aliases, lookup.usings)
    found, _is_known = branded.index.resolve(site, lookup.local)
    if not found:
        return candidates
    return tuple(qualified for qualified in found if qualified in branded.position)


@dataclass(frozen=True, slots=True)
class Site:
    """One site (THE SITES): its node, the name that it spells and its entity."""

    node: tsast.Node
    name: str
    entity: str


def sites_of(tree: tsast.Tree, branded: Branded, lookup: Lookup | None) -> list[Site]:
    """Return each site of one tree (THE SITES).

    Complexity: linear in the nodes of the tree.

    Args:
        tree: A parsed file, or the tree of a macro body
        branded: The branded templates
        lookup: The facts of the file, or None for a macro body
    """
    found: list[Site] = []
    for node in tree.find(*SPELLINGS):
        name = node.child_by_field("name")
        arguments = node.child_by_field("arguments")
        if name is None or arguments is None or name.text not in branded.by_last_name:
            continue
        named = [child for child in arguments.children if not tsast.is_comment(child)]
        is_erased = any(names_erased_brand(child) for child in named)
        counted = [
            qualified
            for qualified in referents(node, branded, lookup)
            if is_erased or len(named) <= branded.position[qualified]
        ]
        if counted:
            found.append(Site(node, name.text, " or ".join("::".join(qualified) for qualified in counted)))
    return found


def unreadable_name(body: tsast.MacroBody, branded: Branded) -> str | None:
    """Return a branded name that a macro body spells before `<` more times than its parse holds its nodes, or None.

    A body that does not parse holds no node.  Each such pair of tokens in it then counts (THE KINDS).
    Complexity: linear in the tokens of the body.
    """
    tokens = tsast.pp_tokens(body.text, body.first_row)
    spelled: dict[str, int] = {}
    for token, following in pairwise(tokens):
        if token.kind == "identifier" and token.text in branded.by_last_name and following.text == "<":
            spelled[token.text] = spelled.get(token.text, 0) + 1
    read: dict[str, int] = {}
    if body.is_parsed:
        for node in body.root.descendants(*SPELLINGS):
            name = node.child_by_field("name")
            if name is not None and name.text in branded.by_last_name:
                read[name.text] = read.get(name.text, 0) + 1
    return next((name for name in sorted(spelled) if spelled[name] > read.get(name, 0)), None)


def site_finding(path: str, line: int, site: Site) -> check_report.Finding:
    """Return the error of one site at the line of its file."""
    return check_report.Finding(
        "error",
        path,
        line,
        RULE,
        f"{site.name}<...> names no brand, and it gets the erased identity DefaultBrand of {site.entity}.  Each token "
        f"of that identity is the same as each other token of its tag.  {ADVICE}",
        SITE_KIND,
        site.entity,
    )


def findings(context: TsastContext) -> list[check_report.Finding]:
    """Give an error for each site, each unreadable macro body and each template with two brand positions.

    The trees of include/ give the branded templates first.  The function
    keeps the trees of the files of the scope in them, and it loads each
    other file of the scope one time.
    """
    template_trees = list(context.trees(context.files(TEMPLATE_SCOPE)))
    branded, found = derived(template_trees)
    held = {tree.path.as_posix(): tree for tree in template_trees}
    scope = context.scope_files()
    held.update((tree.path.as_posix(), tree) for tree in context.trees([path for path in scope if path not in held]))
    trees = [held[path] for path in scope if path in held]
    for tree in trees:
        path = tree.path.as_posix()
        found += [site_finding(path, site.node.line, site) for site in sites_of(tree, branded, lookup_of(tree))]
    for body in context.macro_bodies(trees):
        path = body.define.tree.path.as_posix()
        name = unreadable_name(body, branded)
        if name is not None:
            found.append(
                check_report.Finding(
                    "error",
                    path,
                    body.define.line,
                    RULE,
                    f"the macro {body.name} spells {name}< more times than its parse holds a node of {name}, and the "
                    "rule cannot read the arguments of the list.  Close the list in the body, or write a body that "
                    "parses",
                    UNREADABLE_KIND,
                    f"#define {body.name}: {name}",
                )
            )
        elif body.is_parsed:
            found += [
                site_finding(path, body.origin(site.node)[0] + 1, site) for site in sites_of(body.tree, branded, None)
            ]
    return found


def explained(context: TsastContext) -> list[str]:
    """Return one line for each branded template and the position of its brand, for `guard explain erased-brand`.

    The line of a template with two positions tells the problem.

    Raises:
        NotApplicable: If the parse kit is not installed
    """
    try:
        branded, problems = derived(list(context.trees(context.files(TEMPLATE_SCOPE))))
    except tsast.KitMissing as problem:
        raise NotApplicable(f"the parse kit is not installed: {problem}") from None
    lines = [
        f"branded {'::'.join(qualified)}: the brand at position {position}"
        for qualified, position in sorted(branded.position.items())
    ]
    return lines + [f"{item.path}:{item.line}: {item.message}" for item in problems]
