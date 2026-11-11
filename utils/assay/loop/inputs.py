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
    gets no inputs.  The test launcher then traces it, and `loop test` reads
    its read set (assay.loop.affected, THE SCRIPT TEST).  A rule test
    that reads a build gets its inputs in the source tree, and the reason
    that it reads the build (THE BUILD).

THE INPUTS OF A RULE
    Each rule of a rule test gives these inputs:
      * the three masters of utils/bin/
      * the modules of the import closure (THE IMPORT CLOSURE)
      * each file that the search of the module of the rule reads, and the
        names of the modules that it compares (THE SEARCH)
      * the ledger utils/ledgers/ID.txt, also when it is not a file
      * each file of the plants utils/plants/ID/
      * the rule table utils/rule-table.txt
      * the files of the scope and of the field inputs of the row (THE SCOPE)
      * the data files of the modules of the closure (MODULE_DATA)
      * the text of the row of the rule, with no line number
      * the text of each row that the rule holds (the field held-by)
      * the texts and the files of git (THE TEXTS OF GIT).
    A change of the row of a different rule then changes no input of the rule.

THE BUILD
    A check of a rule with the scope word builddir reads the build directory
    of its configuration.  A check of an engine, such as `guard check
    --engine plugin`, reads the objects of the build.  The inputs of such a
    test hold its files and its texts in the source tree.  The field build
    tells why the test reads the build.  The files of a build are not
    files of the source tree.  `loop test` then runs such a test on each
    call.

THE SEARCH
    A rule with no module of its own is a member of a family module.
    module_search() of assay.guard.engines.meta.runner finds that module.  It
    parses the modules of the engine directory in the sequence of the names,
    and it stops at the module that names the rule.  The runner of each rule
    runs the search, also for a rule with the field legacy or held-by.  Each
    file that the search reads is an input, and the module that it finds is a
    root of the closure.  The names that the search compares are a text.  A
    new module that sorts before the match then changes the inputs.  A module
    after the match is no input.

THE INPUTS OF A GROUP
    The inputs of a group test are the masters and the modules of the import
    closure of its test modules and of assay.guard.selftest.  The data files
    of the modules of the closure are inputs too.  When the closure holds
    assay.core.registry, the registry is an input too.  The texts and the
    files of git apply as for a rule.

THE TEXTS OF GIT
    Two texts of a test come from the functions of assay.core.git that the
    test can call (THE CALLS).  A function of assay.core.git.READS_HISTORY
    reads the history.  A test that can call one gets the HEAD commit as a
    text.  A function of assay.core.git.READS_NAMES lists the files of the
    tree.  A test that can call one gets the list of the files as a text.  It
    also gets each file .gitignore and .gitattributes of the tree as inputs,
    because git reads them.  A commit then runs again only the tests that
    read the history.  A new file runs again only the tests that list the
    files.

THE CALLS
    The calls of a test are the top-level functions and classes of the
    modules of its closure that its code can call.  The walk starts at the
    code that runs at the import of each module of the closure.  It also
    starts at each function and class of the root modules of the test.  The
    roots of a rule test are the subcommand, the runners and the module of
    the rule.  The roots of a group test are its test modules.  From a
    function or a class, the walk follows each name of the same module.  It
    also follows each reference to a different module of the closure (shape()
    of assay.readers.python).  A class is one unit, and each of its methods
    is a call of the test with the class.  A name that a module imports from
    a second module names the function of the second module.  A module that
    the code uses as a value makes each of its functions and classes a call.
    An attribute of an object, such as context.tracked, gives nothing.  The
    class of the object is a call when a code makes such an object.

THE PROGRAMS
    A test that runs a compiler gets each file of include/ too, because a
    compile can read each header.  The option --cxx and the variable
    ASSAY_TEST_CXX give a compiler.  A witness with --build-dir DIR gets the
    compile database of DIR, because the witness of the engine plugin reads
    the flags of the build there.  `loop test` adds the bytes of each program
    that the command or the environment names.

THE IMPORT CLOSURE
    The closure holds the module of the subcommand, the runner of the engine
    of the rule and the module of the rule.  A measured rule with a holder
    has no module of its own, and the closure holds the module of the judges
    of the holders (HELD_WITNESS).  It follows each import of a
    module of the package, also an import in a function.  It also holds the
    file __init__.py of each package of a module.  assay.readers.python reads
    the imports of each module one time (THE PARSE CACHE).  The modules of DISPATCH import the
    runner of each engine in the function of that engine.  The closure then
    does not follow an import of a module of assay/guard/engines/ from these
    modules, other than the runner of the engine meta.  Each run imports
    that runner.

THE PARSE CACHE
    The parse of a module gives its imports and its shape.  The cache
    SHAPES_CACHE of assay.core.cache keeps that result.  A call of `loop
    test` then parses only a module with a new text.  The name of an entry
    is the SHA-256 of these inputs:
      - SHAPES_FORMAT and the version of Python
      - The bytes of this module and of assay.readers.python
      - The dotted name, the file and the text of the module.
    A change of an input then gives a different entry, and an entry does
    not hold an earlier result.  On 2026-10-17, the parse of the 227 modules of
    the closures took 1.79 s, and the read of their entries took 0.03 s.

THE FACTS OF A CALL
    A Deriver reads the registry, the rule table, the list of the files and
    the HEAD commit one time, and its inputs come from them.  is_current()
    tells if each fact that it read is the same at this time.  `loop test`
    keeps a current Deriver after the tests (THE DIGEST MEMO of
    assay.loop.affected).  A changed module of a closure changes no fact,
    because each module is a file of the inputs, and its hash changes.

THE SCOPE
    A path of a scope that ends with "/" names each file in it.  A path with
    no "/" at its end names one file, whatever its suffix, because the rule
    reads that file by its name.  The word `tracked` names each file of the
    tree.  The word base names each file that a layer row of the rule table
    holds.  The word quarantined names each file that a quarantine row holds.
    The word package names each module of the package and each master.  The
    files are the files that git holds and the new files that .gitignore does
    not match.  The runner of the engine of the rule gives the files of these
    that a scope can hold.  The runner of the engine tsast gives a rule only
    the C++ files that are not plants (is_candidate() of
    assay.guard.engines.tsast.runner).  The runner of the engine plugin reads
    the findings that the compile of a unit gives.  A scope of a rule of that
    engine then names only the C and C++ files (COMPILED_SUFFIXES).  The
    context of each other engine gives each file, also a plant.

THE METERS OF A TEST
    Two step meters of the master loop run in front of each test of a build:
    the test launcher (assay.loop.teststep) and the fixture driver
    (assay.loop.fixture.store).  meter_inputs() gives the inputs of one
    meter.  These are the master loop and the files of the import closure of
    its module.  The ledger and the row of each rule that it judges are
    inputs too.

THE TOUCH
    The gate of `loop land` must know the tests of a build that a change can
    change (assay.loop.gate, THE SELECTION).  read_change() reads the change
    of a tree against a commit.  Inputs.is_touched() tells if the change
    touches the inputs of a test.  It does when one of these facts holds:
      * a changed path is a file of the inputs
      * a path that the change deletes is in a directory of the inputs.
        Each directory of the scope and of the field inputs is such a
        directory.  The plants of the rule, the modules of its engine and a
        data directory are such directories too.  The directory of a group,
        and include/ for a test that compiles, are such directories too.  A
        word of a scope names
        files anywhere in the tree, and the word package names the files of
        utils/.  A scope with the predicate of an engine gives no directory.
        That engine reads only the C and C++ files of the scope, and a
        change of such a file changes each build
      * the change adds, removes or changes the row of a rule, and that row
        is a text of the test
      * the inputs hold a reason to run on each call.
    The texts of git are no part of the touch.  The HEAD commit changes with
    each change, and a rule reads the same history in each build.  A new
    file of a scope is a file of the inputs, and a deleted one is in a
    directory of the inputs.

Complexity: one git listing of the tree, and one parse of each module of a closure.  O(f * s) for f files and s
different items of the scopes.
"""

import functools
import hashlib
import json
import sys
from collections.abc import Callable, Collection, Iterable, Sequence
from dataclasses import dataclass, replace
from pathlib import Path, PurePosixPath

from assay.core import cache, git, ruletable
from assay.core import registry as rule_registry
from assay.core import rows as rows_grammar
from assay.readers import compdb
from assay.readers import python as python_reader

# The masters of utils/bin/, relative to the root of the tree, and the master loop of the step meters (THE METERS OF A
# TEST).
MASTERS = ("utils/bin/guard", "utils/bin/loop", "utils/bin/setup")
LOOP_MASTER = "utils/bin/loop"
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
# The directory of the modules of the engines, relative to the root of the tree (THE TOUCH).
ENGINE_DIRECTORY = "utils/assay/guard/engines"
META_RUNNER = "assay.guard.engines.meta.runner"
PLUGIN_WITNESS = "assay.guard.engines.plugin.witness"
# The module of the judge of the holder of a measured rule with a holder, which its witness runs.
HELD_WITNESS = "assay.guard.engines.buildsys.held"
REGISTRY_MODULE = "assay.core.registry"
# The files of the tree that a module reads, and that no scope of a rule names.
MODULE_DATA = {
    "assay.core.pins": ("utils/pins.txt",),
    "assay.oracle.runner": ("test/session_oracle/",),
    "assay.loop.evidence.items": ("utils/assay/loop/evidence/admitted-gates.txt",),
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
# The suffixes of a C or C++ file.  A compile reads such a file, and a change of one can change each build (THE SCOPE,
# THE TOUCH).
COMPILED_SUFFIXES = (".h", ".hh", ".hpp", ".hxx", ".c", ".cc", ".cpp", ".cxx", ".inc", ".ipp", ".tpp")
# The names of the files of the tree that git reads when it lists the files (THE TEXTS OF GIT).
GIT_FILES = (".gitattributes", ".gitignore")
# The words that start the texts of git and the text of the search (THE TEXTS OF GIT, THE SEARCH).
HEAD_TEXT = "head"
FILES_TEXT = "files"
SEARCH_TEXT = "modules"
# The cache of the imports and the shapes of the modules, and the format of its entries (THE PARSE CACHE).
SHAPES_CACHE = "python-shapes"
SHAPES_FORMAT = b"python-shapes-1"


@dataclass(frozen=True, slots=True)
class Change:
    """The change of a tree against a commit, as the touch reads it (THE TOUCH).

    `paths` holds the absolute path of each changed file, also of a file that the change deletes.  `deleted` holds the
    paths of `paths` that the tree does not hold.  `rows` holds each rule with a row of the registry that the change
    adds, removes or changes.
    """

    paths: frozenset[str]
    deleted: frozenset[str]
    rows: frozenset[str]


@dataclass(frozen=True, slots=True)
class Inputs:
    """The inputs of one test: the absolute paths of its files, its texts, and a reason to run on each call.

    `files` and `texts` are sorted.  A path that is not a file is an input too, and a new file changes the inputs.
    `always` is empty for a test that the inputs judge.  `build` tells why the test reads the build directory of its
    configuration, and it is empty for a test that reads no build (THE BUILD).  `directories` holds the absolute path of
    each directory with files that the test reads, and `rules` holds each rule with a row that is a text (THE TOUCH).
    """

    files: tuple[str, ...] = ()
    texts: tuple[str, ...] = ()
    always: str = ""
    build: str = ""
    directories: tuple[str, ...] = ()
    rules: tuple[str, ...] = ()

    def is_touched(self, change: Change) -> bool:
        """Tell if a change touches the inputs (THE TOUCH).

        Complexity: linear in the files, and O(d * p) for d directories and p deleted paths.
        """
        if self.always or not change.paths.isdisjoint(self.files) or not change.rows.isdisjoint(self.rules):
            return True
        return any(path.startswith(f"{directory}/") for path in change.deleted for directory in self.directories)


def row_text(rule: rule_registry.Rule | None, rule_id: str) -> str:
    """Return the text of the row of a rule with no line number, or a text that tells that the rule has no row."""
    return repr(replace(rule, line=0)) if rule is not None else f"no row of the rule {rule_id}"


def is_compiled_file(path: str) -> bool:
    """Tell if a path is a C or C++ file, which a compile reads (COMPILED_SUFFIXES)."""
    return path.endswith(COMPILED_SUFFIXES)


def directory_of(item: str) -> str | None:
    """Return the directory, relative to the root, of the files that one item of a scope names, or None for one file.

    A word names files anywhere in the tree, and its directory is the root, "".  The word package names the files of
    utils/ (THE TOUCH).
    """
    if item == PACKAGE_SCOPE:
        return UTILS
    if item in rule_registry.SCOPE_WORDS:
        return ""
    return item.rstrip("/") if item.endswith("/") else None


def read_change(root: Path, base: str, changed: Iterable[str]) -> Change:
    """Return the change of some paths of a tree against the commit `base` (THE TOUCH).

    Args:
        root: The tree, which holds the change
        base: The commit that the change starts from
        changed: The paths that the change touches, relative to the root

    Raises:
        git.GitError: If git cannot read the registry at the base

    Complexity: linear in the paths.  A change of the registry adds one read of the registry at the base and in the
    tree, and the rows of the two.
    """
    relative = sorted(set(changed))
    paths = frozenset(str(root / path) for path in relative)
    deleted = frozenset(str(root / path) for path in relative if not (root / path).is_file())
    rows: frozenset[str] = frozenset()
    if rule_registry.REGISTRY_PATH in relative:
        earlier_text = git.show(root, base, rule_registry.REGISTRY_PATH)
        earlier = rule_registry.examine(earlier_text.decode("utf-8"))[0].rules if earlier_text is not None else {}
        current_path = root / rule_registry.REGISTRY_PATH
        current_text = current_path.read_text(encoding="utf-8") if current_path.is_file() else ""
        current = rule_registry.examine(current_text)[0].rules
        rows = frozenset(
            rule_id
            for rule_id in earlier.keys() | current.keys()
            if row_text(earlier.get(rule_id), rule_id) != row_text(current.get(rule_id), rule_id)
        )
    return Change(paths, deleted, rows)


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


def parse_module(name: str, file: str, text: str) -> tuple[tuple[str, ...], python_reader.Shape | None]:
    """Return the modules of the package that a module imports, also in a function, and the shape of the module.

    A text that does not parse gives no import and no shape.  The run of the module then fails, and its test runs.
    Complexity: one parse and one walk of the text.
    """
    try:
        source = python_reader.parse(text, file)
    except python_reader.PythonError:
        return (), None
    is_package = file.endswith("/__init__.py")
    resolved = (absolute_import(name, item, is_package) for item in python_reader.imports(source))
    imports = tuple(sorted({item for item in resolved if item == PACKAGE or item.startswith(f"{PACKAGE}.")}))
    return imports, python_reader.shape(source)


# The imports and the shape of each module of the package, by its dotted name, its file and its text.
type ParseCache = dict[tuple[str, str, str], tuple[tuple[str, ...], python_reader.Shape | None]]
# The imports and the shape of one module, as parse_module() gives them.
type Parsed = tuple[tuple[str, ...], python_reader.Shape | None]


@functools.cache
def shapes_identity() -> bytes:
    """Return the digest of the code that makes an entry of THE PARSE CACHE, one time for each process.

    The digest holds SHAPES_FORMAT, the version of Python and the bytes of this module and of assay.readers.python.
    """
    digest = hashlib.sha256(SHAPES_FORMAT)
    digest.update(sys.version.encode())
    for module_file in (__file__, python_reader.__file__):
        digest.update(Path(module_file).read_bytes())
    return digest.digest()


def shape_record(parsed: Parsed) -> bytes:
    """Return the bytes of an entry of THE PARSE CACHE for the imports and the shape of one module."""
    imports, shape = parsed
    document: dict[str, object] = {"imports": list(imports), "shape": None}
    if shape is not None:
        document["shape"] = {
            "definitions": [
                [item.name, list(item.references), list(item.names)] for item in shape.definitions.values()
            ],
            "module": [list(shape.module.references), list(shape.module.names)],
            "targets": sorted(shape.imports.targets.items()),
        }
    return json.dumps(document, separators=(",", ":")).encode("utf-8")


def parsed_of(data: bytes) -> Parsed | None:
    """Return the imports and the shape of an entry of THE PARSE CACHE, or None for an entry with a different shape."""
    try:
        document = json.loads(data)
        imports = tuple(str(item) for item in document["imports"])
        stored = document["shape"]
        if stored is None:
            return imports, None
        definitions = {
            str(name): python_reader.Definition(str(name), tuple(map(str, references)), tuple(map(str, names)))
            for name, references, names in stored["definitions"]
        }
        references, names = stored["module"]
        module = python_reader.Definition(
            python_reader.MODULE_SCOPE, tuple(map(str, references)), tuple(map(str, names))
        )
        targets = python_reader.Bindings({str(name): str(target) for name, target in stored["targets"]})
    except ValueError, KeyError, TypeError:
        return None
    return imports, python_reader.Shape(definitions, module, targets)


def stored_parse(directory: Path | None, name: str, file: str, text: str) -> Parsed:
    """Return the imports and the shape of a module from its entry of THE PARSE CACHE, or parse it and write the entry.

    `directory` is the cache, or None when the caches are off.  Complexity: one read of one entry, or one parse.
    """
    if directory is None:
        return parse_module(name, file, text)
    digest = hashlib.sha256(shapes_identity())
    digest.update(f"{name}\0{file}\0{text}".encode("utf-8", "surrogateescape"))
    entry_name = digest.hexdigest()
    entry = directory / entry_name[:2] / entry_name
    try:
        used = entry.stat().st_mtime
        found = parsed_of(entry.read_bytes())
    except OSError:
        found = None
    if found is not None:
        cache.mark_used(entry, used)
        return found
    parsed = parse_module(name, file, text)
    try:
        cache.write_atomic(entry, shape_record(parsed))
    except OSError:
        pass
    return parsed


class Deriver:
    """The inputs of the tests of the master guard in one tree, read one time for each call of `loop test`.

    The parse cache can come from the caller.  A second Deriver of the same caller then parses only a module with a
    new text.
    """

    __slots__ = (
        "_absolute",
        "_closures",
        "_declared",
        "_files",
        "_git_files",
        "_git_texts",
        "_parsed",
        "_parses",
        "_registry",
        "_root",
        "_rule_inputs",
        "_scopes",
        "_seen",
        "_shapes",
        "_table",
        "_tracked",
    )

    def __init__(self, root: Path, parses: ParseCache | None = None) -> None:
        """Keep the root of the tree and the parse cache, or a new parse cache.  Each other fact is read on its first use."""
        self._root = root
        self._parses: ParseCache = {} if parses is None else parses
        # The absolute path of each path relative to the root.
        self._absolute: dict[str, str] = {}
        # The inputs of each rule, by its ID and the subcommand.
        self._rule_inputs: dict[tuple[str, str], Inputs] = {}
        self._registry: rule_registry.Registry | None = None
        self._table: ruletable.RuleTable | None = None
        self._tracked: tuple[str, ...] | None = None
        # The text of the HEAD commit and the text of the list of the files, by the word that starts each one.
        self._git_texts: dict[str, str] = {}
        self._git_files: tuple[str, ...] | None = None
        # The facts of the table RULES of each module that a search of a module read (THE SEARCH).
        self._declared: dict[Path, tuple[bool, list[str]]] = {}
        # The modules of the package that each module imports, and its shape (assay.readers.python), or None.
        self._parsed: dict[str, tuple[tuple[str, ...], python_reader.Shape | None]] = {}
        # The file of each module that a closure read, as a tuple of one path, or of no path for a module with no file.
        self._files: dict[str, tuple[str, ...]] = {}
        self._scopes: dict[tuple[str, Callable[[str], bool] | None], tuple[str, ...]] = {}
        self._closures: dict[frozenset[str], frozenset[str]] = {}
        # The directory of THE PARSE CACHE, or None when the caches are off, in a tuple after the first parse.
        self._shapes: tuple[Path | None] | None = None
        # The SHA-256 of the registry and of the rule table at their first read (THE FACTS OF A CALL).
        self._seen: dict[str, str] = {}

    def table_digest(self, relative: str) -> str:
        """Return the SHA-256 of the bytes of a table of the tree, or the empty text when the table is not a file."""
        path = self._root / relative
        return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else ""

    def is_current(self) -> bool:
        """Tell if each fact of the tree that this Deriver read is the same at this time (THE FACTS OF A CALL).

        A fact that the Deriver did not read counts as the same.  A tree that cannot be read is not current.
        Complexity: one read of each table that the Deriver read, and one listing of git when it read the files.
        """
        try:
            if any(self.table_digest(relative) != digest for relative, digest in self._seen.items()):
                return False
            if self._tracked is not None and git.tracked_files(self._root, with_new=True) != self._tracked:
                return False
            head = self._git_texts.get(HEAD_TEXT)
            return head is None or head == f"{HEAD_TEXT} {git.head(self._root)}"
        except OSError, git.GitError:
            return False

    @property
    def registry(self) -> rule_registry.Registry:
        """The rows of the registry of the tree that read.  A row that does not read is not in it."""
        if self._registry is None:
            path = self._root / rule_registry.REGISTRY_PATH
            text = path.read_text(encoding="utf-8") if path.is_file() else ""
            self._seen[rule_registry.REGISTRY_PATH] = self.table_digest(rule_registry.REGISTRY_PATH)
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

    def head_text(self) -> str:
        """Return the text of the HEAD commit of the tree (THE TEXTS OF GIT).

        Raises:
            git.GitError: If the root is not a git work tree, or it has no commit
        """
        if HEAD_TEXT not in self._git_texts:
            self._git_texts[HEAD_TEXT] = f"{HEAD_TEXT} {git.head(self._root)}"
        return self._git_texts[HEAD_TEXT]

    def files_text(self) -> str:
        """Return the text of the digest of the list of the files of the tree (THE TEXTS OF GIT).

        Raises:
            git.GitError: If the root is not a git work tree
        """
        if FILES_TEXT not in self._git_texts:
            listing = hashlib.sha256("\n".join(self.tracked()).encode("utf-8", "surrogateescape")).hexdigest()
            self._git_texts[FILES_TEXT] = f"{FILES_TEXT} {listing}"
        return self._git_texts[FILES_TEXT]

    def git_files(self) -> list[str]:
        """Return each file of the tree that git reads when it lists the files: each .gitignore and .gitattributes.

        The two files at the root are inputs also when the tree does not hold them.  A new one then changes the inputs.
        One time for each call of `loop test`.

        Raises:
            git.GitError: If the root is not a git work tree
        """
        if self._git_files is None:
            named = (path for path in self.tracked() if path.rpartition("/")[2] in GIT_FILES)
            self._git_files = tuple(sorted({*GIT_FILES, *named}))
        return list(self._git_files)

    def module_file(self, name: str) -> str | None:
        """Return the file of a module of the package relative to the root, or None when the module has no file."""
        if name not in self._files:
            self._files[name] = tuple(place for place in module_path(name) if (self._root / place).is_file())[:1]
        return self._files[name][0] if self._files[name] else None

    def parsed(self, name: str, file: str) -> tuple[tuple[str, ...], python_reader.Shape | None]:
        """Return the modules of the package that a module imports, also in a function, and the shape of the module.

        A module that does not parse gives no import and no shape.  The run of the module then fails, and its test
        runs.
        """
        if name not in self._parsed:
            try:
                text = (self._root / file).read_text(encoding="utf-8")
            except OSError:
                self._parsed[name] = ((), None)
                return self._parsed[name]
            key = (name, file, text)
            if key not in self._parses:
                self._parses[key] = stored_parse(self.shapes_directory(), name, file, text)
            self._parsed[name] = self._parses[key]
        return self._parsed[name]

    def shapes_directory(self) -> Path | None:
        """Return the directory of THE PARSE CACHE, or None when the caches are off.

        The first call holds the cache in its row of LIMITS of assay.core.cache.
        """
        if self._shapes is None:
            directory = cache.cache_root(SHAPES_CACHE)
            if directory is not None:
                cache.hold_bound(directory, cache.LIMITS[SHAPES_CACHE])
            self._shapes = (directory,)
        return self._shapes[0]

    def imports_of(self, name: str, file: str) -> tuple[str, ...]:
        """Return the dotted name of each module of the package that a module imports, also in a function (parsed())."""
        return self.parsed(name, file)[0]

    def shape_of(self, name: str) -> python_reader.Shape | None:
        """Return the shape of a module of the package, or None for a module with no file or a file that does not parse."""
        file = self.module_file(name)
        return None if file is None else self.parsed(name, file)[1]

    def targets(
        self, reference: str, modules: frozenset[str], depth: int = 0
    ) -> tuple[list[tuple[str, str]], set[str]]:
        """Return the functions and the classes of the closure that a reference names, and the functions of git (THE CALLS).

        The function of assay.core.git is a name of the second item, also when the closure does not hold that module.
        A name that a module imports from a second module names the definition of the second module, at most 8 steps.
        """
        prefix = f"{git.__name__}."
        if reference.startswith(prefix):
            return [], {reference.removeprefix(prefix).split(".", 1)[0]}
        parts = reference.split(".")
        module = next(
            (".".join(parts[:end]) for end in range(len(parts), 0, -1) if ".".join(parts[:end]) in modules), ""
        )
        shape = self.shape_of(module) if module else None
        if shape is None:
            return [], set()
        attributes = parts[len(module.split(".")) :]
        if not attributes:
            return [(module, name) for name in shape.definitions], set()
        if attributes[0] in shape.definitions:
            return [(module, attributes[0])], set()
        imported = shape.imports.target(attributes[0])
        if imported is None or depth >= 8:
            return [], set()
        return self.targets(".".join([imported, *attributes[1:]]), modules, depth + 1)

    def git_functions(self, modules: frozenset[str], roots: Iterable[str]) -> set[str]:
        """Return each function of assay.core.git that a test can call (THE CALLS).

        Complexity: linear in the names of the definitions of the closure, with one parse of each module.
        """
        pending: list[tuple[str, str]] = [(module, python_reader.MODULE_SCOPE) for module in modules]
        for root in roots:
            shape = self.shape_of(root) if root in modules else None
            pending += [(root, name) for name in shape.definitions] if shape is not None else []
        seen: set[tuple[str, str]] = set()
        used: set[str] = set()
        while pending:
            node = pending.pop()
            if node in seen:
                continue
            seen.add(node)
            module, name = node
            shape = self.shape_of(module)
            if shape is None:
                continue
            definition = shape.module if name == python_reader.MODULE_SCOPE else shape.definitions.get(name)
            if definition is None:
                continue
            pending += [(module, plain) for plain in definition.names if plain in shape.definitions]
            for reference in definition.references:
                found, functions = self.targets(reference, modules)
                pending += found
                used |= functions
        return used

    def git_texts(self, modules: frozenset[str], roots: Iterable[str]) -> tuple[list[str], list[str]]:
        """Return the texts and the files of git of a test with a closure and the modules that it starts with.

        THE TEXTS OF GIT gives the rules.  Complexity: the cost of git_functions().

        Raises:
            git.GitError: If the root is not a git work tree, or it has no commit
        """
        used = self.git_functions(modules, roots)
        texts: list[str] = []
        files: list[str] = []
        if used & git.READS_HISTORY:
            texts.append(self.head_text())
        if used & git.READS_NAMES:
            texts.append(self.files_text())
            files += self.git_files()
        return texts, files

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
            self._seen[RULE_TABLE] = self.table_digest(RULE_TABLE)
            self._table = ruletable.load(self._root / RULE_TABLE)
        return self._table.class_of(path)[0]

    def scope_files(self, item: str, predicate: Callable[[str], bool] | None = None) -> tuple[str, ...]:
        """Return the files of the tree that one item of a scope or of the field inputs names (THE SCOPE).

        A path with no "/" at its end names its one file, also when the tree does not hold it.  For a directory and a
        word, `predicate` gives the files that the runner of the engine of the rule gives it, and None gives each file.
        Complexity: O(f) for f files of the tree, one time for each different item and predicate.

        Raises:
            git.GitError: If the root is not a git work tree
        """
        if not item.endswith("/") and item not in rule_registry.SCOPE_WORDS:
            return (item,)
        key = (item, predicate)
        if key not in self._scopes:
            from assay.guard.engines.meta import package

            found: list[str] = []
            for path in self.tracked():
                if predicate is not None and not predicate(path):
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
                found += self.scope_files(item)
        return found

    @staticmethod
    def data_directories(modules: Iterable[str]) -> list[str]:
        """Return the directory of each item of MODULE_DATA that the modules give and that names a directory."""
        return [
            directory
            for name in modules
            for item in MODULE_DATA.get(name, ())
            if (directory := directory_of(item)) is not None
        ]

    def absolute(self, files: Iterable[str]) -> tuple[str, ...]:
        """Return the absolute path of each path relative to the root, one time each, sorted.

        Complexity: O(n log n) for n paths, with one join of each different path for each Deriver.
        """
        memo = self._absolute
        found: set[str] = set()
        for file in files:
            path = memo.get(file)
            if path is None:
                path = memo[file] = str(self._root / file)
            found.add(path)
        return tuple(sorted(found))

    def rule_modules(
        self, rule: rule_registry.Rule, subcommand: str
    ) -> tuple[list[str], tuple[str, ...], tuple[str, ...]]:
        """Return the roots of the closure of a rule, and the files and the names that the search of its module read.

        The roots are the subcommand, the runners and the module that the search finds.  The module of the field legacy
        or the witness of a holder is a root too.  The runner of each rule runs the search, also for a rule with the
        field legacy or held-by (THE SEARCH).
        """
        from assay.guard.engines.meta import runner as meta_runner

        roots = [CHECK if subcommand == "check" else WITNESS, META_RUNNER]
        roots.append(f"{ENGINES}{rule.engine.directory}.runner")
        if rule.engine == rule_registry.Engine.PLUGIN and subcommand == "witness":
            roots.append(PLUGIN_WITNESS)
        if rule.legacy is not None:
            roots.append(rule.legacy.qualified)
        elif rule.held_by is not None:
            roots.append(HELD_WITNESS)
        search = meta_runner.module_search(rule, self._root, self.declared)
        roots.append(search.name)
        return roots, search.read, search.listed

    def declared(self, path: Path) -> tuple[bool, list[str]]:
        """Return the facts of the table RULES of one module (declared_rules() of the runner of meta), one time for each call."""
        if path not in self._declared:
            from assay.guard.engines.meta import runner as meta_runner

            self._declared[path] = meta_runner.declared_rules(path)
        return self._declared[path]

    @staticmethod
    def scope_predicate(rule: rule_registry.Rule) -> Callable[[str], bool] | None:
        """Return the predicate of the files that the runner of the engine of a rule gives it, or None for each file.

        The runner of the engine tsast gives a rule only the C++ files that are not plants.  The runner of the engine
        plugin reads only the findings of the C and C++ files that a compile reads (THE SCOPE).
        """
        if rule.engine == rule_registry.Engine.PLUGIN:
            return is_compiled_file
        if rule.engine != rule_registry.Engine.TSAST:
            return None
        from assay.guard.engines.tsast import runner as tree_runner

        return tree_runner.is_candidate

    def rule_inputs(self, rule_id: str, subcommand: str) -> Inputs:
        """Return the inputs of one rule for the subcommand check or witness (THE INPUTS OF A RULE).

        The Deriver calculates the inputs of each rule and subcommand one time.

        Raises:
            git.GitError: If the root is not a git work tree, or it has no commit
        """
        key = (rule_id, subcommand)
        if key not in self._rule_inputs:
            self._rule_inputs[key] = self._derive_rule_inputs(rule_id, subcommand)
        return self._rule_inputs[key]

    def warm(self) -> None:
        """Calculate the inputs of each rule of the registry for the subcommands check and witness, and keep them.

        `loop test` calls it while the build runs (THE WARM START of assay.loop.affected).  A tree that cannot be read
        stops the warm start with no error, and test_inputs() then reports the problem.  Complexity: the cost of
        rule_inputs() for each rule, with one parse of each module.
        """
        try:
            for rule_id in sorted(self.registry.rules):
                for subcommand in ("check", "witness"):
                    self.rule_inputs(rule_id, subcommand)
        except git.GitError, OSError, ruletable.TableError, rows_grammar.RowsError:
            return

    def _derive_rule_inputs(self, rule_id: str, subcommand: str) -> Inputs:
        """Return the inputs of one rule for a subcommand, with no memo (rule_inputs())."""
        rule = self.registry.rules.get(rule_id)
        if rule is None:
            return Inputs(always=f"the registry has no readable row of the rule {rule_id}")
        build = ""
        if subcommand == "check" and BUILD_SCOPE in rule.scope:
            build = f"the rule {rule_id} reads the build directory (the scope word {BUILD_SCOPE})"
        roots, searched, listed = self.rule_modules(rule, subcommand)
        modules = self.closure(roots)
        predicate = self.scope_predicate(rule)
        files = [*MASTERS, RULE_TABLE, f"{LEDGERS}/{rule_id}.txt", *searched]
        files += [file for name in modules if (file := self.module_file(name)) is not None]
        files += self.scope_files(f"{PLANTS}{rule_id}/")
        directories = [f"{PLANTS}{rule_id}", f"{ENGINE_DIRECTORY}/{rule.engine.directory}"]
        for item in rule.scope:
            if item != BUILD_SCOPE:
                files += self.scope_files(item, predicate)
                directory = directory_of(item) if predicate is None else None
                directories += [directory] if directory is not None else []
        for item in rule.inputs:
            files += self.scope_files(item)
            directories += [directory] if (directory := directory_of(item)) is not None else []
        files += self.data_files(modules)
        directories += self.data_directories(modules)
        texts, read_by_git = self.git_texts(modules, roots)
        files += read_by_git
        held = [other.id for other in self.registry.rules.values() if other.held_by == rule_id]
        texts += self.row_texts([rule_id, *held])
        if listed:
            texts.append(f"{SEARCH_TEXT} {rule.engine.directory}: {' '.join(listed)}")
        return Inputs(
            self.absolute(files),
            tuple(sorted(texts)),
            build=build,
            directories=self.absolute(directories),
            rules=tuple(sorted({rule_id, *held})),
        )

    def row_texts(self, rule_ids: Iterable[str]) -> tuple[str, ...]:
        """Return the text of the row of each rule with no line number, or a text that tells that the rule has no row."""
        return tuple(sorted(row_text(self.registry.rules.get(rule_id), rule_id) for rule_id in rule_ids))

    def meter_inputs(self, module: str, rule_ids: Iterable[str]) -> Inputs:
        """Return the inputs of a step meter of the master loop that runs in front of each test (THE METERS OF A TEST).

        Args:
            module: The module of the meter, such as assay.loop.teststep
            rule_ids: The rules that the meter judges

        Complexity: one parse of each module of the closure, one time for each call of `loop test`.
        """
        rules = tuple(sorted(set(rule_ids)))
        files = [
            LOOP_MASTER,
            *(file for name in self.closure([module]) if (file := self.module_file(name)) is not None),
        ]
        files += [f"{LEDGERS}/{rule_id}.txt" for rule_id in rules]
        return Inputs(self.absolute(files), self.row_texts(rules), rules=rules)

    def call_inputs(self, call: Call) -> Inputs:
        """Return the inputs of a rule test: the union of the inputs of its rules (THE TESTS).

        Raises:
            git.GitError: If the root is not a git work tree
        """
        if call.has_root:
            return Inputs(always="the call reads the tree of the option --root")
        build = ""
        if call.subcommand == "check" and call.engines:
            build = f"a check of the engine {call.engines[0]} reads the objects of the build"
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
        texts: set[str] = set()
        directories: set[str] = set()
        rules: set[str] = set()
        for rule_id in rule_ids:
            found = self.rule_inputs(rule_id, call.subcommand)
            if found.always:
                return found
            build = build or found.build
            files.update(found.files)
            texts.update(found.texts)
            directories.update(found.directories)
            rules.update(found.rules)
        if call.runs_compiler:
            files.update(self.absolute(self.scope_files(INCLUDE)))
            directories.update(self.absolute([INCLUDE.rstrip("/")]))
        if call.subcommand == "witness" and call.build_dir:
            files.add(str(compdb.database_file(Path(call.build_dir))))
        return Inputs(
            tuple(sorted(files)),
            tuple(sorted(texts)),
            build=build,
            directories=tuple(sorted(directories)),
            rules=tuple(sorted(rules)),
        )

    def group_inputs(self, group: str, runs_compiler: bool) -> Inputs:
        """Return the inputs of a group test (THE INPUTS OF A GROUP).

        Raises:
            git.GitError: If the root is not a git work tree, or it has no commit
        """
        tests = f"{TESTS}/{group}/"
        names = [
            ".".join(PurePosixPath(path).with_suffix("").parts[1:])
            for path in self.scope_files(tests)
            if PurePosixPath(path).name.startswith("test_") and path.endswith(".py")
        ]
        roots = [SELFTEST, *names]
        modules = self.closure(roots)
        files = [*MASTERS, *(file for name in modules if (file := self.module_file(name)) is not None)]
        files += self.data_files(modules)
        directories = [tests.rstrip("/"), *self.data_directories(modules)]
        if REGISTRY_MODULE in modules:
            files.append(rule_registry.REGISTRY_PATH)
        if runs_compiler:
            files += self.scope_files(INCLUDE)
            directories.append(INCLUDE.rstrip("/"))
        texts, read_by_git = self.git_texts(modules, roots)
        files += read_by_git
        return Inputs(self.absolute(files), tuple(sorted(texts)), directories=self.absolute(directories))

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
