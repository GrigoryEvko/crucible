"""The registry: the rules, the jobs and the permitted paths of the utils system (utils/registry.txt).

THE ROWS
    The registry obeys the rows grammar of assay.core.rows.  It has a head
    comment, a format row and four row kinds:

        format 1
        rule ID engine=ENGINE scope=SCOPE[,SCOPE...] ledger=POLICY [FIELD=VALUE]... doc=REF | REFUSES | ACTION
        job ENTITY module=OWNER[,OWNER...] | REASON
        path GLOB | WHAT THE FILES ARE
        term WORD category=CATEGORY | ITS MEANING IN THIS PROJECT

    A rule row has these fields:
      engine       plugin, tsast, elf, build or meta: one engine for each rule
      scope        a path relative to the source root (a directory ends with
                   '/'), or one of the words base, quarantined, tracked,
                   builddir and package
      ledger       none, count, admit, max or baseline (assay.core.ledger)
      kinds        the finding kinds of the rule
      warn, error, unit
                   the two thresholds and the unit of a measured rule.  A
                   rule has the three fields or none of them.  The unit is
                   one word: s, G (10^9 instructions), GB, MB, KB, %,
                   operations, elements, x (a multiple) or links
      inputs       the paths that the rule reads and that its module does not
                   show
      doc          the section of CLAUDE.md that states the rule, such as XV,
                   XV.4, XXII.R11 or L0
      legacy       the module under assay that holds the rule at this time,
                   when no module of assay/guard/engines/ holds it
      legacy-args  the words that select the mode of the legacy module,
                   divided by commas
      held-by      the rule or the command that reads the thresholds of a
                   measured rule that has no module of its own: a rule ID,
                   or guard-warnings, loop-gauge, loop-history, loop-step,
                   loop-test-step, loop-fixture or cmake
    The first cell tells what the rule refuses, or what a measured rule
    measures.  The second cell is the default action, in the imperative.

    A rule has at most one of legacy and held-by.  A rule with neither has a
    module of its own in assay/guard/engines/ENGINE/, which the rule
    rule-implementations holds.

    A job row names a Python entity and the modules that can use it.  An
    owner is a dotted name from utils/: a module of the package, such as
    assay.core.proc, or a master, such as bin.guard.  The rule job-owners
    reads the job rows.  A path row gives a glob of tracked paths and what
    those files are.  The rule layout reads the path rows.

    A term row adds a word to the glossary of the project.  The STE
    dictionary refuses the word, and this project uses it as a technical noun
    or a technical verb.  CATEGORY is noun-N for the technical-noun category N of
    rule 1.5 (1 to 22), or verb-N for the technical-verb category N of rule
    1.12 (1a to 1f, 2a to 2c, 3a to 3f, or 4).  The rule ste-prose refuses no
    shape of a word of a term row.  The rule registry-shape refuses a term row
    with a word that the dictionary does not refuse.

LOAD AND EXAMINE
    load() reads the registry and refuses the first problem.  examine() reads
    each row and returns each problem, for the rule registry-shape.

`guard selftest tests.core.test_registry` does a test of the module.
"""

import enum
from collections.abc import Iterable, Mapping
from dataclasses import dataclass, field
from pathlib import Path

from assay.core import rows as rows_grammar
from assay.core.paths import REPO_ROOT

REGISTRY = REPO_ROOT / "utils" / "registry.txt"
# The path of the registry in each copy of the tree, from its root.
REGISTRY_PATH = "utils/registry.txt"
FORMAT = "1"
RULE_FIELDS = frozenset({"engine", "scope", "ledger", "kinds", "warn", "error", "unit", "inputs", "doc", "legacy",
                         "legacy-args", "held-by"})
REQUIRED_FIELDS = ("engine", "scope", "ledger", "doc")
THRESHOLD_FIELDS = ("warn", "error", "unit")
JOB_FIELDS = frozenset({"module"})
TERM_FIELDS = frozenset({"category"})
# The categories of a term row: the technical-noun categories of rule 1.5, and the technical-verb categories of
# rule 1.12.
TERM_CATEGORIES = frozenset({*(f"noun-{number}" for number in range(1, 23)),
                             *(f"verb-{group}{letter}" for group, letters in (("1", "abcdef"), ("2", "abc"),
                                                                             ("3", "abcdef")) for letter in letters),
                             "verb-4"})
# The two prefixes of the name of an owner of a job: a module of the package, and a master of utils/bin/.
PACKAGE_OWNER = "assay."
MASTER_OWNER = "bin."
SCOPE_WORDS = frozenset({"base", "quarantined", "tracked", "builddir", "package"})
UNITS = frozenset({"s", "G", "GB", "MB", "KB", "%", "operations", "elements", "x", "links"})
# The commands that read the thresholds of a measured rule with no module of its own.
HOLDING_COMMANDS = frozenset({"guard-warnings", "loop-gauge", "loop-history", "loop-step", "loop-test-step",
                              "loop-fixture", "cmake"})


class Engine(enum.StrEnum):
    """The five engines: the input that a rule reads."""

    PLUGIN = "plugin"
    TSAST = "tsast"
    ELF = "elf"
    BUILD = "build"
    META = "meta"


class Policy(enum.StrEnum):
    """The policy of the ledger of a rule (assay.core.ledger)."""

    NONE = "none"
    COUNT = "count"
    ADMIT = "admit"
    MAX = "max"
    BASELINE = "baseline"


@dataclass(frozen=True, slots=True)
class Threshold:
    """The two thresholds and the unit of a measured rule."""

    warn: float
    error: float
    unit: str


@dataclass(frozen=True, slots=True)
class Legacy:
    """The module that holds a rule at this time, and the words that select its mode."""

    module: str
    words: tuple[str, ...]

    @property
    def qualified(self) -> str:
        """The full dotted name of the module."""
        return f"assay.{self.module}"


@dataclass(frozen=True, slots=True)
class Rule:
    """One rule row."""

    id: str
    engine: Engine
    scope: tuple[str, ...]
    policy: Policy
    kinds: tuple[str, ...]
    threshold: Threshold | None
    inputs: tuple[str, ...]
    doc: str
    refuses: str
    action: str
    legacy: Legacy | None
    held_by: str | None
    line: int

    @property
    def has_module(self) -> bool:
        """Whether a module of assay/guard/engines/ holds the rule: the rule has no legacy module and no holder."""
        return self.legacy is None and self.held_by is None


@dataclass(frozen=True, slots=True)
class Job:
    """One job row: a Python entity and the modules that can use it."""

    entity: str
    modules: tuple[str, ...]
    reason: str
    line: int


@dataclass(frozen=True, slots=True)
class PathRow:
    """One path row: a glob of tracked paths and what those files are."""

    glob: str
    text: str
    line: int


@dataclass(frozen=True, slots=True)
class Term:
    """One term row: a word of the glossary of the project, its STE category and its meaning."""

    word: str
    category: str
    meaning: str
    line: int


@dataclass(frozen=True, slots=True)
class Registry:
    """The rows of the registry."""

    path: str
    rules: Mapping[str, Rule] = field(default_factory=dict)
    jobs: tuple[Job, ...] = ()
    paths: tuple[PathRow, ...] = ()
    terms: tuple[Term, ...] = ()

    def measured(self) -> dict[str, Rule]:
        """Return each rule with thresholds, by its ID."""
        return {rule_id: rule for rule_id, rule in self.rules.items() if rule.threshold is not None}


def is_rule_id(text: str) -> bool:
    """Whether a text is a rule ID: lowercase words of letters and digits, with a hyphen between two words."""
    if not text or not ("a" <= text[0] <= "z") or text.endswith("-") or "--" in text:
        return False
    return all("a" <= character <= "z" or "0" <= character <= "9" or character == "-" for character in text)


def is_module_name(text: str) -> bool:
    """Whether a text is a dotted module name under assay, such as guard.legacy.check_cache_size."""
    return bool(text) and all(part.isidentifier() and part.islower() or part == "__init__"
                              for part in text.split("."))


def is_doc_reference(text: str) -> bool:
    """Whether a text has the form of a section of CLAUDE.md: a section, and a rule or an item after a point."""
    section, point, item = text.partition(".")
    is_roman = bool(section) and all(character in "IVXL" for character in section)
    is_layer = section.startswith("L") and section[1:].isdigit()
    if not (is_roman or is_layer):
        return False
    if not point:
        return True
    return item.isdigit() or (item.startswith("R") and item[1:].isdigit())


def parse_rule(row: rows_grammar.Row) -> Rule:
    """Read one rule row.

    Raises:
        rows_grammar.RowsError: If the row does not have the form of a rule row
    """
    if len(row.words) < 2 or not is_rule_id(row.words[1]):
        raise row.error("a rule row starts with 'rule ID', and an ID is lowercase words with hyphens")
    rule_id = row.words[1]
    values = rows_grammar.fields(row, RULE_FIELDS)
    missing = [name for name in REQUIRED_FIELDS if name not in values]
    if missing:
        raise row.error(f"the rule {rule_id} has no field {', '.join(missing)}")
    if len(row.cells) != 2 or not all(row.cells):
        raise row.error(f"the rule {rule_id} has {len(row.cells)} cells.  A rule row has two: what the rule "
                        f"refuses, and the default action")
    try:
        engine = Engine(values["engine"])
    except ValueError:
        raise row.error(f"the engine {values['engine']!r} of {rule_id} is not one of "
                        f"{', '.join(item.value for item in Engine)}") from None
    try:
        policy = Policy(values["ledger"])
    except ValueError:
        raise row.error(f"the ledger policy {values['ledger']!r} of {rule_id} is not one of "
                        f"{', '.join(item.value for item in Policy)}") from None
    scope = tuple(values["scope"].split(","))
    for item in scope:
        if not item or (item not in SCOPE_WORDS and (item.startswith("/") or ".." in item.split("/"))):
            raise row.error(f"the scope {item!r} of {rule_id} is not a path relative to the source root or one of "
                            f"{', '.join(sorted(SCOPE_WORDS))}")
    given = [name for name in THRESHOLD_FIELDS if name in values]
    threshold = None
    if given and len(given) != len(THRESHOLD_FIELDS):
        raise row.error(f"the rule {rule_id} has {', '.join(given)}, and a measured rule has warn, error and unit")
    if given:
        warn = rows_grammar.decimal(values["warn"], row, "warning threshold")
        error = rows_grammar.decimal(values["error"], row, "error threshold")
        if warn > error:
            raise row.error(f"the warning threshold {values['warn']} of {rule_id} is above its error threshold "
                            f"{values['error']}")
        if values["unit"] not in UNITS:
            raise row.error(f"the unit {values['unit']!r} of {rule_id} is not one of {', '.join(sorted(UNITS))}")
        threshold = Threshold(warn, error, values["unit"])
    if not is_doc_reference(values["doc"]):
        raise row.error(f"the doc {values['doc']!r} of {rule_id} is not a section of CLAUDE.md, such as XV, XV.4, "
                        f"XXII.R11 or L0")
    legacy = None
    if "legacy" in values:
        if not is_module_name(values["legacy"]):
            raise row.error(f"the legacy module {values['legacy']!r} of {rule_id} is not a dotted module name under "
                            f"assay")
        legacy = Legacy(values["legacy"], tuple(values["legacy-args"].split(",")) if "legacy-args" in values else ())
    elif "legacy-args" in values:
        raise row.error(f"the rule {rule_id} has legacy-args and no legacy module")
    held_by = values.get("held-by")
    if held_by is not None and legacy is not None:
        raise row.error(f"the rule {rule_id} has a legacy module and a holder.  A rule has at most one of the two")
    if held_by is not None and threshold is None:
        raise row.error(f"the rule {rule_id} has a holder and no thresholds.  Only a measured rule has a holder")
    if held_by is not None and held_by not in HOLDING_COMMANDS and not is_rule_id(held_by):
        raise row.error(f"the holder {held_by!r} of {rule_id} is not a rule ID or one of "
                        f"{', '.join(sorted(HOLDING_COMMANDS))}")
    kinds = tuple(values["kinds"].split(",")) if "kinds" in values else ()
    inputs = tuple(values["inputs"].split(",")) if "inputs" in values else ()
    return Rule(rule_id, engine, scope, policy, kinds, threshold, inputs, values["doc"], row.cells[0], row.cells[1],
                legacy, held_by, row.line)


def parse_job(row: rows_grammar.Row) -> Job:
    """Read one job row.

    Raises:
        rows_grammar.RowsError: If the row does not have the form of a job row
    """
    if len(row.words) < 3 or len(row.cells) != 1 or not row.cells[0]:
        raise row.error("a job row is 'job ENTITY module=MODULE[,MODULE...] | REASON'")
    values = rows_grammar.fields(row, JOB_FIELDS)
    if "module" not in values:
        raise row.error(f"the job {row.words[1]} has no field module")
    modules = tuple(values["module"].split(","))
    if not all(is_owner_name(module) for module in modules):
        raise row.error(f"an owner of the job {row.words[1]} is not a module of the package, such as assay.core.proc, "
                        f"or a master, such as bin.guard")
    return Job(row.words[1], modules, row.cells[0], row.line)


def is_owner_name(text: str) -> bool:
    """Return True when a text names an owner of a job: assay.MODULE for a module of the package, or bin.NAME."""
    if text.startswith(PACKAGE_OWNER):
        return is_module_name(text[len(PACKAGE_OWNER):])
    name = text.removeprefix(MASTER_OWNER)
    return text.startswith(MASTER_OWNER) and name.isidentifier() and name.islower() and "_" not in name


def is_term_word(text: str) -> bool:
    """Return True when a text is a word of the glossary: parts of lowercase letters, with a hyphen between parts."""
    return bool(text) and all(part.isalpha() and part.islower() for part in text.split("-"))


def parse_term(row: rows_grammar.Row) -> Term:
    """Read one term row.

    Raises:
        rows_grammar.RowsError: If the row is not a correct term row, or its category is not a category of rule 1.5
            or rule 1.12
    """
    if len(row.words) != 3 or len(row.cells) != 1 or not row.cells[0] or not is_term_word(row.words[1]):
        raise row.error("a term row is 'term WORD category=CATEGORY | ITS MEANING IN THIS PROJECT', with a lowercase "
                        "WORD")
    values = rows_grammar.fields(row, TERM_FIELDS)
    category = values.get("category", "")
    if category not in TERM_CATEGORIES:
        raise row.error(f"the category {category!r} of the term {row.words[1]} is not noun-N of rule 1.5 (1 to 22) or "
                        f"verb-N of rule 1.12 (1a to 1f, 2a to 2c, 3a to 3f, or 4)")
    return Term(row.words[1], category, row.cells[0], row.line)


def parse_path(row: rows_grammar.Row) -> PathRow:
    """Read one path row.

    Raises:
        rows_grammar.RowsError: If the row does not have the form of a path row
    """
    if len(row.words) != 2 or len(row.cells) != 1 or not row.cells[0]:
        raise row.error("a path row is 'path GLOB | WHAT THE FILES ARE'")
    glob = row.words[1]
    if glob.startswith("/") or ".." in glob.split("/"):
        raise row.error(f"the glob {glob!r} is not relative to the source root")
    return PathRow(glob, row.cells[0], row.line)


def examine(text: str, shown: str = REGISTRY_PATH) -> tuple[Registry, list[rows_grammar.RowsError]]:
    """Read the registry, and return each row that reads and each problem.

    A row with a problem is not in the result.  A grammar error of the file
    stops the read, because the rows grammar reads the file as a whole.
    Complexity: linear in the length of the registry.
    """
    problems: list[rows_grammar.RowsError] = []
    try:
        all_rows = rows_grammar.parse(text, shown)
    except rows_grammar.RowsError as problem:
        return Registry(shown), [problem]
    rules: dict[str, Rule] = {}
    jobs: list[Job] = []
    paths: list[PathRow] = []
    terms: list[Term] = []
    seen_globs: dict[str, int] = {}
    seen_jobs: dict[str, int] = {}
    seen_terms: dict[str, int] = {}
    if not all_rows or all_rows[0].words != ("format", FORMAT) or all_rows[0].cells:
        problems.append(rows_grammar.RowsError(shown, all_rows[0].line if all_rows else 0,
                                               f"the first row of the registry is 'format {FORMAT}'"))
    for row in all_rows[1:] if all_rows and all_rows[0].words[0] == "format" else all_rows:
        kind = row.words[0]
        try:
            if kind == "rule":
                rule = parse_rule(row)
                if rule.id in rules:
                    raise row.error(f"the rule {rule.id} has a second row.  The first is at line {rules[rule.id].line}")
                rules[rule.id] = rule
            elif kind == "job":
                job = parse_job(row)
                if job.entity in seen_jobs:
                    raise row.error(f"the job {job.entity} has a second row.  The first is at line "
                                    f"{seen_jobs[job.entity]}")
                seen_jobs[job.entity] = row.line
                jobs.append(job)
            elif kind == "path":
                path_row = parse_path(row)
                if path_row.glob in seen_globs:
                    raise row.error(f"the glob {path_row.glob} has a second row.  The first is at line "
                                    f"{seen_globs[path_row.glob]}")
                seen_globs[path_row.glob] = row.line
                paths.append(path_row)
            elif kind == "term":
                term = parse_term(row)
                if term.word in seen_terms:
                    raise row.error(f"the term {term.word} has a second row.  The first is at line "
                                    f"{seen_terms[term.word]}")
                seen_terms[term.word] = row.line
                terms.append(term)
            else:
                raise row.error(f"the row kind {kind!r} is not format, rule, job, path or term")
        except rows_grammar.RowsError as problem:
            problems.append(problem)
    return Registry(shown, rules, tuple(jobs), tuple(paths), tuple(terms)), problems


def load(path: Path = REGISTRY, shown: str = REGISTRY_PATH) -> Registry:
    """Read the registry, and refuse its first problem.

    Raises:
        rows_grammar.RowsError: If a row does not obey the grammar or the schema
        OSError: If the file cannot be read
    """
    registry, problems = examine(path.read_text(encoding="utf-8"), shown)
    if problems:
        raise problems[0]
    return registry


def doc_section(guide: str, reference: str) -> str | None:
    """Return the text of CLAUDE.md that a doc field names, or None when the guide has no such text.

    A reference is a section, such as XV or L0: the lines from its heading
    "## XV." or "## L0 " to the next heading of that level.  XV.4 is item 4 of
    the section: the line that starts with "4. ", and the later one when the
    section has two numbered lists.  XXII.R11 is the subsection of the
    section whose heading holds the word R11.

    Complexity: linear in the length of the guide.
    """
    section, point, item = reference.partition(".")
    lines = guide.splitlines()
    start = next((index for index, line in enumerate(lines) if line.startswith("## ")
                  and line[3:].split(" ", 1)[0].rstrip(".") == section), None)
    if start is None:
        return None
    end = next((index for index in range(start + 1, len(lines)) if lines[index].startswith("## ")), len(lines))
    body = lines[start:end]
    if not point:
        return "\n".join(body)
    if item.isdigit():
        matches = [line for line in body if line.startswith(f"{item}. ")]
        return matches[-1] if matches else None
    for index, line in enumerate(body):
        if line.startswith("### ") and item in line.replace("(", " ").replace(")", " ").replace(",", " ").split(" "):
            stop = next((later for later in range(index + 1, len(body)) if body[later].startswith("### ")), len(body))
            return "\n".join(body[index:stop])
    return None


def measured_rule(rule_id: str, warn: float, error: float, unit: str, measures: str = "a planted quantity") -> str:
    """Return the text of one measured rule row, for a planted registry of a test.

    Raises:
        ValueError: If the row cannot be written
    """
    def number_text(value: float) -> str:
        return str(int(value)) if float(value).is_integer() else repr(float(value))

    return rows_grammar.render(["rule", rule_id, "engine=build", "scope=builddir", "ledger=max",
                                f"warn={number_text(warn)}", f"error={number_text(error)}", f"unit={unit}",
                                "held-by=cmake", "doc=XV"], [measures, "lower the value"])


def planted(rows: Iterable[str], comment: str = "# a planted registry\n") -> str:
    """Return the text of a registry with a head comment, the format row and the rows."""
    return comment + f"format {FORMAT}\n" + "".join(f"{row}\n" for row in rows)
