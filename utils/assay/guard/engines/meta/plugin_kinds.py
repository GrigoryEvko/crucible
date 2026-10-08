"""plugin-kinds — the kinds that the quarantine plugin gives are the kinds that the rows of the engine plugin name.

THE RULE
    The quarantine plugin writes the kinds of each of its rules to
    BUILD/quarantine/kinds.txt, one row `kind RULE KIND` for each kind (the
    argument kinds of utils/tools/quarantine/plugin.cpp, which
    utils/tools/quarantine/Quarantine.cmake gives at each build of the
    plugin).  Each rule of the engine plugin of utils/registry.txt names its
    kinds in the field kinds.  The rule refuses:
      unregistered  a kind that the plugin gives, and that the row of its rule
                    does not name.  A kind of a rule that the registry does not
                    hold, or that is not of the engine plugin, is one too
      missing       a kind that a row of the engine plugin names, and that the
                    plugin does not give
      malformed     kinds.txt that does not obey the rows grammar, or a row of
                    it that is not `kind RULE KIND`
    A new kind of the plugin then goes into the registry in the same change.
    The ledger and the runner of its rule then know it.

NOT APPLICABLE
    The rule does not apply with no build directory, or with a build directory
    that holds no kinds.txt.  A plant holds the build directory
    `planted-build` with its kinds.txt (plant_context()).

Complexity: linear in the rows of kinds.txt and of the registry.
"""

from pathlib import Path

from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.guard.engines.meta.runner import MetaContext, NotApplicable, copy_plant

RULE = "plugin-kinds"
# The file of the kinds in the build directory.
KINDS = Path("quarantine") / "kinds.txt"
# The name of the build directory of a plant.  Git ignores each directory with the name build.
PLANT_BUILD = "planted-build"
REBUILD = "Build the plugin again"


def malformed(shown: str, line: int, message: str, entity: str) -> check_report.Finding:
    """Return the finding of a kinds.txt that is not a list of rows `kind RULE KIND`."""
    return check_report.Finding("error", shown, line, RULE, f"{message}.  {REBUILD}", "malformed", entity)


def given_kinds(path: Path, shown: str) -> tuple[set[tuple[str, str]], list[check_report.Finding]]:
    """Return each (rule, kind) of kinds.txt, and a finding for each row that is not `kind RULE KIND`."""
    try:
        rows = rows_grammar.read(path, shown)
    except rows_grammar.RowsError as problem:
        return set(), [malformed(shown, problem.line, str(problem), shown)]
    given: set[tuple[str, str]] = set()
    found: list[check_report.Finding] = []
    for row in rows:
        if row.words[0] != "kind" or len(row.words) != 3 or row.cells:
            found.append(malformed(shown, row.line, "the row is not 'kind RULE KIND'", " ".join(row.words)))
            continue
        given.add((row.words[1], row.words[2]))
    return given, found


def unregistered(rule_id: str, kind: str, rules: dict[str, rule_registry.Rule]) -> check_report.Finding:
    """Return the finding of a kind that the plugin gives, and that no row of the engine plugin names."""
    rule = rules.get(rule_id)
    if rule is not None and rule.engine == rule_registry.Engine.PLUGIN:
        reason = f"the row of the rule {rule_id} does not name it"
    else:
        reason = f"the registry holds no row of the engine plugin for the rule {rule_id}"
    message = (
        f"the plugin gives the kind {kind} of the rule {rule_id}, and {reason}.  "
        f"Add the kind to the field kinds of the row of its rule"
    )
    line = rule.line if rule is not None else 0
    return check_report.Finding(
        "error", rule_registry.REGISTRY_PATH, line, RULE, message, "unregistered", f"{rule_id} {kind}"
    )


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Compare the kinds that the plugin of the build gives with the kinds of the rows of the engine plugin.

    Raises:
        NotApplicable: If the call gives no build directory, or the build directory holds no kinds.txt
    """
    if context.build_dir is None:
        raise NotApplicable("the rule reads the kinds of the plugin of a build: give --build-dir")
    path = context.build_dir / KINDS
    if not path.is_file():
        raise NotApplicable(f"{path} does not exist: configure the build, which builds the plugin and writes it")
    given, found = given_kinds(path, KINDS.as_posix())
    rules = dict(context.registry.rules)
    plugin_rules = [rule for rule in rules.values() if rule.engine == rule_registry.Engine.PLUGIN]
    named = {(rule.id, kind) for rule in plugin_rules for kind in rule.kinds}
    found += [unregistered(rule_id, kind, rules) for rule_id, kind in sorted(given - named)]
    for rule_id, kind in sorted(named - given):
        message = (
            f"the row of the rule {rule_id} names the kind {kind}, and the plugin does not give it.  "
            f"Remove the kind from the row, or give it in the plugin"
        )
        line = rules[rule_id].line
        found.append(
            check_report.Finding(
                "error", rule_registry.REGISTRY_PATH, line, RULE, message, "missing", f"{rule_id} {kind}"
            )
        )
    return found


def plant_context(plant: Path, scratch: Path) -> MetaContext:
    """Copy a plant to a throwaway repository, and give its context with the build directory of the plant."""
    copy_plant(plant, scratch)
    build_dir = scratch / PLANT_BUILD
    return MetaContext(scratch, build_dir if build_dir.is_dir() else None)
