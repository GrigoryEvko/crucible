"""The runner of the engine tsast: the trees that the rules of one call read, and the copy of a plant.

THE CONTEXT
    A tsast rule reads the parse trees of the C++ files of a tree.  Its
    module is assay/guard/engines/tsast/ID.py, with underscores in place of
    the hyphens, and its function findings(context) gets a TsastContext.  The
    context gives the root of the tree, the build directory when the call
    gives one, the C++ files of a scope, the tree of each file and the macro
    bodies of each tree.  The run and the witness of
    assay.guard.engines.meta.runner judge the findings.

THE SHARED TREES
    When a call of `guard check` runs more than one rule, the rules share one
    context.  The context then keeps each tree and each list of macro bodies
    that a rule reads, so the call loads each tree one time from the tree
    cache of assay.readers.tsast.  A rule reads a tree and changes nothing in
    it.  A call with one rule keeps no tree: each tree goes after the rule
    reads it, so the memory of the call is the memory of the facts of the
    rule.

THE SCOPE
    files() gives the C++ files of a scope, sorted.  The files are the files
    that git tracks and the new files that .gitignore does not match.  A path
    of a scope is a directory that ends with "/" or one file.  The word
    tracked holds each file, and the word base holds each file that a layer
    row of the rule table holds.  The plants of utils/plants/ are data of
    their rules, so no scope holds them.  A file that tsast.UNPARSEABLE lists
    is in a scope only when the rule gives include_unparseable.

THE PARSE ERRORS
    The tree of a file that does not parse has a diagnostic.  The rule
    parse-clean refuses such a file.  trees() gives no such tree to the other
    rules.  findings_of() gives a rule one error of the kind parse for each
    such file that it reads, because the rule cannot read the file.

THE PARTS
    A call with --part K/N reads part K of N of the files of a rule, and only
    a rule that calls part_of() reads a part.  part_of() orders the files by
    their size, the largest first, and the part holds each N-th file from
    the position K.  The run judges only the ledger rows of the files of the
    part.  The parts of one rule run at the same time.  Each part writes
    its warnings to the file RULE.partK.txt of the warnings directory
    (assay.core.report, THE WARNINGS DIRECTORY).  A part then does not
    replace or remove the warnings of a different part.

THE PLANTS
    plant_context() copies a plant to a throwaway repository
    (assay.guard.engines.meta.runner.copy_plant), so a file of a plant with
    the suffix .planted gets its name with no suffix in the copy.  The C++
    files of a plant have the suffix, so no rule of the tree reads them.

Complexity: one load of each tree that a rule of the call reads, and one parse of each macro body that the tree
cache does not hold.
"""

from collections.abc import Iterator, Sequence
from pathlib import Path, PurePosixPath

from assay.core import git
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import ruletable
from assay.core.paths import REPO_ROOT
from assay.guard.engines.meta import runner as meta_runner
from assay.readers import tsast

# The finding kind of a file that a rule cannot read (THE PARSE ERRORS).
PARSE_KIND = "parse"
# The rule table of a tree, relative to its root.  A copy of a plant has no table, so it reads the table of this tree.
TABLE = ruletable.TABLE.relative_to(REPO_ROOT)
SCOPE_TRACKED = "tracked"
SCOPE_BASE = "base"
# The scope words of the engine (assay.core.registry.SCOPE_WORDS): the other words name no C++ of a tree.
SCOPE_WORDS = (SCOPE_TRACKED, SCOPE_BASE)


def has_directory(path: str, names: frozenset[str]) -> bool:
    """Return True when the name of a directory of the path of a file is one of the names."""
    return any(part in names for part in PurePosixPath(path).parts[:-1])


class TsastContext:
    """The tree that the tsast rules of one call read: its root, its build directory, its C++ files and their trees.

    The context reads the list of files and the rule table one time, on the
    first use.  With keep, it also keeps each tree and each list of macro
    bodies (THE SHARED TREES).
    """

    __slots__ = ("root", "build_dir", "part", "rule", "_keep", "_listed", "_table", "_trees", "_bodies", "_broken",
                 "_covered")

    def __init__(self, root: Path, build_dir: Path | None = None, part: tuple[int, int] | None = None,
                 keep: bool = False) -> None:
        """Keep the root, the build directory, the part of the call and the choice to keep the trees.

        Args:
            root: The root of the tree
            build_dir: The build directory, or None
            part: (K, N) for the part K of N (THE PARTS), or None for each file
            keep: Whether the context keeps each tree that a rule reads, for the other rules of the call
        """
        self.root = root
        self.build_dir = build_dir
        self.part = part
        # The rule that findings_of() runs at this time.
        self.rule: rule_registry.Rule | None = None
        self._keep = keep
        self._listed: tuple[str, ...] | None = None
        self._table: ruletable.RuleTable | None = None
        self._trees: dict[str, tsast.Tree] = {}
        self._bodies: dict[str, list[tsast.MacroBody]] = {}
        self._broken: dict[str, tuple[int, str]] = {}
        self._covered: frozenset[str] | None = None

    def listed(self) -> tuple[str, ...]:
        """Return each C++ file of the tree that a scope can hold, sorted (THE SCOPE).

        Raises:
            NotApplicable: If the root is not a git work tree
        """
        if self._listed is None:
            try:
                names = git.tracked_files(self.root, with_new=True)
            except git.GitError as problem:
                raise meta_runner.NotApplicable(f"the tree {self.root} is not a git work tree: {problem}") from None
            plants = f"{meta_runner.PLANTS}/"
            self._listed = tuple(name for name in names
                                 if name.endswith(tsast.CPP_SUFFIXES) and not name.startswith(plants))
        return self._listed

    def is_base(self, path: str) -> bool:
        """Return whether a layer row of the rule table holds a file (the scope word base)."""
        if self._table is None:
            table = self.root / TABLE
            self._table = ruletable.load(table if table.is_file() else REPO_ROOT / TABLE)
        return self._table.class_of(path)[0] == SCOPE_BASE

    def files(self, scope: Sequence[str], *, include_unparseable: bool = False) -> list[str]:
        """Return the C++ files that the paths and the words of a scope hold, sorted (THE SCOPE).

        Complexity: O(f * s) for f files and s items of the scope.

        Args:
            scope: Directories that end with "/", files, and the words of SCOPE_WORDS
            include_unparseable: Whether to keep the files that tsast.UNPARSEABLE lists

        Raises:
            ValueError: If an item of the scope is a word that names no C++ of a tree
            NotApplicable: If the root is not a git work tree
        """
        words = [item for item in scope if item in rule_registry.SCOPE_WORDS]
        unknown = [word for word in words if word not in SCOPE_WORDS]
        if unknown:
            raise ValueError(f"the scope word {unknown[0]} names no C++ file of a tree.  A tsast rule has the scope "
                             f"words {' and '.join(SCOPE_WORDS)}, and paths")
        paths = [item for item in scope if item not in rule_registry.SCOPE_WORDS]
        found = []
        for name in self.listed():
            if name in tsast.UNPARSEABLE and not include_unparseable:
                continue
            if (SCOPE_TRACKED in words or (SCOPE_BASE in words and self.is_base(name))
                    or any(ruletable.holds(path, name) for path in paths)):
                found.append(name)
        return found

    def scope_files(self, *, include_unparseable: bool = False) -> list[str]:
        """Return the C++ files of the scope of the rule that findings_of() runs (files()).

        Raises:
            ValueError: If no rule runs, or its scope has a word that names no C++ of a tree
        """
        if self.rule is None:
            raise ValueError("scope_files() reads the scope of the rule of findings_of(), and no rule runs")
        return self.files(self.rule.scope, include_unparseable=include_unparseable)

    def part_of(self, paths: Sequence[str]) -> list[str]:
        """Return the files of the part of the call, or each file with no part (THE PARTS).

        Complexity: O(f log f) for f files.
        """
        if self.part is None:
            return list(paths)
        index, count = self.part
        ordered = sorted(paths, key=lambda path: (-(self.root / path).stat().st_size, path))
        chosen = ordered[index - 1::count]
        self._covered = frozenset(chosen)
        return sorted(chosen)

    def is_covered(self, key: rule_ledger.Key) -> bool:
        """Return whether the run judges a ledger row: each row with no part, and a row of a file of the part."""
        return self._covered is None or key.path in self._covered

    def trees(self, paths: Sequence[str], *, with_broken: bool = False) -> Iterator[tsast.Tree]:
        """Yield the tree of each file, in the order of the paths.

        Each tree has the path of its file relative to the root.  A file that
        does not parse gives no tree, unless with_broken: findings_of() gives
        the rule an error for it (THE PARSE ERRORS).

        Args:
            paths: The files, relative to the root
            with_broken: Whether to give the tree of a file that does not parse

        Raises:
            KitMissing: If the pinned kit is not installed
        """
        if self._keep:
            missing = [path for path in dict.fromkeys(paths) if path not in self._trees]
            for tree in tsast.parse_files(self.root, missing):
                self._trees[PurePosixPath(tree.path).as_posix()] = tree
            source: Iterator[tsast.Tree] = (self._trees[path] for path in paths)
        else:
            source = tsast.parse_files(self.root, list(paths))
        for tree in source:
            error = tsast.first_error(tree)
            if error is not None and not with_broken:
                self._broken[PurePosixPath(tree.path).as_posix()] = error
                continue
            yield tree

    def macro_bodies(self, trees: Sequence[tsast.Tree]) -> list[tsast.MacroBody]:
        """Return the macro bodies of the trees, in the order of the trees and of their definitions.

        Complexity: one parse of each body that the tree cache does not hold.
        """
        if not self._keep:
            return tsast.macro_bodies(trees)
        missing = [tree for tree in trees if PurePosixPath(tree.path).as_posix() not in self._bodies]
        for tree in missing:
            self._bodies[PurePosixPath(tree.path).as_posix()] = []
        for body in tsast.macro_bodies(missing):
            self._bodies[PurePosixPath(body.define.tree.path).as_posix()].append(body)
        return [body for tree in trees for body in self._bodies[PurePosixPath(tree.path).as_posix()]]

    def findings_of(self, rule: rule_registry.Rule) -> list[check_report.Finding]:
        """Return the findings of the module of a rule, and one error for each file that it cannot read.

        Raises:
            NotApplicable: If the rule does not apply to the tree
        """
        self._broken = {}
        self._covered = None
        self.rule = rule
        try:
            found = list(meta_runner.rule_module(rule).findings(self))
        except tsast.KitMissing as problem:
            raise meta_runner.NotApplicable(f"the parse kit is not installed: {problem}") from None
        reported = {item.path for item in found if item.kind == PARSE_KIND}
        for path, (line, node) in sorted(self._broken.items()):
            if path not in reported:
                found.append(check_report.Finding(
                    "error", path, line, rule.id, f"the parse kit cannot read the file at {node}, so the rule reads "
                    f"none of it.  The rule parse-clean gives the construct", PARSE_KIND))
        return found


def plant_context(plant: Path, scratch: Path) -> TsastContext:
    """Copy a plant to a throwaway repository (THE PLANTS), and give its context."""
    meta_runner.copy_plant(plant, scratch)
    return TsastContext(scratch)


def run(rules: Sequence[rule_registry.Rule], root: Path, build_dir: Path | None, warnings_dir: Path | None,
        part: tuple[int, int] | None = None) -> list[int]:
    """Run the rules on one context, and give the status of each one (assay.guard.engines.meta.runner.run()).

    The context keeps the trees when the call runs more than one rule (THE SHARED TREES).
    """
    context = TsastContext(root, build_dir, part, keep=len(rules) > 1)
    kept = context.is_covered if part is not None else None
    key = f"part{part[0]}" if part is not None else None
    return [meta_runner.run(rule, context, warnings_dir, kept, key) for rule in rules]


def witness(rule: rule_registry.Rule, root: Path = REPO_ROOT) -> int:
    """Run a rule on each of its plants and controls (assay.guard.engines.meta.runner.witness())."""
    return meta_runner.witness(rule, root, plant_context)
