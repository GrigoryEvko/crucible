"""parse-x86-64-v3 and parse-aarch64 — a change of an arm runs its parse build, and the counted rules judge the parse.

THE RULES
    Each rule has one preset of PARSE_PRESETS of assay.loop.syntax (RULES).
    The change of the tree can hold an arm (assay.loop.syntax, THE CHANGE OF
    AN ARM).  Then the rule does these steps on the build directory of its
    preset:
      1. A build directory with no CMake cache, or with a cache that does
         not set CRUCIBLE_SYNTAX_ONLY, gives the error not-configured.  Its
         message gives the command that configures the build directory.
      2. `loop syntax` parses each unit of the build (assay.loop.syntax.run).
         A unit that does not parse gives the error parse.
      3. The rules quarantine, heavy-include and syscall-capability judge
         the reports of the parse (assay.guard.engines.plugin.runner).  Each
         rule that refuses the parse build, or that does not apply to it,
         gives the error counted.
    A change with no arm gives no finding, and the rule reads no parse
    build.  The rule reads the change of the tree, and not the build of
    --build-dir.  Each parse runs with JOBS of assay.loop.syntax, the
    PROCESSORS of the test of the rule (test/CMakeLists.txt).

THE PLANTS
    A plant holds the directory parent/ and the directory tree/.
    plant_context() makes a repository with one commit of parent/.  Then it
    writes each file of tree/ over the working tree, as the change.  No plant
    holds a parse build.  A change of an arm then gives not-configured, and a
    change with no arm gives no finding.

Complexity: one read of the change and one parse tree of each changed C++ file.  A change of an arm adds one
parse of the build of the preset.
"""

import shutil
from pathlib import Path

from assay.core import configuration as build_config
from assay.core import git
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.guard.engines.buildsys.runner import BuildContext
from assay.guard.engines.meta import runner as meta_runner
from assay.loop import syntax

# The directories of a plant (THE PLANTS).
PARENT = "parent"
TREE = "tree"
# The rules that judge a parse build (THE RULES, step 3).
COUNTED_RULES = ("quarantine", "heavy-include", "syscall-capability")


def error(rule_id: str, path: str, message: str, kind: str, entity: str) -> check_report.Finding:
    """Return one error of a rule of the family, with no line."""
    return check_report.Finding("error", path, 0, rule_id, message, kind, entity)


def judged_parse(context: BuildContext, rule_id: str, preset: str) -> list[check_report.Finding]:
    """Give the findings of one rule of the family: the steps of THE RULES on the parse build of its preset."""
    try:
        arms = syntax.arm_files(context.root)
    except syntax.ParseError as problem:
        message = f"{problem}.  The rule cannot tell if the change makes the parse build of {preset} necessary"
        return [error(rule_id, ".", message, "change", preset)]
    if not arms:
        check_report.write_line(
            f"{rule_id}: the change holds no preprocessor arm on an architecture or ISA macro.  "
            f"The parse build of {preset} is not necessary"
        )
        return []
    first = next(iter(arms))
    shown = "; ".join(f"{path} ({', '.join(sorted(macros))})" for path, macros in arms.items())
    check_report.write_line(
        f"{rule_id}: the change holds the arms of {shown}.  The parse build of {preset} is necessary"
    )
    try:
        build = syntax.build_directory(context.root, preset)
    except syntax.ParseError as problem:
        message = f"{problem}.  Give the preset its binaryDir in CMakePresets.json"
        return [error(rule_id, first, message, "not-configured", preset)]
    shown_build = context.shown(build)
    if not build_config.is_parse_build(build):
        message = (
            f"the change holds a preprocessor arm on an architecture or ISA macro, and the parse build {shown_build} "
            f"of the preset {preset} is not configured.  Configure it with `cmake --preset {preset}` in "
            f"{context.root}, and run the test again"
        )
        return [error(rule_id, first, message, "not-configured", preset)]
    if syntax.run(build, context.root, syntax.JOBS) != 0:
        message = (
            f"a unit of the parse build {shown_build} does not parse, or a step of `loop syntax` fails (the lines "
            f"above).  Correct the change"
        )
        return [error(rule_id, first, message, "parse", preset)]
    from assay.guard.engines.plugin import runner as plugin_runner

    registry = context.registry if context.registry is not None else rule_registry.load()
    counted = [registry.rules[rule] for rule in COUNTED_RULES]
    statuses = plugin_runner.run(counted, context.root, build, None)
    found: list[check_report.Finding] = []
    for rule, status in zip(counted, statuses, strict=True):
        if status == 0:
            continue
        verdict = "refuses" if status == 1 else "does not apply to"
        message = (
            f"the rule {rule.id} {verdict} the parse build {shown_build} (the lines above).  Correct the change, or "
            f"write the rows of the configuration of the build with `python3 utils/bin/guard write --rule {rule.id} "
            f"--build-dir {shown_build}`"
        )
        found.append(error(rule_id, first, message, "counted", rule.id))
    return found


def x86_64_v3_findings(context: BuildContext) -> list[check_report.Finding]:
    """Give the findings of the rule parse-x86-64-v3 (THE RULES)."""
    return judged_parse(context, "parse-x86-64-v3", "syntax-x86-64-v3")


def aarch64_findings(context: BuildContext) -> list[check_report.Finding]:
    """Give the findings of the rule parse-aarch64 (THE RULES)."""
    return judged_parse(context, "parse-aarch64", "syntax-aarch64")


def plant_context(plant: Path, scratch: Path) -> BuildContext:
    """Make the repository of a plant: one commit of parent/, and the files of tree/ as the change (THE PLANTS).

    Raises:
        NotApplicable: If the plant has no parent/, or git fails
    """
    if not (plant / PARENT).is_dir():
        raise meta_runner.NotApplicable(f"the plant {plant.name} has no directory {PARENT}/")
    try:
        meta_runner.copy_plant(plant / PARENT, scratch)
        git.commit_all(scratch, "The parent of the change")
        if (plant / TREE).is_dir():
            shutil.copytree(plant / TREE, scratch, dirs_exist_ok=True)
            for planted in sorted(scratch.rglob(f"*{meta_runner.PLANTED_SUFFIX}")):
                planted.replace(planted.with_name(planted.name.removesuffix(meta_runner.PLANTED_SUFFIX)))
    except (git.GitError, git.NotIsolated) as problem:
        raise meta_runner.NotApplicable(f"the repository of the plant cannot be made: {problem}") from None
    return BuildContext(scratch, None)


RULES = {
    "parse-x86-64-v3": meta_runner.Member(x86_64_v3_findings),
    "parse-aarch64": meta_runner.Member(aarch64_findings),
}
