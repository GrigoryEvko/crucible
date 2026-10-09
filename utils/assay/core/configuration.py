"""configuration — the configuration of a build, and the one policy of the rows that judge a build and that a write writes.

THE CONFIGURATION
    A configuration is the build kind and the target tier of
    assay.core.target, and each cache variable of CONFIGURATION.  A target
    can add a native flag to its own compile, as each bench target adds
    -march=native.  The tier then also holds the native tier of the host.  A
    different configuration compiles other objects or other preprocessor
    arms.  Its findings, its symbols and its totals can then be different.

    The name of a configuration is its build kind, for example
    x86_64-debug-asan.  The name adds -bench when CRUCIBLE_BENCH is on, -fuzz
    when CRUCIBLE_FUZZ is on and -syntax when CRUCIBLE_SYNTAX_ONLY is on.  A build with
    CRUCIBLE_SYNTAX_ONLY only parses its units (is_parse_build(), `loop
    syntax`).  CRUCIBLE_SENSE_HUB_V2 is no key.  Each build compiles
    the SenseHub v2 sources in two object libraries, with and without
    CRUCIBLE_SENSE_HUB_EXTENDED, and that option then changes no finding line.
    A rule can read fewer facts.  The rule artifact-symbols reads only the
    build kind and the tier, and the build kind is then the name.

THE POLICY
    Each rule with rows that change with the configuration judges each build,
    and no such rule skips a build because of its configuration.  These rows
    judge a build (judging()):
      * the rows of the configuration with the name of the build, when the
        ledger holds one.  A configuration row can give only some facts: the
        rows of the rule syscall-capability give only the build kind.  Each
        fact of the row that the build does not share gives one line, because
        the build can then compile other preprocessor arms
      * the largest count of each key over the configurations, for a build
        of a name that the ledger does not hold (assay.core.ledger, THE
        MAXIMUM).  An arm that a different configuration counts then gives
        no error.  An example is a SIMD arm of a CI runner of a different
        native tier.  A count below its row is no error, because the build
        can compile fewer arms
      * the base rows, for a ledger with no configuration.
    The summary line of a run names the rows.  A build of a new kind then
    fails on each new finding of an arm that only it compiles.  No finding
    with no row gets through.  The plugin rules
    quarantine, heavy-include and syscall-capability, the elf rule
    artifact-symbols and the census totals of the engine build use this
    policy.

    A write from a different build puts the counts of other arms into the
    rows.  The write of a build is then possible only in two conditions
    (write_target()):
      * the ledger holds the configuration of the build, with each fact of
        its row
      * the ledger holds no configuration.
    `guard write --add-configuration` adds the configuration of the build
    with each of its facts.  It refuses a name that the ledger holds with
    each fact of the build, and the base name with other facts
    (add_refusal()).

THE ROWS FOR AN IMPORT
    A rule that counts in each configuration writes the rows of the
    configuration of a build other than the base, each with the prefix
    "ledger-row: " (print_rows(), assay.core.ledger.rows_of_counts).  It
    writes them after a failure, and each time when the ledger does not hold
    the configuration.  `guard write --rule RULE --import LOG` then puts the
    rows of the log into the ledger (import_rows()).  Only CI builds aarch64.
    The rows of its configuration then come from the log of its leg.

`guard selftest tests.core.test_configuration` does a test of the module.
"""

import dataclasses
from collections.abc import Callable, Iterable, Mapping, Sequence
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core import target as build_target
from assay.readers import cmake

KIND_KEY = "kind"
TIER_KEY = "tier"
# The cache variables that change the objects of all or their preprocessor arms.
CONFIGURATION = (
    "CMAKE_BUILD_TYPE",
    "CMAKE_CXX_FLAGS",
    "CRUCIBLE_SANITIZE",
    "CRUCIBLE_TSAN",
    "CRUCIBLE_UBSAN_STRICT",
    "CRUCIBLE_ANALYZER",
    "CRUCIBLE_VERIFY",
    "CRUCIBLE_PGO",
    "CRUCIBLE_BENCH",
    "CRUCIBLE_FUZZ",
    "CRUCIBLE_EXAMPLES",
    "CRUCIBLE_HAVE_BPF",
    "CRUCIBLE_SENSE_HUB_EXTENDED",
    "CRUCIBLE_VALGRIND_INCLUDE",
    "TORCH_DIR",
    "CRUCIBLE_SYNTAX_ONLY",
)
# The cache option of a build that only parses its units (is_parse_build()).
SYNTAX_OPTION = "CRUCIBLE_SYNTAX_ONLY"
# The options that add a word to the name of a configuration.
NAME_OPTIONS = (("bench", "CRUCIBLE_BENCH"), ("fuzz", "CRUCIBLE_FUZZ"), ("syntax", SYNTAX_OPTION))
CMAKE_FALSE = frozenset({"", "0", "OFF", "NO", "FALSE", "N", "IGNORE", "NOTFOUND"})
BASE_WORDS = "the base rows"


class ConfigurationError(Exception):
    """The configuration of a build cannot be read: its build kind, its CMake cache or its target."""


@dataclasses.dataclass(frozen=True, slots=True)
class BuildConfiguration:
    """The name of the configuration of a build and its facts, as KEY VALUE pairs (THE CONFIGURATION)."""

    name: str
    facts: tuple[tuple[str, str], ...]

    @property
    def tier(self) -> str:
        """The target tier of the build."""
        return dict(self.facts).get(TIER_KEY, "?")


@dataclasses.dataclass(frozen=True, slots=True)
class Judging:
    """The rows that judge a build (THE POLICY).

    `name` is the configuration of the rows, assay.core.ledger.MAXIMUM for the largest count of each key, or None for
    the base rows.  `words` name the rows for a summary line.  `note` is the line of the facts that the build does not
    share with the configuration of the rows, or "".
    """

    name: str | None
    words: str
    note: str


@dataclasses.dataclass(frozen=True, slots=True)
class WriteTarget:
    """Where a write puts the rows of a build (THE POLICY).

    `ledger` is the ledger with the configuration of the build when the write adds it.  `name` is the configuration
    of the rows, or None for the base rows of a ledger with no configuration.  `is_added` is True when the write adds
    the configuration, and each of its rows is then new.
    """

    ledger: rule_ledger.Ledger
    name: str | None
    is_added: bool


# ── The configuration of a build ───────────────────────────────────


def normalized(kind: str, value: str) -> str:
    """Return a cache value as one word: on or off for a BOOL, set or unset for a path, else the value or unset."""
    is_false = value.upper() in CMAKE_FALSE or value.upper().endswith("-NOTFOUND")
    if kind == "BOOL":
        return "off" if is_false else "on"
    if kind in ("PATH", "FILEPATH"):
        return "unset" if is_false else "set"
    return value if value.strip() else "unset"


def is_parse_build(build_dir: Path) -> bool:
    """Return True when the CMake cache of the build sets SYNTAX_OPTION: the build then only parses its units.

    A cache that cannot be read gives False, and the caller then reads the objects of the build.
    """
    try:
        entry = cmake.cache(build_dir).get(SYNTAX_OPTION)
    except cmake.CmakeError:
        return False
    return entry is not None and normalized("BOOL", entry.value) == "on"


def read_configuration(build_dir: Path, commands: Iterable[str]) -> BuildConfiguration:
    """Return the configuration of a build: the build kind, the tier and each name of CONFIGURATION.

    The tier holds the native tier of each native flag of the compile commands of the objects.  A bench target with
    its own -march=native then gives the configuration the tier of the host.  Complexity: linear in the size of the
    commands, and one compiler run for each different tier.

    Raises:
        ConfigurationError: If the build has no build-kind.txt, its CMake cache cannot be read, or the compiler does
            not print its target
    """
    kind = build_target.kind_of(build_dir)
    if kind is None:
        raise ConfigurationError(f"{build_dir} has no build-kind.txt, which cmake/BuildLauncher.cmake writes")
    try:
        entries = cmake.cache(build_dir)
    except cmake.CmakeError as problem:
        raise ConfigurationError(f"the CMake cache of {build_dir} cannot be read: {problem}") from None
    compiler = entries.get("CMAKE_CXX_COMPILER")
    try:
        tier = build_target.build_tier(build_dir, compiler.value if compiler is not None else "", commands)
    except build_target.TierError as problem:
        raise ConfigurationError(str(problem)) from None
    facts = [(KIND_KEY, kind), (TIER_KEY, tier)]
    for name in CONFIGURATION:
        entry = entries.get(name)
        value = normalized(entry.type_name, entry.value) if entry is not None else normalized("STRING", "")
        facts.append((name, value.replace(" ", "_")))
    words = [word for word, key in NAME_OPTIONS if dict(facts)[key] == "on"]
    return BuildConfiguration("-".join([kind, *words]), tuple(facts))


def facts_of(ledger: rule_ledger.Ledger, name: str) -> tuple[tuple[str, str], ...] | None:
    """Return the facts of a configuration of the ledger, or None when the ledger does not hold it."""
    item = ledger.configuration(name)
    return item.facts if item is not None else None


def is_held(ledger: rule_ledger.Ledger, configuration: BuildConfiguration) -> bool:
    """Return True when the ledger holds the configuration of the build: its name, and each fact that its row gives.

    A row can give only some facts, as the rows of the rule syscall-capability give only the build kind.  A fact
    that the build has and the row does not give changes nothing.
    """
    facts = facts_of(ledger, configuration.name)
    if facts is None:
        return False
    built = dict(configuration.facts)
    return all(built.get(name) == value for name, value in facts)


def configuration_difference(build: Sequence[tuple[str, str]], ledger: Sequence[tuple[str, str]]) -> list[str]:
    """Return a text for each fact of the row in the ledger with a value in the build that is not its value there."""
    built, held = dict(build), dict(ledger)
    return [
        f"{name} is {built.get(name, 'absent')} in the build and {held[name]} in the ledger"
        for name in sorted(held)
        if built.get(name) != held[name]
    ]


def held_line(rule_id: str, ledger: rule_ledger.Ledger, configuration: BuildConfiguration, consequence: str) -> str:
    """Return the one line of a build with a configuration that the ledger does not hold (target.not_held).

    `consequence` tells what the rule does with the build.
    """
    held = [(item.name, dict(item.facts).get(TIER_KEY, "?")) for item in ledger.configurations]
    facts = facts_of(ledger, configuration.name)
    detail = ""
    if facts is not None:
        detail = (
            f"Its configuration {configuration.name} differs: "
            f"{'; '.join(configuration_difference(configuration.facts, facts))}"
        )
    return build_target.not_held(rule_id, configuration.name, configuration.tier, held, consequence, detail)


# ── The policy ─────────────────────────────────────────────────────


def judging(rule_id: str, ledger: rule_ledger.Ledger, configuration: BuildConfiguration) -> Judging:
    """Return the rows that judge the build of a configuration (THE POLICY).  Complexity: linear in the facts."""
    facts = facts_of(ledger, configuration.name)
    if facts is None:
        if not ledger.configurations:
            return Judging(None, BASE_WORDS, "")
        names = ", ".join(item.name for item in ledger.configurations)
        words = (
            f"the largest count of each key over the configurations {names}, because the ledger holds no "
            f"configuration {configuration.name}"
        )
        return Judging(rule_ledger.MAXIMUM, words, "")
    note = ""
    if not is_held(ledger, configuration):
        note = held_line(
            rule_id,
            ledger,
            configuration,
            f"The rows of the configuration {configuration.name} judge the build, and the build can compile other "
            f"preprocessor arms",
        )
    return Judging(configuration.name, f"the rows of the configuration {configuration.name}", note)


def add_refusal(ledger: rule_ledger.Ledger, held: str | None, name: str) -> str:
    """Return the reason that --add-configuration cannot write the configuration of the build, or an empty text.

    `held` is the name of the configuration when the ledger holds it with each fact of the build, else None.
    """
    if not rule_ledger.rows_grammar_name(name):
        return f"the name {name!r} of the configuration is not a lowercase word"
    if held is not None:
        return f"the ledger holds the configuration {name} of the build.  Write its rows with no --add-configuration"
    if name == ledger.base_name:
        return (
            f"the build has other keys than the base configuration {name}.  A change of the base keys makes the "
            f"ledger again: keep its head, remove its rows, then write each configuration with "
            f"--add-configuration in its build, the base first"
        )
    return ""


def write_target(
    rule_id: str, ledger: rule_ledger.Ledger, configuration: BuildConfiguration, add_configuration: bool
) -> WriteTarget | str:
    """Return where a write puts the rows of a build, or the line that tells why the write writes nothing (THE POLICY).

    A write is possible when the ledger holds the configuration of the build with each fact of its row, or no
    configuration.  --add-configuration adds the configuration of the build with each of its facts, after the other
    configurations.  It also removes the earlier difference rows of the name.
    """
    name = configuration.name
    held = is_held(ledger, configuration)
    if add_configuration:
        refusal = add_refusal(ledger, name if held else None, name)
        if refusal:
            return f"{rule_id}: the ledger is not written: {refusal}"
        others = tuple(item for item in ledger.configurations if item.name != name)
        added = dataclasses.replace(
            ledger,
            configurations=(*others, rule_ledger.Configuration(name, configuration.facts)),
            deltas={key: table for key, table in ledger.deltas.items() if key != name},
        )
        return WriteTarget(added, name, True)
    if held:
        return WriteTarget(ledger, name, False)
    if not ledger.configurations:
        return WriteTarget(ledger, None, False)
    return held_line(
        rule_id,
        ledger,
        configuration,
        "A write from that build puts the counts of other preprocessor arms into the rows, so the write writes "
        "nothing.  Give --add-configuration to write the configuration of the build",
    )


# ── The rows for an import ─────────────────────────────────────────


def print_rows(
    rule_id: str,
    ledger: rule_ledger.Ledger,
    configuration: BuildConfiguration,
    counts: Mapping[rule_ledger.Key, int],
    has_problems: bool,
) -> None:
    """Write the rows of the configuration of the build, as --import reads them from a log (THE ROWS FOR AN IMPORT).

    The base has no such rows, because a write writes it, and a build with a problem has no rows to write.  Each
    row has the prefix assay.core.ledger.ROW_MARK.  Complexity: O(k log k) for k rows and counts.
    """
    if configuration.name == ledger.base_name or ledger.base_name is None or has_problems:
        return
    for line in rule_ledger.rows_of_counts(ledger, configuration.name, list(configuration.facts), counts):
        check_report.write_line(line)
    check_report.write_line(
        f"{rule_id}: the lines above give the rows of the configuration {configuration.name}.  "
        f"Import them from the log with `python3 utils/bin/guard write --rule {rule_id} --import "
        f"LOG`"
    )


def import_rows(
    rule: rule_registry.Rule,
    root: Path,
    source: Path,
    refusal: Callable[[rule_ledger.Ledger], str] | None = None,
) -> int:
    """Put the rows of one configuration that print_rows wrote into the ledger.  They replace its earlier rows.

    `refusal` gives the first problem of the ledger with the rows of the log, or an empty text.  A rule with a shape
    of its own for its rows gives it, and the import then writes no ledger that the run of the rule refuses.

    Returns:
        0 when the ledger is written, 1 when the rows or the ledger do not have the format
    """
    try:
        ledger = rule_ledger.import_rows(
            rule_ledger.read(rule, root), source.read_text(encoding="utf-8", errors="replace")
        )
    except (OSError, rows_grammar.RowsError, rule_ledger.LedgerError) as problem:
        check_report.write_line(f"{rule.id}: the ledger is not written: {problem}", is_error=True)
        return 1
    problem_text = refusal(ledger) if refusal is not None else ""
    if problem_text:
        check_report.write_line(f"{rule.id}: the ledger is not written: {problem_text}", is_error=True)
        return 1
    rule_ledger.store(ledger, root)
    name = ledger.configurations[-1].name
    check_report.write_line(
        f"{rule.id}: imported the configuration {name}: {len(ledger.configurations[-1].facts)} keys "
        f"and {len(ledger.deltas.get(name, {}))} difference rows, to "
        f"{rule_ledger.ledger_path(rule.id, root)}"
    )
    return 0
