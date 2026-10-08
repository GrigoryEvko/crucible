"""The inputs of a test of the master guard: the files and the texts that its result reads, for `loop test`.

THE TESTS
    `loop test` (assay.loop.affected) gives this module the words of each
    test that runs the master guard of the source tree, after the master.
    Two kinds of such a test get their inputs:
      * a rule test: `guard check --rule ID...`, `guard witness --rule ID...`
        and `guard witness --engine plugin`
      * a group test: `guard selftest GROUP [--part K/N]`, for a directory
        GROUP of utils/assay/tests/.
    A test of the master with other words, such as `guard selftest MODULE`,
    gets no inputs.  `loop test` then runs it on each call.  A rule test
    that reads a build gets a reason to run on each call.  These rule tests
    read a build:
      * a check of a rule with the scope word builddir
      * a check of an engine, such as `guard check --engine plugin`
      * a rule with assay.guard.preprocessed in its import closure.

THE INPUTS OF A RULE
    Each rule of a rule test gives these inputs:
      * the three masters of utils/bin/
      * the modules of the import closure (THE IMPORT CLOSURE)
      * the ledger utils/ledgers/ID.txt, also when it is not a file
      * each file of the plants utils/plants/ID/
      * the rule table utils/rule-table.txt
      * the files of the scope and of the field inputs of the row (THE SCOPE)
      * the data files of the modules of the closure (MODULE_DATA)
      * the text of the row of the rule, with no line number
      * the text of each row that the rule holds (the field held-by).
    A change of the row of a different rule then changes no input of the rule.

THE INPUTS OF A GROUP
    The inputs of a group test are the masters and the modules of the import
    closure of its test modules and of assay.guard.selftest.  The data files
    of the modules of the closure are inputs too.  When the closure holds
    assay.core.registry, the registry is an input too.

THE TEXTS OF EACH TEST
    Each rule test and each group test also gets two texts.  The first text
    is the HEAD commit, because some rules read the git history.  The second
    text is the list of the files of the tree, because some rules read the
    names of the files.  A commit, a new file and a file that goes then
    change the inputs of each such test.

THE PROGRAMS
    A test that runs a compiler gets each file of include/ too, because a
    compile can read each header.  The option --cxx and the variable
    ASSAY_TEST_CXX give a compiler.  A witness with --build-dir DIR gets the
    compile database of DIR, because the witness of the engine plugin reads
    the flags of the build there.  `loop test` adds the bytes of each program
    that the command or the environment names.

THE IMPORT CLOSURE
    The closure holds the module of the subcommand, the runner of the engine
    of the rule and the module of the rule.  It follows each import of a
    module of the package, also an import in a function.  It also holds the
    file __init__.py of each package of a module.  assay.readers.python reads
    the imports of each module one time.  The modules of DISPATCH import the
    runner of each engine in the function of that engine.  The closure then
    does not follow an import of a module of assay/guard/engines/ from these
    modules, other than the runner of the engine meta.  Each run imports
    that runner.

THE SCOPE
    A path of a scope names each file in it.  The word `tracked` names each
    file of the tree.  The word base names each file that a layer row of the
    rule table holds.  The word quarantined names each file that a
    quarantine row holds.  The word package names each module of the package
    and each master.  The files are the files that git holds and the new
    files that .gitignore does not match.  No scope holds a plant of
    utils/plants/, as in the runners of the engines.  A path in utils/plants/
    of a scope or of the field inputs names its plants.  A scope of a rule of
    the engine tsast holds only its C++ files.

Complexity: one git listing of the tree, and one parse of each module of a closure.  O(f * s) for f files and s
different items of the scopes.
"""

import hashlib
from collections.abc import Collection, Iterable, Sequence
from dataclasses import dataclass, replace
from pathlib import Path, PurePosixPath

from assay.core import git, ruletable
from assay.core import registry as rule_registry
from assay.core import rows as rows_grammar
from assay.readers import compdb

# The masters of utils/bin/, relative to the root of the tree.
MASTERS = ("utils/bin/guard", "utils/bin/loop", "utils/bin/setup")
UTILS = "utils"
PACKAGE = "assay"
TESTS = "utils/assay/tests"
PLANTS = "utils/plants/"
LEDGERS = "utils/ledgers"
RULE_TABLE = "utils/rule-table.txt"
INCLUDE = "include/"
# The modules of the subcommands check, witness and selftest of the master guard.
CHECK = "assay.guard.check"
WITNESS = "assay.guard.witness"
SELFTEST = "assay.guard.selftest"
DISPATCH = frozenset({CHECK, WITNESS})
ENGINES = "assay.guard.engines."
META_RUNNER = "assay.guard.engines.meta.runner"
PLUGIN_WITNESS = "assay.guard.engines.plugin.witness"
PREPROCESSED = "assay.guard.preprocessed"
REGISTRY_MODULE = "assay.core.registry"
# The files of the tree that a module reads, and that no scope of a rule names.
MODULE_DATA = {
    "assay.core.pins": ("utils/pins.txt",),
    "assay.oracle.runner": ("test/session_oracle/",),
    "assay.tests.mutation.test_run": ("utils/plants/gate-mutants/",),
}
# The fixture of the parse trees.  The tree cache holds a parse of the files of the tree.  A test with the fixture
# then reads no input other than the files of its scope.
TREE_FIXTURE = "tsast_trees"
# The marks of a test that runs a compiler (THE PROGRAMS).
COMPILER_OPTION = "--cxx"
COMPILER_VARIABLE = "ASSAY_TEST_CXX"
# The options of the subcommands check and witness, each with the count of its words after it.
OPTIONS = {
    "--rule": 1,
    "--engine": 1,
    "--part": 1,
    "--build-dir": 1,
    "--warnings-dir": 1,
    "--path": 1,
    "--cxx": 1,
    "--plugin": 1,
    "--jobs": 1,
    "--self-test": 0,
}
BUILD_SCOPE = "builddir"
TRACKED_SCOPE = "tracked"
BASE_SCOPE = "base"
QUARANTINED_SCOPE = "quarantined"
PACKAGE_SCOPE = "package"


@dataclass(frozen=True, slots=True)
class Inputs:
    """The inputs of one test: the absolute paths of its files, its texts, and a reason to run on each call.

    `files` and `texts` are sorted.  A path that is not a file is an input too, and a new file changes the inputs.
    `always` is empty for a test that the inputs judge.
    """

    files: tuple[str, ...] = ()
    texts: tuple[str, ...] = ()
    always: str = ""


@dataclass(frozen=True, slots=True)
class Call:
    """The words of one call of the master guard: the subcommand, the rules, the engines and the other options."""

    subcommand: str
    rules: tuple[str, ...]
    engines: tuple[str, ...]
    build_dir: str
    has_root: bool
    runs_compiler: bool


def parsed_call(words: Sequence[str], variables: Collection[str]) -> Call | None:
    """Return the call of the words after the master guard, or None when the words are no rule test (THE TESTS).

    Args:
        words: The subcommand and its words
        variables: The names of the variables of the environment of the test
    """
    if not words or words[0] not in ("check", "witness"):
        return None
    rules: list[str] = []
    engines: list[str] = []
    build_dir = ""
    has_root = False
    index = 1
    while index < len(words):
        word = words[index]
        if word == "--root":
            has_root = True
            index += 2
            continue
        count = OPTIONS.get(word)
        if count is None or index + count >= len(words):
            return None
        value = words[index + 1] if count else ""
        if word == "--rule":
            rules.append(value)
        elif word == "--engine":
            engines.append(value)
        elif word == "--build-dir":
            build_dir = value
        index += 1 + count
    runs_compiler = COMPILER_OPTION in words or COMPILER_VARIABLE in variables
    return Call(words[0], tuple(rules), tuple(engines), build_dir, has_root, runs_compiler)


def module_path(name: str) -> tuple[str, str]:
    """Return the two possible paths of the file of a module of the package, relative to the root of the tree.

    The first path is the file NAME.py, and the second is the file __init__.py of a package NAME.
    """
    relative = "/".join((UTILS, *name.split(".")))
    return f"{relative}.py", f"{relative}/__init__.py"


def absolute_import(name: str, imported: str, is_package: bool) -> str:
    """Return the dotted name of an import of a module, with the points of a relative import resolved."""
    if not imported.startswith("."):
        return imported
    level = len(imported) - len(imported.lstrip("."))
    parts = name.split(".") if is_package else name.split(".")[:-1]
    base = parts[: len(parts) - level + 1] if level > 1 else parts
    rest = imported[level:]
    return ".".join([*base, rest] if rest else base)


class Deriver:
    """The inputs of the tests of the master guard in one tree, read one time for each call of `loop test`."""

    __slots__ = ("_closures", "_files", "_imports", "_registry", "_root", "_scopes", "_table", "_texts", "_tracked")

    def __init__(self, root: Path) -> None:
        """Keep the root of the tree.  Each other fact is read on its first use."""
        self._root = root
        self._registry: rule_registry.Registry | None = None
        self._table: ruletable.RuleTable | None = None
        self._tracked: tuple[str, ...] | None = None
        self._texts: tuple[str, ...] = ()
        self._imports: dict[str, tuple[str, ...]] = {}
        # The file of each module that a closure read, as a tuple of one path, or of no path for a module with no file.
        self._files: dict[str, tuple[str, ...]] = {}
        self._scopes: dict[tuple[str, bool], tuple[str, ...]] = {}
        self._closures: dict[frozenset[str], frozenset[str]] = {}

    @property
    def registry(self) -> rule_registry.Registry:
        """The rows of the registry of the tree that read.  A row that does not read is not in it."""
        if self._registry is None:
            path = self._root / rule_registry.REGISTRY_PATH
            text = path.read_text(encoding="utf-8") if path.is_file() else ""
            self._registry = rule_registry.examine(text)[0]
        return self._registry

    def tracked(self) -> tuple[str, ...]:
        """Return each file of the tree that git holds and each new file that .gitignore does not match, sorted.

        Raises:
            git.GitError: If the root is not a git work tree
        """
        if self._tracked is None:
            self._tracked = git.tracked_files(self._root, with_new=True)
        return self._tracked

    def texts(self) -> tuple[str, ...]:
        """Return the HEAD commit and the digest of the list of the files of the tree (THE TEXTS OF EACH TEST).

        Raises:
            git.GitError: If the root is not a git work tree, or it has no commit
        """
        if not self._texts:
            listing = hashlib.sha256("\n".join(self.tracked()).encode("utf-8", "surrogateescape")).hexdigest()
            self._texts = (f"head {git.head(self._root)}", f"files {listing}")
        return self._texts

    def module_file(self, name: str) -> str | None:
        """Return the file of a module of the package relative to the root, or None when the module has no file."""
        if name not in self._files:
            self._files[name] = tuple(place for place in module_path(name) if (self._root / place).is_file())[:1]
        return self._files[name][0] if self._files[name] else None

    def imports_of(self, name: str, file: str) -> tuple[str, ...]:
        """Return the dotted name of each module of the package that a module imports, also in a function.

        A module that does not parse gives no import.  The run of the module then fails, and its test runs.
        """
        if name not in self._imports:
            from assay.readers import python as python_reader

            is_package = file.endswith("/__init__.py")
            try:
                source = python_reader.read(self._root / file, file)
            except OSError, python_reader.PythonError:
                self._imports[name] = ()
                return ()
            resolved = (absolute_import(name, item, is_package) for item in python_reader.imports(source))
            self._imports[name] = tuple(
                sorted({item for item in resolved if item == PACKAGE or item.startswith(f"{PACKAGE}.")})
            )
        return self._imports[name]

    def closure(self, roots: Iterable[str]) -> frozenset[str]:
        """Return the modules of the import closure of the roots that have a file (THE IMPORT CLOSURE).

        Complexity: one parse of each module of the closure, one time for each call of `loop test`.
        """
        key = frozenset(roots)
        if key in self._closures:
            return self._closures[key]
        seen: set[str] = set()
        stack = sorted(key)
        while stack:
            name = stack.pop()
            if name in seen:
                continue
            file = self.module_file(name)
            if file is None:
                continue
            seen.add(name)
            parts = name.split(".")
            stack += [".".join(parts[:end]) for end in range(1, len(parts))]
            for imported in self.imports_of(name, file):
                if name in DISPATCH and imported.startswith(ENGINES) and imported != META_RUNNER:
                    continue
                stack.append(imported)
        self._closures[key] = frozenset(seen)
        return self._closures[key]

    def class_of(self, path: str) -> str:
        """Return base, quarantined or "" for a file of the tree, from the rule table of the tree."""
        if self._table is None:
            self._table = ruletable.load(self._root / RULE_TABLE)
        return self._table.class_of(path)[0]

    def scope_files(self, item: str, cpp_only: bool) -> tuple[str, ...]:
        """Return the files of the tree that one item of a scope or of the field inputs names (THE SCOPE).

        Complexity: O(f) for f files of the tree, one time for each different item.

        Raises:
            git.GitError: If the root is not a git work tree
        """
        key = (item, cpp_only)
        if key not in self._scopes:
            from assay.guard.engines.meta import package
            from assay.readers import tsast

            is_plant_item = item.startswith(PLANTS)
            found: list[str] = []
            for path in self.tracked():
                if path.startswith(PLANTS) and not is_plant_item:
                    continue
                if cpp_only and not path.endswith(tsast.CPP_SUFFIXES):
                    continue
                if item == TRACKED_SCOPE:
                    holds = True
                elif item in (BASE_SCOPE, QUARANTINED_SCOPE):
                    holds = self.class_of(path) == item
                elif item == PACKAGE_SCOPE:
                    holds = package.is_in_scope(path)
                else:
                    holds = ruletable.holds(item, path)
                if holds:
                    found.append(path)
            self._scopes[key] = tuple(found)
        return self._scopes[key]

    def data_files(self, modules: Iterable[str]) -> list[str]:
        """Return the files that MODULE_DATA gives for the modules, each item expanded to its files."""
        found: list[str] = []
        for name in modules:
            for item in MODULE_DATA.get(name, ()):
                found += self.scope_files(item, False) if item.endswith("/") else [item]
        return found

    def rule_modules(self, rule: rule_registry.Rule, subcommand: str) -> list[str]:
        """Return the roots of the closure of a rule: the subcommand, the runners and the module of the rule."""
        from assay.guard.engines.meta import runner as meta_runner

        roots = [CHECK if subcommand == "check" else WITNESS, META_RUNNER]
        roots.append(f"{ENGINES}{rule.engine.directory}.runner")
        if rule.engine == rule_registry.Engine.PLUGIN and subcommand == "witness":
            roots.append(PLUGIN_WITNESS)
        roots.append(rule.legacy.qualified if rule.legacy is not None else meta_runner.module_name_of(rule, self._root))
        return roots

    def rule_inputs(self, rule_id: str, subcommand: str) -> Inputs:
        """Return the inputs of one rule for the subcommand check or witness (THE INPUTS OF A RULE).

        Raises:
            git.GitError: If the root is not a git work tree
        """
        rule = self.registry.rules.get(rule_id)
        if rule is None:
            return Inputs(always=f"the registry has no readable row of the rule {rule_id}")
        if subcommand == "check" and BUILD_SCOPE in rule.scope:
            return Inputs(always=f"the rule {rule_id} reads the build directory (the scope word {BUILD_SCOPE})")
        modules = self.closure(self.rule_modules(rule, subcommand))
        if PREPROCESSED in modules:
            return Inputs(always=f"the rule {rule_id} reads the preprocessed store of the build ({PREPROCESSED})")
        cpp_only = rule.engine == rule_registry.Engine.TSAST
        files = [*MASTERS, RULE_TABLE, f"{LEDGERS}/{rule_id}.txt"]
        files += [file for name in modules if (file := self.module_file(name)) is not None]
        files += self.scope_files(f"{PLANTS}{rule_id}/", False)
        for item in rule.scope:
            if item != BUILD_SCOPE:
                files += self.scope_files(item, cpp_only)
        for item in rule.inputs:
            files += self.scope_files(item, False) if item.endswith("/") else [item]
        files += self.data_files(modules)
        held = [other.id for other in self.registry.rules.values() if other.held_by == rule_id]
        return Inputs(tuple(sorted({str(self._root / file) for file in files})), self.row_texts([rule_id, *held]))

    def row_texts(self, rule_ids: Iterable[str]) -> tuple[str, ...]:
        """Return the text of the row of each rule with no line number, or a text that tells that the rule has no row."""
        texts: list[str] = []
        for rule_id in rule_ids:
            rule = self.registry.rules.get(rule_id)
            texts.append(repr(replace(rule, line=0)) if rule is not None else f"no row of the rule {rule_id}")
        return tuple(sorted(texts))

    def call_inputs(self, call: Call) -> Inputs:
        """Return the inputs of a rule test: the union of the inputs of its rules (THE TESTS).

        Raises:
            git.GitError: If the root is not a git work tree
        """
        if call.has_root:
            return Inputs(always="the call reads the tree of the option --root")
        if call.subcommand == "check" and call.engines:
            return Inputs(always=f"a check of the engine {call.engines[0]} reads the objects of the build")
        rule_ids = list(call.rules)
        for engine in call.engines:
            rule_ids += [
                rule.id
                for rule in self.registry.rules.values()
                if rule.engine.value == engine and rule.held_by is None and rule.id not in rule_ids
            ]
        if not rule_ids:
            return Inputs(always="the call names no rule")
        files: set[str] = set()
        texts: set[str] = set(self.texts())
        for rule_id in rule_ids:
            found = self.rule_inputs(rule_id, call.subcommand)
            if found.always:
                return found
            files.update(found.files)
            texts.update(found.texts)
        if call.runs_compiler:
            files.update(str(self._root / file) for file in self.scope_files(INCLUDE, False))
        if call.subcommand == "witness" and call.build_dir:
            files.add(str(compdb.database_file(Path(call.build_dir))))
        return Inputs(tuple(sorted(files)), tuple(sorted(texts)))

    def group_inputs(self, group: str, runs_compiler: bool) -> Inputs:
        """Return the inputs of a group test (THE INPUTS OF A GROUP).

        Raises:
            git.GitError: If the root is not a git work tree
        """
        tests = f"{TESTS}/{group}/"
        names = [
            ".".join(PurePosixPath(path).with_suffix("").parts[1:])
            for path in self.scope_files(tests, False)
            if PurePosixPath(path).name.startswith("test_") and path.endswith(".py")
        ]
        modules = self.closure([SELFTEST, *names])
        files = [*MASTERS, *(file for name in modules if (file := self.module_file(name)) is not None)]
        files += self.data_files(modules)
        if REGISTRY_MODULE in modules:
            files.append(rule_registry.REGISTRY_PATH)
        if runs_compiler:
            files += self.scope_files(INCLUDE, False)
        return Inputs(tuple(sorted({str(self._root / file) for file in files})), self.texts())

    def test_inputs(self, words: Sequence[str], variables: Collection[str]) -> Inputs | None:
        """Return the inputs of a test of the master guard, or None when the words are no rule test and no group test.

        Args:
            words: The words after the master: the subcommand and its words
            variables: The names of the variables of the environment of the test

        Returns:
            The inputs, an Inputs with a reason to run on each call, or None (THE TESTS)
        """
        try:
            if len(words) in (2, 4) and words[0] == "selftest":
                group = words[1]
                is_group = group.isidentifier() and (self._root / TESTS / group).is_dir()
                is_part = len(words) == 2 or words[2] == "--part"
                if not (is_group and is_part):
                    return None
                return self.group_inputs(group, COMPILER_VARIABLE in variables)
            call = parsed_call(list(words), variables)
            return None if call is None else self.call_inputs(call)
        except git.GitError as problem:
            return Inputs(always=f"the source tree is not a git work tree with a commit: {problem}")
        except (OSError, ruletable.TableError, rows_grammar.RowsError) as problem:
            return Inputs(always=f"the registry or the rule table cannot be read: {problem}")
