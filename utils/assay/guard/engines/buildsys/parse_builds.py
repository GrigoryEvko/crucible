"""parse-builds — each configured parse build of the tree is current, and the counted rules judge it.

THE RULE
    PARSE_PRESETS of assay.loop.syntax names the presets of the parse
    builds.  The rule does these steps on the build directory of each preset
    in the presets files of the tree:
      1. A preset that the presets files do not give, or that has no
         binaryDir, gives the error preset.
      2. A build directory with no CMake cache, or with a cache that does
         not set CRUCIBLE_SYNTAX_ONLY, gives no finding.  The rule prints the
         command that configures it.
      3. The rule builds the input targets of the parse build, and it finds
         the units that keep their reports (assay.loop.syntax, THE CHECK).  A
         step that fails gives the error parse.  A unit with no current
         report gives the error stale, and its message gives the command
         `loop syntax BUILD_DIR`, which parses the unit.  The rule parses
         nothing.
      4. The rules of COUNTED_RULES judge the reports of the parse build
         (assay.guard.engines.plugin.runner).  Each rule that refuses the
         parse build, or that does not apply to it, gives the error counted.
    When no preset has a configured parse build, the rule does not apply.
    The rule reads no change and no history of the tree, and it does not
    read the build of --build-dir.  `loop test` runs `loop syntax` on each
    configured parse build after its build (assay.loop.affected, THE PARSE
    BUILDS).  Then the test of the rule finds each report current.

THE PLANTS
    A plant holds the presets file of its tree, and build.txt for a planted
    build (assay.guard.engines.buildsys.planted).  A plant can also hold the
    directory PROGRAMS with one Python script NAME.py for each program of
    its planted build, such as a compiler.  plant_context() makes each
    script the executable program PROGRAMS/NAME.  For such a plant, it then
    runs `loop syntax` on the planted parse build, and each report of the
    build is then current.

Complexity: one read of the presets files.  Each configured parse build adds one build of its input targets, one
stat of each dependency of its units and one read of each report.
"""

import contextlib
import io
from pathlib import Path

from assay.core import configuration as build_config
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import testing
from assay.guard.engines.buildsys import planted
from assay.guard.engines.buildsys.runner import BuildContext
from assay.guard.engines.meta import runner as meta_runner
from assay.loop import syntax

RULE = "parse-builds"
# The rules that judge a parse build (THE RULE, step 4).
COUNTED_RULES = ("quarantine", "heavy-include", "syscall-capability", "door-unit")
# The units with no current report that the message of the error stale names (THE RULE, step 3).
SHOWN_STALE_UNITS = 3
# The directory of the scripts of the programs of a plant, and the suffix of a script (THE PLANTS).
PROGRAMS = "programs"
SCRIPT_SUFFIX = ".py"
# The file of the presets of a tree, which the findings with no line name.
PRESETS_FILE = "CMakePresets.json"


def error(path: str, message: str, kind: str, entity: str) -> check_report.Finding:
    """Return one error of the rule, with no line."""
    return check_report.Finding("error", path, 0, RULE, message, kind, entity)


def judged_build(context: BuildContext, preset: str, build: Path) -> list[check_report.Finding]:
    """Give the findings of steps 3 and 4 of THE RULE on the configured parse build of one preset."""
    shown_build = context.shown(build)
    try:
        plan = syntax.locked_plan(build, context.root, syntax.JOBS)
    except syntax.ParseError as problem:
        message = f"the reports of the parse build {shown_build} cannot be judged: {problem}.  Correct the build"
        return [error(shown_build, message, "parse", preset)]
    if plan.planned:
        shown_units = ", ".join(context.shown(item.unit.object_path) for item in plan.planned[:SHOWN_STALE_UNITS])
        message = (
            f"{len(plan.planned)} of {len(plan.kept) + len(plan.planned)} units of the parse build {shown_build} have "
            f"no current report, such as {shown_units}.  Parse them with `python3 utils/bin/loop syntax "
            f"{shown_build}`, and run the test again.  `python3 utils/bin/loop test` runs that parse after its build"
        )
        return [error(shown_build, message, "stale", preset)]
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
            f"write a move or a raise from that build with `python3 utils/bin/guard write --rule {rule.id} "
            f"--build-dir {shown_build}`"
        )
        found.append(error(shown_build, message, "counted", rule.id))
    return found


def findings(context: BuildContext) -> list[check_report.Finding]:
    """Give the findings of THE RULE on each parse build of the presets of the tree.

    Raises:
        NotApplicable: If no preset of PARSE_PRESETS has a configured parse build, and no preset has an error
    """
    found: list[check_report.Finding] = []
    judged = 0
    for preset in syntax.PARSE_PRESETS:
        try:
            build = syntax.build_directory(context.root, preset)
        except syntax.ParseError as problem:
            message = f"{problem}.  Give the preset {preset} its binaryDir in {PRESETS_FILE}"
            found.append(error(PRESETS_FILE, message, "preset", preset))
            continue
        if not build_config.is_parse_build(build):
            check_report.write_line(
                f"{RULE}: the parse build {context.shown(build)} of the preset {preset} is not configured.  Configure "
                f"it with `cmake --preset {preset}` in {context.root}"
            )
            continue
        judged += 1
        found += judged_build(context, preset, build)
    if not judged and not found:
        raise meta_runner.NotApplicable(f"no preset of {', '.join(syntax.PARSE_PRESETS)} has a configured parse build")
    return found


def plant_context(plant: Path, scratch: Path) -> BuildContext:
    """Make the tree and the planted build of a plant, its programs, and the parse of its planted build (THE PLANTS).

    Raises:
        NotApplicable: If build.txt does not obey its rows, or the parse of the planted build fails
    """
    context = planted.plant_context(plant, scratch)
    programs = scratch / PROGRAMS
    if not programs.is_dir():
        return context
    for script in sorted(programs.glob(f"*{SCRIPT_SUFFIX}")):
        testing.planted_program(programs, script.stem, script.read_text(encoding="utf-8"))
    if context.build_dir is not None:
        with contextlib.redirect_stdout(io.StringIO()) as output, contextlib.redirect_stderr(output):
            status = syntax.run(context.build_dir, scratch, 1)
        if status != 0:
            raise meta_runner.NotApplicable(f"the parse of the planted build fails: {output.getvalue().strip()}")
    return context
