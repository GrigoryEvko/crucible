"""hw-axis-rows — each hardware-axis block has a pin row, and each pin row has a live block.

THE BLOCKS
    A hardware-axis block is a namespace named SITE_hw that states a
    hardware construct as fixy atoms: a SIMD ISA, a cache instruction or a
    memory fence that the preprocessor selects.  Each atom is a type alias in
    the block.

THE TWO HALVES
    The compiler half is test/test_hardware_axis_pins.cpp.  It holds one row
    for each block, `static_assert(hw_axis_pins::pinned<^^BLOCK, AXIS...>);`,
    and test/fixy/hw_axis_pins.h walks the block by reflection when the
    compiler evaluates the row.  The row does not compile when an axis of the
    row has no atom alias in the block, when an atom alias engages an axis
    that the row does not list, or when the build has no such block.

    This rule is the other half.  It reads the parse trees and refuses:
      missing-rows   no rows file, or a rows file with no row
      missing-block  a row whose block no file of include/ or src/ defines
      dead-block     a block whose each definition is in a preprocessor
                     conditional: the compiler sees only the arm that one build
                     selects, so the block is dead in the other builds
      relative-row   a row that names its block with no first `::`, because
                     the rule compares the name as the source spells it, and
                     the compiler resolves a relative name
      unlisted-site  a SITE_hw namespace of include/ or src/ with no row
    A comment and a string hold no node, so a block name or a row in one
    counts for nothing.  The entity of a finding is the name of the block.

THE SCOPE
    The C++ files of include/ and src/, and the rows file.

Complexity: one load of the tree of each file of the scope.
"""

from assay.core import report as check_report
from assay.guard.engines.tsast.runner import TsastContext
from assay.readers import tsast

RULE = "hw-axis-rows"
ROWS_FILE = "test/test_hardware_axis_pins.cpp"
ROW_TEMPLATE = ("hw_axis_pins", "pinned")
BLOCK_SUFFIX = "_hw"
PREPROC_ARMS = ("preproc_if", "preproc_ifdef", "preproc_else", "preproc_elif", "preproc_elifdef")
OPERAND_NAMES = ("qualified_identifier", "namespace_identifier", "type_identifier", "identifier")


def row_of(assertion: tsast.Node) -> tuple[str, int, bool] | None:
    """Return (block, line, written from `::`) of a row `static_assert(hw_axis_pins::pinned<^^BLOCK, AXIS...>);`.

    The block is the reflected name, the first template argument.  Another assertion gives None.
    """
    name = assertion.child_by_field("condition")
    if name is None or name.type != "qualified_identifier":
        return None
    template = name.child_by_field("name")
    scope = name.child_by_field("scope")
    if template is None or template.type != "template_function" or scope is None:
        return None
    template_name = template.child_by_field("name")
    if template_name is None or (scope.text, template_name.text) != ROW_TEMPLATE:
        return None
    arguments = template.child_by_field("arguments")
    reflected = next(iter(arguments.children_of_type("reflect_expression")), None) if arguments else None
    # descendants() walks in source order, so the first name is the outermost one.
    operand = next(reflected.descendants(*OPERAND_NAMES), None) if reflected is not None else None
    parts = tsast.qualified_parts(operand) if operand is not None else None
    if parts is None:
        return None
    return "::".join(parts[1]), assertion.line, parts[0]


def block_name(node: tsast.Node) -> str | None:
    """Return the full name of a namespace definition from the root when it is a block, else None."""
    name = node.child_by_field("name")
    if name is None:
        return None
    leaves = [name] if name.type == "namespace_identifier" else list(name.descendants("namespace_identifier"))
    parts = tsast.qualified_parts(name)
    if not leaves or parts is None or not tsast.lexeme(leaves[-1]).endswith(BLOCK_SUFFIX):
        return None
    return "::".join(tsast.namespace_path(node) + parts[1])


def findings(context: TsastContext) -> list[check_report.Finding]:
    """Hold the blocks of the tree to the rows, in the two directions (THE TWO HALVES)."""
    found: list[check_report.Finding] = []
    rows: list[tuple[str, int, bool]] = []
    if ROWS_FILE not in context.listed():
        found.append(check_report.Finding("error", ROWS_FILE, 0, RULE, f"{ROWS_FILE} is not in the tree, so no "
                                          f"hardware-axis block is held to its axes.  Restore it", "missing-rows"))
    else:
        for tree in context.trees([ROWS_FILE]):
            rows = [row for node in tree.find("static_assert_declaration") if (row := row_of(node)) is not None]
            if not rows:
                found.append(check_report.Finding("error", ROWS_FILE, 1, RULE, f"{ROWS_FILE} holds no "
                                                  f"hw_axis_pins::pinned row", "missing-rows"))
    blocks: dict[str, list[tuple[str, int, bool]]] = {}
    for tree in context.trees([path for path in context.scope_files() if path != ROWS_FILE]):
        for node in tree.find("namespace_definition"):
            name = block_name(node)
            if name is not None:
                dead = node.ancestor_of_type(*PREPROC_ARMS) is not None
                blocks.setdefault(name, []).append((tree.path.as_posix(), node.line, dead))
    for block, line, from_root in rows:
        definitions = blocks.get(block, [])
        if not from_root:
            found.append(check_report.Finding(
                "error", ROWS_FILE, line, RULE, f"the row names {block} with no first `::`.  Write the row from "
                f"`::`, so this rule and the compiler read the same name", "relative-row", block))
        if not definitions:
            found.append(check_report.Finding(
                "error", ROWS_FILE, line, RULE, f"no file of include/ or src/ defines the namespace {block}.  "
                f"Restore the block, or remove its row", "missing-block", block))
        elif all(dead for _path, _line, dead in definitions):
            places = ", ".join(f"{path}:{place}" for path, place, _dead in definitions)
            found.append(check_report.Finding(
                "error", definitions[0][0], definitions[0][1], RULE, f"each definition of {block} ({places}) is in a "
                f"preprocessor conditional, so a build that selects the other arm has no block.  Move the block out "
                f"of the conditional", "dead-block", block))
    listed = {block for block, _line, _from_root in rows}
    for name in sorted(blocks):
        if name not in listed:
            found += [check_report.Finding(
                "error", path, line, RULE, f"the namespace {name} is a hardware-axis block with no row, so no rule "
                f"holds it to its axes.  Add a row to {ROWS_FILE} in the same change", "unlisted-site", name)
                for path, line, _dead in blocks[name]]
    return found
