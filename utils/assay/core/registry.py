"""The registry: the rules and the permitted paths of the utils system (utils/registry.txt).

THE ROWS
    The registry obeys the rows grammar of assay.core.rows.  It has a head
    comment, a format row and two row kinds:

        format 1
        rule ID engine=ENGINE scope=SCOPE[,SCOPE...] ledger=POLICY [FIELD=VALUE]... doc=REF | REFUSES | ACTION
        path GLOB | WHAT THE FILES ARE

    A rule row has these fields:
      engine       plugin, tsast, elf, build or meta: one engine for each rule
      scope        a path relative to the source root (a directory ends with
                   '/'), or one of the words base, quarantined, tracked,
                   builddir and package
      ledger       none, count, admit, max or baseline (assay.core.ledger)
      kinds        the finding kinds of the rule
      limit, unit  the limit and the unit of a measured rule.  A rule has the
                   two fields or none of them.  A value above the limit
                   fails.  The limit is one number, or one number for each
                   kind of item of the rule, KIND:NUMBER,KIND:NUMBER, such as
                   compile:4,link:1,test:1.5 (limits() of assay.core.rows).
                   The judge of an item reads the limit of its kind.  The
                   unit is one word: s, G (10^9 instructions), GB, MB, KB,
                   bytes, %, operations, elements, x (a multiple), links,
                   jobs or threads
      inputs       the paths that the rule reads and that its module does not
                   show
      doc          the section of CLAUDE.md that states the rule, such as XV,
                   XV.4, XXII.R11 or L0
      legacy       the module under assay that holds the rule at this time,
                   when no module of assay/guard/engines/ holds it
      legacy-args  the words that select the mode of the legacy module,
                   divided by commas
      held-by      the rule or the command that reads the limit of a
                   measured rule that has no module of its own: a rule ID,
                   or loop-gauge, loop-step or loop-fixture.
                   A step meter (loop-step or loop-fixture)
                   can also hold a rule with no limit, which it judges
                   for each step
      holder       the function of the holder that judges a rule with
                   held-by, as MODULE:FUNCTION, such as
                   assay.loop.gauge:judged_record.  MODULE is a module of the
                   package.  The witness of the rule runs the function
                   (assay.guard.engines.buildsys.held)
    The first cell tells what the rule refuses, or what a measured rule
    measures.  The second cell is the default action, in the imperative.

    A rule has at most one of legacy and held-by.  A rule with neither has a
    module of its own in assay/guard/engines/ENGINE/, which the rule
    rule-implementations holds.  A rule with held-by has the field holder
    too, and the rule rule-implementations holds that field.  Only a rule
    with held-by has the field holder.

    A path row gives a glob of tracked paths and what those files are.  The
    rule layout reads the path rows.

LOAD AND EXAMINE
    load() reads the registry and refuses the first problem.  examine() reads
    each row and returns each problem, for the rule registry-shape.

`guard selftest tests.core.test_registry` does a test of the module.
"""

import enum
import keyword
from collections.abc import Iterable, Mapping
from dataclasses import dataclass, field
from pathlib import Path

from assay.core import rows as rows_grammar
from assay.core.paths import REPO_ROOT

REGISTRY = REPO_ROOT / "utils" / "registry.txt"
# The path of the registry in each copy of the tree, from its root.
REGISTRY_PATH = "utils/registry.txt"
FORMAT = "1"
RULE_FIELDS = frozenset(
    {
        "engine",
        "scope",
        "ledger",
        "kinds",
        "limit",
        "unit",
        "inputs",
        "doc",
        "legacy",
        "legacy-args",
        "held-by",
        "holder",
    }
)
REQUIRED_FIELDS = ("engine", "scope", "ledger", "doc")
THRESHOLD_FIELDS = ("limit", "unit")
# The prefix of the module of the field holder: a module of the package.
PACKAGE_OWNER = "assay."
SCOPE_WORDS = frozenset({"base", "quarantined", "tracked", "builddir", "package"})
UNITS = frozenset(
    {
        "s",
        "G",
        "GB",
        "MB",
        "KB",
        "bytes",
        "%",
        "operations",
        "elements",
        "x",
        "links",
        "jobs",
        "threads",
    }
)
# The commands that read the limit of a measured rule with no module of its own.
HOLDING_COMMANDS = frozenset(
    {
        "loop-gauge",
        "loop-step",
        "loop-fixture",
    }
)
# The step meters, which judge each step that they run.  Such a meter can also hold a rule with no limit.
STEP_METERS = frozenset({"loop-step", "loop-fixture"})
# The character between the module and the function in the field holder.
HOLDER_SEPARATOR = ":"


class Engine(enum.StrEnum):
    """The five engines: the input that a rule reads."""

    PLUGIN = "plugin"
    TSAST = "tsast"
    ELF = "elf"
    BUILD = "build"
    META = "meta"

    @property
    def directory(self) -> str:
        """The directory of the modules of the engine in utils/assay/guard/engines/.

        It is the name of the engine.  The engine build has the directory
        buildsys, because the pattern build/ of .gitignore matches a
        directory with that name at each depth.
        """
        return "buildsys" if self is Engine.BUILD else self.value


class Policy(enum.StrEnum):
    """The policy of the ledger of a rule (assay.core.ledger)."""

    NONE = "none"
    COUNT = "count"
    ADMIT = "admit"
    MAX = "max"
    BASELINE = "baseline"


@dataclass(frozen=True, slots=True)
class Threshold:
    """The limits and the unit of a measured rule: one limit for each kind of item.  A value above its limit fails.

    A rule with one limit has the one kind "".  `limits` keeps the sequence of the field limit of the row.
    """

    limits: tuple[tuple[str, float], ...]
    unit: str

    @property
    def kinds(self) -> tuple[str, ...]:
        """The kinds of the items of the rule, in the sequence of the row: ("",) for a rule with one limit."""
        return tuple(kind for kind, _ in self.limits)

    @property
    def limit(self) -> float:
        """The limit of a rule with one limit.

        Raises:
            ValueError: If the rule has one limit for each kind of item
        """
        if self.kinds != ("",):
            raise ValueError(
                f"the rule has one limit for each kind of item ({', '.join(self.kinds)}), and no one limit"
            )
        return self.limits[0][1]

    def limit_of(self, kind: str) -> float | None:
        """Return the limit of one kind of item, or None when the rule has no such kind."""
        return next((limit for name, limit in self.limits if name == kind), None)


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
class Holder:
    """The function of the holder that judges a rule with held-by: a module of the package and a function of it."""

    module: str
    function: str

    @property
    def text(self) -> str:
        """The field holder of the row: MODULE:FUNCTION."""
        return f"{self.module}{HOLDER_SEPARATOR}{self.function}"


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
    holder: Holder | None
    line: int

    @property
    def has_module(self) -> bool:
        """Whether a module of assay/guard/engines/ holds the rule: the rule has no legacy module and no holder."""
        return self.legacy is None and self.held_by is None


@dataclass(frozen=True, slots=True)
class PathRow:
    """One path row: a glob of tracked paths and what those files are."""

    glob: str
    text: str
    line: int


@dataclass(frozen=True, slots=True)
class Registry:
    """The rows of the registry."""

    path: str
    rules: Mapping[str, Rule] = field(default_factory=dict)
    paths: tuple[PathRow, ...] = ()

    def measured(self) -> dict[str, Rule]:
        """Return each rule with a limit, by its ID."""
        return {rule_id: rule for rule_id, rule in self.rules.items() if rule.threshold is not None}


def is_rule_id(text: str) -> bool:
    """Whether a text is a rule ID: lowercase words of letters and digits, with a hyphen between two words."""
    if not text or not ("a" <= text[0] <= "z") or text.endswith("-") or "--" in text:
        return False
    return all("a" <= character <= "z" or "0" <= character <= "9" or character == "-" for character in text)


def is_module_name(text: str) -> bool:
    """Whether a text is a dotted module name under assay, such as guard.engines.meta.runner."""
    return bool(text) and all(part.isidentifier() and part.islower() or part == "__init__" for part in text.split("."))


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
        raise row.error(
            f"the rule {rule_id} has {len(row.cells)} cells.  A rule row has two: what the rule "
            f"refuses, and the default action"
        )
    try:
        engine = Engine(values["engine"])
    except ValueError:
        raise row.error(
            f"the engine {values['engine']!r} of {rule_id} is not one of {', '.join(item.value for item in Engine)}"
        ) from None
    try:
        policy = Policy(values["ledger"])
    except ValueError:
        raise row.error(
            f"the ledger policy {values['ledger']!r} of {rule_id} is not one of "
            f"{', '.join(item.value for item in Policy)}"
        ) from None
    scope = tuple(values["scope"].split(","))
    for item in scope:
        if not item or (item not in SCOPE_WORDS and (item.startswith("/") or ".." in item.split("/"))):
            raise row.error(
                f"the scope {item!r} of {rule_id} is not a path relative to the source root or one of "
                f"{', '.join(sorted(SCOPE_WORDS))}"
            )
    given = [name for name in THRESHOLD_FIELDS if name in values]
    threshold = None
    if given and len(given) != len(THRESHOLD_FIELDS):
        raise row.error(f"the rule {rule_id} has {', '.join(given)}, and a measured rule has limit and unit")
    if given:
        limits = rows_grammar.limits(values["limit"], row, "limit")
        if values["unit"] not in UNITS:
            raise row.error(f"the unit {values['unit']!r} of {rule_id} is not one of {', '.join(sorted(UNITS))}")
        threshold = Threshold(limits, values["unit"])
    if not is_doc_reference(values["doc"]):
        raise row.error(
            f"the doc {values['doc']!r} of {rule_id} is not a section of CLAUDE.md, such as XV, XV.4, XXII.R11 or L0"
        )
    legacy = None
    if "legacy" in values:
        if not is_module_name(values["legacy"]):
            raise row.error(
                f"the legacy module {values['legacy']!r} of {rule_id} is not a dotted module name under assay"
            )
        legacy = Legacy(values["legacy"], tuple(values["legacy-args"].split(",")) if "legacy-args" in values else ())
    elif "legacy-args" in values:
        raise row.error(f"the rule {rule_id} has legacy-args and no legacy module")
    held_by = values.get("held-by")
    if held_by is not None and legacy is not None:
        raise row.error(f"the rule {rule_id} has a legacy module and a holder.  A rule has at most one of the two")
    if held_by is not None and threshold is None and held_by not in STEP_METERS:
        raise row.error(
            f"the rule {rule_id} has the holder {held_by} and no limit.  Only a measured rule, or a "
            f"rule that a step meter judges ({', '.join(sorted(STEP_METERS))}), has a holder"
        )
    if held_by is not None and held_by not in HOLDING_COMMANDS and not is_rule_id(held_by):
        raise row.error(
            f"the holder {held_by!r} of {rule_id} is not a rule ID or one of {', '.join(sorted(HOLDING_COMMANDS))}"
        )
    holder = None
    if "holder" in values:
        if held_by is None:
            raise row.error(f"the rule {rule_id} has the field holder and no field held-by.  Only a held rule has it")
        holder = parse_holder(values["holder"])
        if holder is None:
            raise row.error(
                f"the field holder {values['holder']!r} of {rule_id} is not MODULE{HOLDER_SEPARATOR}FUNCTION, with "
                f"a module of the package, such as assay.loop.gauge{HOLDER_SEPARATOR}judged_record"
            )
    kinds = tuple(values["kinds"].split(",")) if "kinds" in values else ()
    inputs = tuple(values["inputs"].split(",")) if "inputs" in values else ()
    return Rule(
        rule_id,
        engine,
        scope,
        policy,
        kinds,
        threshold,
        inputs,
        values["doc"],
        row.cells[0],
        row.cells[1],
        legacy,
        held_by,
        holder,
        row.line,
    )


def parse_holder(text: str) -> Holder | None:
    """Read the field holder: a module of the package, the separator and a function name.

    Returns:
        The module and the function, or None when the text has a different shape
    """
    module, separator, function = text.partition(HOLDER_SEPARATOR)
    is_module = module.startswith(PACKAGE_OWNER) and is_module_name(module.removeprefix(PACKAGE_OWNER))
    if not separator or not is_module or not function.isidentifier() or keyword.iskeyword(function):
        return None
    return Holder(module, function)


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
    paths: list[PathRow] = []
    seen_globs: dict[str, int] = {}
    if not all_rows or all_rows[0].words != ("format", FORMAT) or all_rows[0].cells:
        problems.append(
            rows_grammar.RowsError(
                shown, all_rows[0].line if all_rows else 0, f"the first row of the registry is 'format {FORMAT}'"
            )
        )
    for row in all_rows[1:] if all_rows and all_rows[0].words[0] == "format" else all_rows:
        kind = row.words[0]
        try:
            if kind == "rule":
                rule = parse_rule(row)
                if rule.id in rules:
                    raise row.error(f"the rule {rule.id} has a second row.  The first is at line {rules[rule.id].line}")
                rules[rule.id] = rule
            elif kind == "path":
                path_row = parse_path(row)
                if path_row.glob in seen_globs:
                    raise row.error(
                        f"the glob {path_row.glob} has a second row.  The first is at line {seen_globs[path_row.glob]}"
                    )
                seen_globs[path_row.glob] = row.line
                paths.append(path_row)
            else:
                raise row.error(f"the row kind {kind!r} is not format, rule or path")
        except rows_grammar.RowsError as problem:
            problems.append(problem)
    return Registry(shown, rules, tuple(paths)), problems


# The text, the shown name and the registry of the last load() of this process (load()).
_last_load: tuple[str, str, Registry] | None = None


def load(path: Path = REGISTRY, shown: str = REGISTRY_PATH) -> Registry:
    """Read the registry, and refuse its first problem.

    A load of the same text with the same shown name gives the registry of the last load of the process with no parse.
    The launcher of a test then parses the registry one time for the three measured rules of a test.  In the gate of
    a1df30f04, each of 12,629 processes parsed it three times.

    Raises:
        rows_grammar.RowsError: If a row does not obey the grammar or the schema
        OSError: If the file cannot be read
    """
    global _last_load
    text = path.read_text(encoding="utf-8")
    if _last_load is not None and _last_load[0] == text and _last_load[1] == shown:
        return _last_load[2]
    registry, problems = examine(text, shown)
    if problems:
        raise problems[0]
    _last_load = (text, shown, registry)
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
    start = next(
        (
            index
            for index, line in enumerate(lines)
            if line.startswith("## ") and line[3:].split(" ", 1)[0].rstrip(".") == section
        ),
        None,
    )
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


def measured_rule(
    rule_id: str, limit: float | Mapping[str, float], unit: str, measures: str = "a planted quantity"
) -> str:
    """Return the text of one measured rule row, for a planted registry of a test.  The gauge holds the rule.

    Args:
        rule_id: The ID of the rule
        limit: The one limit of the rule, or the limit of each kind of item
        unit: The unit of the limit
        measures: The first cell of the row

    Raises:
        ValueError: If the row cannot be written
    """

    def number_text(value: float) -> str:
        return str(int(value)) if float(value).is_integer() else repr(float(value))

    field_text = (
        ",".join(f"{kind}:{number_text(value)}" for kind, value in limit.items())
        if isinstance(limit, Mapping)
        else number_text(limit)
    )
    return rows_grammar.render(
        [
            "rule",
            rule_id,
            "engine=build",
            "scope=builddir",
            "ledger=max",
            f"limit={field_text}",
            f"unit={unit}",
            "held-by=loop-gauge",
            "doc=XV",
        ],
        [measures, "lower the value"],
    )


def planted(rows: Iterable[str], comment: str = "# a planted registry\n") -> str:
    """Return the text of a registry with a head comment, the format row and the rows."""
    return comment + f"format {FORMAT}\n" + "".join(f"{row}\n" for row in rows)


def planted_rules(rows: Iterable[str]) -> dict[str, Rule]:
    """Return each rule of a planted registry of the rows (planted()), by its identifier.

    Raises:
        ValueError: If a row of the planted registry does not read
    """
    registry, problems = examine(planted(rows))
    if problems:
        raise ValueError(f"the planted registry does not read: {problems[0]}")
    return dict(registry.rules)
