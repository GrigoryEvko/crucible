"""layer-residual — each arm of a base file includes no higher project root, and a base directory holds C++ and prose.

THE RULE
    The quarantine plugin reads only the arms of a preprocessor conditional
    that a build compiles.  The rule layer-boundary and the include rules of
    the rule quarantine have this limit.  The kit does not preprocess, and
    this rule reads each #include directive of each arm of each base file.
    A base file is a file that a layer row of the rule table
    utils/rule-table.txt holds.  The rule also reads each file of a base
    directory that a compiler does not read.  The rule refuses these items:
      `include`   an include of a file of a higher project root, or of a
                  file that no layer row holds.  Also an include that names
                  a project root with no layer in the rule table
      `computed`  an include with a macro as its path (`#include MACRO`) that
                  no object-like macro of the same file resolves.  The rule
                  cannot see the target of such an include
      `unread`    a file of a base directory that is not C++ and not prose
                  (.md or .txt), because no rule of a compiler reads it.
    A file that does not parse is an error of the runner (THE PARSE ERRORS of
    assay.guard.engines.tsast.runner).  A file that tsast.UNPARSEABLE lists
    is an error too, because a base file must parse.

THE PROJECT ROOTS
    A project root is a directory in include/.  The rank of a root is the
    rank of the rule layer-boundary (assay.core.ruletable.root_ranks): the
    lowest rank of a layer row of the root.  A root of a quarantine row is
    above each layer.

    A base file can include a file of its own root and of each root with a
    lower rank.  The rule judges only the root of a file, as the three layers
    foundation, fixy and crucible do.  The include rules of the plugin judge
    the layers in a root.

THE TARGET OF AN INCLUDE
    A quoted include first resolves in the directory of the file, as the
    compiler does.  Then it resolves in include/.  An angle include resolves
    in include/.  A target that resolves is a file of the source root.  A base
    file has the rank of its root, and each other file is above each layer.

    An include can name a project root as its first directory and not
    resolve in the directory of the file.  Then the include also names that
    root, and the rule takes the higher of the two ranks.  Each other include
    names a library header, and the rule heavy-include and the include rules
    of the plugin judge it.

The rule has no ledger, and each finding is an error.

Complexity: linear in the number of bytes of the base files, and in the number of files that git holds.
"""

import os
from collections.abc import Sequence
from pathlib import Path, PurePosixPath

from assay.core import git, ruletable
from assay.core import report as check_report
from assay.core.paths import REPO_ROOT, listing
from assay.guard.engines.meta import runner as meta_runner
from assay.guard.engines.tsast.runner import TsastContext
from assay.readers import tsast

RULE = "layer-residual"
INCLUDE_KIND = "include"
COMPUTED_KIND = "computed"
UNREAD_KIND = "unread"
PROSE_SUFFIXES = (".md", ".txt")
TABLE = ruletable.TABLE.relative_to(REPO_ROOT)
INCLUDE_DIRECTORY = "include"
# The rank of a project root that the rule table gives no layer.
NO_LAYER = -1


def header_of(body: tsast.MacroBody) -> str:
    """Return the include path that an object-like macro expands to, with its delimiters, or "" for no path.

    The preprocessor reads `#include MACRO` as the tokens of the body.  One string literal is a quoted path.  A `<`
    first and a `>` last is an angle path, and its spelling is the tokens joined.  A comment in the body gives no
    token.
    """
    tokens = tsast.pp_tokens(body.text, body.first_row)
    if len(tokens) == 1 and tokens[0].kind == "string":
        return tsast.lexeme(tokens[0])
    if len(tokens) >= 2 and tsast.lexeme(tokens[0]) == "<" and tsast.lexeme(tokens[-1]) == ">":
        return "".join(tsast.lexeme(token) for token in tokens)
    return ""


class Roots:
    """The project roots of a tree, their ranks and the rule table, for THE TARGET OF AN INCLUDE."""

    __slots__ = ("names", "ranks", "root", "table")

    def __init__(self, root: Path, table: ruletable.RuleTable) -> None:
        """Read the directories in include/ and the ranks of the rule table."""
        self.root = root
        self.table = table
        self.names = frozenset(path.name for path in listing(root / INCLUDE_DIRECTORY) if path.is_dir())
        self.ranks = ruletable.root_ranks(table)

    def rank_of_file(self, rel: str) -> int:
        """Return the rank of a file of the source root: the rank of its root for a base file, else the top rank."""
        kind, _ = self.table.class_of(rel)
        if kind != "base":
            return ruletable.TOP_RANK
        return self.ranks.get(ruletable.root_of_path(rel), NO_LAYER)

    def target(self, rel: str, spelled: str) -> tuple[str, int] | None:
        """Return the target of one include and its rank, or None for a library header.

        Args:
            rel: The file of the include, relative to the root
            spelled: The include path with its delimiters
        """
        body = spelled[1:-1].strip()
        parts = PurePosixPath(body).parts
        named = self.ranks.get(parts[0], NO_LAYER) if len(parts) > 1 and parts[0] in self.names else None
        beside = (self.root / rel).parent / body if spelled.startswith('"') else None
        if beside is not None and beside.is_file():
            # A quoted include that resolves in the directory of the file names the root of its target only.
            named = None
        candidates = [path for path in (beside, self.root / INCLUDE_DIRECTORY / body) if path is not None]
        for candidate in candidates:
            if candidate.is_file():
                target = PurePosixPath(os.path.relpath(candidate.resolve(), self.root.resolve()))
                if target.parts[:1] != ("..",):
                    rank = self.rank_of_file(target.as_posix())
                    if named is not None and named != NO_LAYER and named > rank:
                        return f"the root {parts[0]}", named
                    return target.as_posix(), rank
        if named is not None:
            return f"<{body}>", named
        return None


def include_findings(
    roots: Roots, rel: str, tree: tsast.Tree, bodies: Sequence[tsast.MacroBody]
) -> list[check_report.Finding]:
    """Return an error for each include of a base file that names a higher root, or that the rule cannot resolve.

    Args:
        roots: The project roots of the tree
        rel: The base file, relative to the root
        tree: The parse tree of the file
        bodies: The object-like macros of the file.  The last definition of a name gives its include path
    """
    paths = {body.name: header_of(body) for body in bodies}
    found: list[check_report.Finding] = []
    file_rank = roots.ranks.get(ruletable.root_of_path(rel), NO_LAYER)
    for node in tree.find("preproc_include"):
        path = node.child_by_field("path")
        if path is None:
            continue
        if path.type in ("string_literal", "system_lib_string"):
            spelled = tsast.prose_text(path).strip()
        else:
            spelled = paths.get(tsast.lexeme(path), "") if path.type == "identifier" else ""
            if not spelled.startswith(("<", '"')):
                found.append(
                    check_report.Finding(
                        "error",
                        rel,
                        node.line,
                        RULE,
                        f"the include `{tsast.excerpt(path)}` has a macro as its path, and no object-like macro of "
                        f"this file resolves it.  The rule cannot see the target.  Write the path in the directive",
                        COMPUTED_KIND,
                        tsast.excerpt(path),
                    )
                )
                continue
        target = roots.target(rel, spelled)
        if target is None:
            continue
        name, rank = target
        if rank == NO_LAYER:
            found.append(
                check_report.Finding(
                    "error",
                    rel,
                    node.line,
                    RULE,
                    f"the include of {spelled} names {name}, and no layer row and no quarantine row of {TABLE} gives "
                    f"its project root a rank.  Give the root a row first",
                    INCLUDE_KIND,
                    spelled,
                )
            )
        elif rank > file_rank:
            found.append(
                check_report.Finding(
                    "error",
                    rel,
                    node.line,
                    RULE,
                    f"the include of {spelled} names {name}, which is above the project root of the file in each "
                    f"preprocessor arm.  A base file includes only its own root and the roots below it, in the "
                    f"sequence foundation, fixy, crucible (CLAUDE.md section XXII).  Move the file up, or move the "
                    f"code that it uses down",
                    INCLUDE_KIND,
                    spelled,
                )
            )
    return found


def findings(context: TsastContext) -> list[check_report.Finding]:
    """Give an error for each include of THE RULE, and for each file of a base directory that is not C++ or prose.

    Raises:
        NotApplicable: If the root is not a git work tree
    """
    table_path = context.root / TABLE
    table = ruletable.load(table_path if table_path.is_file() else REPO_ROOT / TABLE)
    roots = Roots(context.root, table)
    trees = list(context.trees(context.scope_files(include_unparseable=True)))
    bodies: dict[str, list[tsast.MacroBody]] = {}
    for body in context.macro_bodies(trees):
        if not body.params and body.define.type == "preproc_def":
            bodies.setdefault(PurePosixPath(body.define.tree.path).as_posix(), []).append(body)
    found: list[check_report.Finding] = []
    for tree in trees:
        rel = PurePosixPath(tree.path).as_posix()
        found += include_findings(roots, rel, tree, bodies.get(rel, []))
    try:
        names = git.tracked_files(context.root, with_new=True)
    except git.GitError as problem:
        raise meta_runner.NotApplicable(f"the tree {context.root} is not a git work tree: {problem}") from None
    plants = f"{meta_runner.PLANTS}/"
    for name in names:
        if (
            name.endswith(tsast.CPP_SUFFIXES)
            or name.endswith(PROSE_SUFFIXES)
            or name.startswith(plants)
            or table.class_of(name)[0] != "base"
        ):
            continue
        found.append(
            check_report.Finding(
                "error",
                name,
                0,
                RULE,
                "the file is in a base directory and is not a C++ file that a compiler reads, and no rule reads it.  "
                "Give it a C++ suffix, or move it out of the base",
                UNREAD_KIND,
            )
        )
    return found
