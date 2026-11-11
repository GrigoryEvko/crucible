"""home — a definition or a specialization of an entity stands only in its home.

THE RULE
    C++ has no orphan rule.  Each unit can define or specialize each entity
    that it can name, also an entity of a different owner.  A specialization
    of a trait outside the file that owns the trait changes what a concept
    admits.  A second definition of a door class mints the authority of the
    door.  The quarantine plugin gives a finding for each site of an entity
    outside its home (utils/tools/quarantine/home.cpp).  The rule table holds
    the home row of each entity (assay.core.ruletable, Home), and the class
    of the row selects the sites of the entity and the kind of a finding:
      federation-outside-home  a definition or a specialization of
                               FederationAdmission, or a definition of a part
                               of it, outside fixy/Federation.h.  The key of a
                               federation peer token befriends the admission,
                               so a second definition builds the key with no
                               handshake (the class federation)
      owner-outside-home       a definition of a class of a namespace named
                               host outside the file of its own home row.  A
                               host class owns the key of a context, so a
                               second owner can make each context (the class
                               owner)
      combine-outside-home     the definition of combine_ids outside
                               foundation/reflect/Hash.h (the class combine)
      second-body              a function named combine_ids in a different
                               scope, or a function or a lambda with the salt
                               0x9e3779b97f4a7c15, a shift left by 6, a shift
                               right by 2 and a call of fmix64.  A copy can
                               drift from the one body with no failed check
      derived-name             a declaration with a name that holds
                               combine_ids and more, such as
                               combine_ids_runtime
      away-from-arguments      a specialization of a split trait, such as
                               can_split_into, that is not in the file that
                               defines each of its tags and not in a path of
                               its row (the class split).  A split for tags of
                               a different file gives authority over a region
                               that the file does not own
      outside-home             a specialization of a template of the two
                               layers outside the file that defines it, a
                               specialization of a template of a trait row
                               outside the paths of the row, and a
                               declaration of a fail-closed namespace outside
                               its home (the class trait)
      open-read                a gate of a header of the three layers that
                               reads a template that a unit can specialize
    A line of the kind at-home is no finding: it says that a path of a home
    row admitted a site.  Each problem of the census of the runner is an
    error too.

THE FINDINGS
    Each finding line is one error at its file and its line.  Its kind and
    its entity are the key of a ledger row.  The entity is a name with the
    names of its scopes, as the reader gives it (assay.readers.sections, THE
    ENTITIES OF THE RECORDS).

THE PATHS OF A HOME ROW
    Each path of a home row must admit a site.  The build then gives an
    at-home line for the entity of the row in a file of the path.  The check
    file of a header, test/layer/checks/LAYER/PATH.cpp, stands where
    include/LAYER/PATH.h stands, as the plugin reads it.  The rule does not
    judge a path that holds no file of the build.  A site of such a path can
    be in a different configuration, such as a bench build.  A path that
    admits no site is an error in each build that compiles a file of the
    path.  The row then lists only the paths that hold a site.

THE BUILD
    A file that the build compiles is a source of the compile database, or a
    header in include/.  test/layer gives each header of include/ a sentinel
    unit in each build.  The run does not judge a ledger row of a file that
    the build does not compile, and a write keeps the row.  The run judges a
    row with no file in each build.  A context with no build directory holds
    a planted census, such as the census of a plant of the witness.  The run
    then judges each path of each home row and each ledger row.

THE LEDGER
    utils/ledgers/home.txt counts the findings of each file, kind and entity
    (the policy count).  A row with the value admit holds a reviewed
    exception, such as a second body of combine_ids, with its reason.  A
    count only falls.  No row admits a finding of the kinds
    federation-outside-home and owner-outside-home (REFUSED_KINDS): such a
    finding is an error in each build, and a write refuses it.  The row of an
    open read of a gate of include/crucible/ gives its reason, and a row with
    no reason is an error.  A row with its reason gives no output, as each
    other row of the ledger does.  `guard list --path FILE` prints the rows
    of a file.

Usage
    guard check --rule home --build-dir DIR [--warnings-dir DIR]
    guard write --rule home --build-dir DIR [--raise] [--reason TEXT]

Complexity: linear in the finding lines of the build and in the entries of the compile database.
O(r * l) for r paths of home rows and l at-home lines, and O(k log k) for k rows of the ledger.
"""

import dataclasses
import enum
from collections import Counter
from collections.abc import Sequence
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core import ruletable
from assay.core.paths import REPO_ROOT
from assay.guard.engines.meta import runner as meta_runner
from assay.guard.engines.plugin import runner
from assay.readers import compdb
from assay.readers import sections as quarantine_sections

RULE = "home"


class Kind(enum.StrEnum):
    """The kinds of the finding lines of the rule (THE RULE)."""

    OUTSIDE_HOME = "outside-home"
    FEDERATION_OUTSIDE_HOME = "federation-outside-home"
    OWNER_OUTSIDE_HOME = "owner-outside-home"
    COMBINE_OUTSIDE_HOME = "combine-outside-home"
    SECOND_BODY = "second-body"
    DERIVED_NAME = "derived-name"
    AWAY_FROM_ARGUMENTS = "away-from-arguments"
    OPEN_READ = "open-read"
    AT_HOME = "at-home"


# The kinds that no ledger row admits (THE LEDGER, and THE REFUSED KINDS of assay.guard.engines.meta.runner).
REFUSED_KINDS = frozenset({Kind.FEDERATION_OUTSIDE_HOME.value, Kind.OWNER_OUTSIDE_HOME.value})
# The one body of combine_ids, which each second body must call.
CANONICAL_COMBINE = "foundation::reflect::combine_ids"
HEADER_TREE = "include/"
CHECK_TREE = "test/layer/checks/"
CHECK_SUFFIX = ".cpp"
HEADER_SUFFIX = ".h"
CRUCIBLE_TREE = "include/crucible/"
# The rule table of a tree, relative to its root.
TABLE = ruletable.TABLE.relative_to(REPO_ROOT)


@dataclasses.dataclass(frozen=True, slots=True)
class Build:
    """The source files of the compile database of a build, relative to the root of the tree (THE BUILD).

    `sources` is None for a planted census with no build directory, and the run then judges each path and each row.
    """

    sources: frozenset[str] | None

    def compiles(self, path: str) -> bool:
        """Say if the build compiles a file of PATH: a file in the directory PATH when PATH ends with '/'."""
        if self.sources is None or path.startswith(HEADER_TREE):
            return True
        if path.endswith("/"):
            return any(source.startswith(path) for source in self.sources)
        return path in self.sources

    def judges(self, key: rule_ledger.Key) -> bool:
        """Say if the run judges a ledger row: a row with no file, or a row of a file that the build compiles."""
        return not key.path or self.compiles(key.path)


def read_as(path: str) -> str:
    """Return the path that a home compares: the header of a check file, and the path of each other file."""
    if path.startswith(CHECK_TREE) and path.endswith(CHECK_SUFFIX):
        return HEADER_TREE + path[len(CHECK_TREE) : -len(CHECK_SUFFIX)] + HEADER_SUFFIX
    return path


def build_of(context: runner.PluginContext) -> Build:
    """Return the source files of the build of the context, or a planted build for a context with none (THE BUILD).

    Raises:
        BuildError: If the compile database cannot be read
    """
    if context.build_dir is None:
        return Build(None)
    try:
        rows = compdb.entries(context.build_dir)
    except compdb.CompdbError as problem:
        raise runner.BuildError(f"the compile database cannot be read: {problem}") from None
    root = str(context.root.resolve())
    prefix = f"{root}/"
    return Build(frozenset(row.file[len(prefix) :] for row in rows if row.file.startswith(prefix)))


def homes_of(root: Path) -> tuple[ruletable.Home, ...]:
    """Return the home rows of the rule table of the tree of ROOT.

    Raises:
        ruletable.TableError: If the table does not obey its grammar
        OSError: If the table cannot be read
    """
    return ruletable.load(root / TABLE).homes


def row_of(entity: str, homes: Sequence[ruletable.Home]) -> ruletable.Home | None:
    """Return the home row of ENTITY, or None."""
    return next((home for home in homes if home.entity == entity), None)


def paths_text(home: ruletable.Home | None) -> str:
    """Return the paths of a home row as words for a message."""
    if home is None or not home.paths:
        return "no file"
    return ", ".join(home.paths)


def trait_message(item: quarantine_sections.Finding, homes: Sequence[ruletable.Home]) -> str:
    """Return the text of a site of the class trait outside its home, or of its default home."""
    name = quarantine_sections.qualified_name(item.entity)
    row = row_of(name.simple_name, homes) or row_of(name.text, homes)
    where = (
        f"its home row admits only {paths_text(row)}"
        if row is not None
        else "its home is the file that defines the template"
    )
    return (
        f"a specialization of {item.entity}, or a declaration of a namespace of its home row, stands here, and "
        f"{where}.  A specialization changes what a concept of the two layers admits.  Move the site to its home"
    )


def owner_message(item: quarantine_sections.Finding, homes: Sequence[ruletable.Home]) -> str:
    """Return the text of a host owner outside the file of its home row, or of an owner with no row of its own."""
    row = row_of(item.entity, homes)
    if row is None:
        return (
            f"the class {item.entity} of a namespace named host stands here, and no home row names it.  A class of "
            f"a host namespace owns the key of a context, and a second owner can make each context.  Remove the "
            f"class, or give it a home row in utils/rule-table.txt with its reason"
        )
    return (
        f"a definition or a specialization of {item.entity} stands here, and its home row admits only "
        f"{paths_text(row)}.  A second definition of an owner can make each context.  Remove it"
    )


def message(item: quarantine_sections.Finding, homes: Sequence[ruletable.Home]) -> str:
    """Return the text of one finding line of the rule, for its kind (THE RULE)."""
    match item.kind:
        case Kind.OUTSIDE_HOME:
            return trait_message(item, homes)
        case Kind.OWNER_OUTSIDE_HOME:
            return owner_message(item, homes)
        case Kind.FEDERATION_OUTSIDE_HOME:
            return (
                f"a definition or a specialization of {item.entity}, or a definition of a part of it, stands here, "
                f"and its home row admits only {paths_text(row_of(item.entity, homes))}.  The key of a federation "
                f"peer token befriends the admission, so a second definition builds the key with no handshake.  "
                f"Remove it"
            )
        case Kind.COMBINE_OUTSIDE_HOME:
            return (
                f"the definition of {item.entity} stands here, and its home row admits only "
                f"{paths_text(row_of(item.entity, homes))}.  Define it only there"
            )
        case Kind.SECOND_BODY:
            return (
                f"{item.entity} is a second body of combine_ids: a function named combine_ids, or a body with its "
                f"salt 0x9e3779b97f4a7c15, its shifts by 6 and by 2 and a call of fmix64.  A copy can drift from the "
                f"one body with no failed check.  Call {CANONICAL_COMBINE}"
            )
        case Kind.DERIVED_NAME:
            return (
                f"the name {item.entity} holds combine_ids and more, the name of a copy of combine_ids.  Call "
                f"{CANONICAL_COMBINE}, or give the declaration a name that does not hold combine_ids"
            )
        case Kind.AWAY_FROM_ARGUMENTS:
            return (
                f"a specialization of {item.entity} stands here, and the file does not define each of its tags.  Its "
                f"home row admits only {paths_text(row_of(item.entity, homes))}.  A split for tags of a different "
                f"file gives authority over a region that the file does not own.  Specialize the trait in the file "
                f"that defines its tags"
            )
        case Kind.OPEN_READ:
            return (
                f"the gate reads the open template {item.entity}: a unit can specialize it, and the specialization "
                f"changes what the gate admits.  Read a concept, or a consteval function that is not a template"
            )
        case _:
            return f"a site of {item.entity} stands outside its home.  Move the site to its home"


def site_findings(census: runner.Census, homes: Sequence[ruletable.Home]) -> list[check_report.Finding]:
    """Give one error for each finding line of the rule, with its kind and its entity (THE FINDINGS)."""
    return [
        check_report.Finding(
            "error",
            item.file,
            item.line,
            RULE,
            message(item, homes),
            item.kind,
            quarantine_sections.qualified_name(item.entity).text,
        )
        for item in census.of_rule(RULE)
        if item.kind != Kind.AT_HOME
    ]


def stale_findings(census: runner.Census, homes: Sequence[ruletable.Home], build: Build) -> list[check_report.Finding]:
    """Give one error for each path of a home row that admits no site in a build that compiles a file of the path.

    Complexity: O(r * l) for r paths of the rows and l at-home lines.
    """
    sites: dict[str, list[str]] = {}
    for item in census.of_rule(RULE):
        if item.kind == Kind.AT_HOME:
            sites.setdefault(quarantine_sections.qualified_name(item.entity).text, []).append(read_as(item.file))
    found: list[check_report.Finding] = []
    for home in homes:
        for path in home.paths:
            if not build.compiles(path) or any(ruletable.holds(path, file) for file in sites.get(home.entity, ())):
                continue
            found.append(
                check_report.Finding(
                    "error",
                    str(TABLE),
                    home.line,
                    RULE,
                    f"the home row of {home.entity} admits the path {path}, and no site of {home.entity} stands "
                    f"under it.  Remove the path from the row, so that the row lists only the places of its sites",
                )
            )
    return found


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give the errors of the rule: each problem of the census, each finding line and each path with no site.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    census = context.census()
    try:
        homes = homes_of(context.root)
    except (OSError, ruletable.TableError) as problem:
        table_error = check_report.Finding("error", str(TABLE), getattr(problem, "line", 0), RULE, str(problem))
        return [*census.errors(RULE), table_error]
    return [
        *census.errors(RULE),
        *site_findings(census, homes),
        *stale_findings(census, homes, build_of(context)),
    ]


def is_crucible_gate(path: str) -> bool:
    """Say if a gate file is a header of include/crucible/ or the check file of one."""
    return read_as(path).startswith(CRUCIBLE_TREE)


def crucible_reads(
    rule: rule_registry.Rule, root: Path, found: Sequence[check_report.Finding]
) -> list[check_report.Finding]:
    """Give an error for each open read of a crucible gate whose row gives no reason (THE LEDGER).

    A row with its reason gives no finding.  Complexity: linear in the findings, with one read of the ledger.
    """
    try:
        ledger = rule_ledger.read(rule, root)
    except rows_grammar.RowsError, OSError:
        return []
    result: list[check_report.Finding] = []
    for item in found:
        if item.kind != Kind.OPEN_READ or not is_crucible_gate(item.path):
            continue
        entry = ledger.base.get(rule_ledger.key_of(item))
        if entry is None or entry.reason:
            continue
        result.append(
            check_report.Finding(
                "error",
                rule_ledger.shown_path(RULE),
                entry.line,
                RULE,
                f"a gate of {CRUCIBLE_TREE} reads the open template {item.entity} at {item.path}:{item.line}, "
                f"and its row gives no reason.  Give the reason in the REASON cell of its row",
            )
        )
    return result


def judge(rule: rule_registry.Rule, context: runner.PluginContext) -> list[check_report.Finding]:
    """Return the findings of the rule judged against its ledger, with the rows of the files that the build compiles.

    A finding with no kind, such as a problem of the census or a path with no site, stays an error.  A finding of a
    kind of REFUSED_KINDS stays an error too.  The reasons of the crucible gates come last (THE LEDGER).

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    raw = context.findings_of(rule)
    keyed = [item for item in raw if item.kind]
    rest = [item for item in raw if not item.kind]
    return [
        *rest,
        *meta_runner.judged(rule, context.root, keyed, build_of(context).judges),
        *crucible_reads(rule, context.root, raw),
    ]


def run(rule: rule_registry.Rule, context: runner.PluginContext, warnings_dir: Path | None) -> int:
    """Run the rule on the build of the context, judge it (judge()) and write its findings.

    Returns:
        0 with no error, 1 with an error, SKIPPED when the rule does not apply
    """
    census = runner.census_or_status(rule, context, warnings_dir)
    if isinstance(census, int):
        return census
    try:
        found = judge(rule, context)
    except runner.NotApplicable as reason:
        check_report.write_line(f"{rule.id}: {reason}.  The rule does not apply")
        return runner.SKIPPED
    except runner.BuildError as problem:
        found = [check_report.Finding("error", context.build_shown(), 0, rule.id, str(problem))]
    status = check_report.emit(found, rule.id, warnings_dir)
    errors = sum(item.level == "error" for item in found)
    check_report.write_line(f"{rule.id}: {errors} error(s), {len(found) - errors} warning(s)")
    return status


def build_counts(rule: rule_registry.Rule, context: runner.PluginContext) -> Counter[rule_ledger.Key]:
    """Return the count of each key of the findings of the build of the context, for a write.

    The counts of one build are partial, and a write from one build keeps each row that the build does not judge
    (THE WRITE of assay.core.ledger).

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read, or it has a problem or a finding of a kind that no row admits
    """
    found = context.findings_of(rule)
    problems = [item for item in found if item.level == "error" and not item.kind]
    if problems:
        raise runner.BuildError(
            f"the build has {len(problems)} problems, such as {problems[0].path}: {problems[0].message}"
        )
    refused = [item for item in found if item.level == "error" and item.kind in REFUSED_KINDS]
    if refused:
        raise runner.BuildError(
            f"the build has {len(refused)} findings of a kind that no row admits, such as {refused[0].path}:"
            f"{refused[0].line}: {refused[0].message}.  Correct them first"
        )
    return Counter(rule_ledger.key_of(item) for item in found if item.level == "error")


def write(rule: rule_registry.Rule, request: runner.WriteRequest) -> int:
    """Write the ledger of the rule from the findings of the builds of the request.

    Returns:
        0 when the ledger is written.  1 when a build has a problem or a finding of a refused kind, a count rose in
        the mode lower or an input cannot be read.  SKIPPED when the rule does not apply to a build
    """
    try:
        ledger = rule_ledger.read(rule, request.root)
    except (rows_grammar.RowsError, OSError) as problem:
        check_report.write_line(f"guard write: the ledger of {rule.id} is not written: {problem}", is_error=True)
        return 1
    per_build = runner.counts_of_builds(rule.id, request, lambda context: build_counts(rule, context))
    if isinstance(per_build, int):
        return per_build
    try:
        written = rule_ledger.write_counts(
            rule,
            ledger,
            rule_ledger.largest_counts(per_build),
            request.mode,
            request.reason,
            request.is_partial,
        )
    except rule_ledger.LedgerError as problem:
        check_report.write_line(f"guard write: the ledger of {rule.id} is not written: {problem}", is_error=True)
        return 1
    rule_ledger.store(written, request.root)
    check_report.write_line(f"guard write: {rule_ledger.shown_path(rule.id)} holds {len(written.base)} rows")
    return 0
