"""definition-home — each entity that a header of the base declares has its definition in that header.

THE RULE
    The quarantine plugin of utils/tools/quarantine/ reads the header of each
    sentinel unit of include/foundation/ and include/fixy/ (definition.cpp).
    A header that declares an entity and leaves its definition to a different
    file lets a unit that includes only that header give a definition of its
    own, and a definition of a member or of a friend has the access of the
    class.  The kinds of a finding line:
      friend     a friend declaration of a class of the header names an
                 entity that the header does not define
      elsewhere  an entity that the header declares has its definition in a
                 different file of the unit, or the header defines a member
                 or a friend of a class of a different file.  A file of
                 src/foundation/ or src/fixy/ that gives such a definition
                 inline, constexpr or as a template gets the kind too
      undefined  an entity that the header declares has no definition in the
                 unit, other than a class of a namespace and an out-of-line
                 entity
    An out-of-line entity, with its one body in a source file of
    src/foundation/ or src/fixy/, is no finding (definition.cpp, THE
    OUT-OF-LINE ENTITIES).

THE JOIN
    A unit cannot see a definition in a file that it does not include, so the
    plugin gives five records, and this module joins them over the census of
    the build (definition.cpp, THE CLASSES):
      declared  a class that a header declares and its unit does not define.
                The word template after the name marks a class template
      defined   a class that a header or a base source file defines
      body      an out-of-line entity that a base source file defines, by its
                symbol and its name
      header    the header of a sentinel unit
      homed     a friend that a home row names: a record and no finding
    The join gives the kind elsewhere, at the declaration, for a declared
    class that a header of a different file defines, that more than one file
    defines, or that a base source file defines when the class is a template.
    It gives the kind elsewhere, at each later definition, for a class of the
    records defined and a symbol of the records body that two files define.
    A build in which a header of include/foundation/ or include/fixy/ has no
    record header is a problem: the rule has not read that header.  A planted
    census with no build directory has no such problem.

THE LEDGER, THE WRITE
    assay.guard.engines.plugin.linear_family: the ledger counts the findings
    of each directory and kind, and each count row gives a reason.

Usage
    guard check --rule definition-home --build-dir DIR [--warnings-dir DIR]
    guard write --rule definition-home --build-dir DIR [--raise] [--reason TEXT]

Complexity: O(n log n) for the n lines of the rule in the census, and linear in the headers of the two layers.
"""

from collections.abc import Iterable, Sequence
from pathlib import Path

from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.guard.engines.plugin import declarations, linear_family, runner
from assay.guard.engines.plugin.home import read_as
from assay.guard.engines.plugin.quarantine import directory_of
from assay.readers import sections as quarantine_sections

RULE = "definition-home"
REFUSED_KINDS = linear_family.REFUSED_KINDS
FRIEND = "friend"
ELSEWHERE = "elsewhere"
UNDEFINED = "undefined"
DECLARED = "declared"
DEFINED = "defined"
BODY = "body"
HEADER = "header"
HOMED = "homed"
RECORDS = frozenset({DECLARED, DEFINED, BODY, HEADER, HOMED})
# The word after the name of a declared class template (THE JOIN).
TEMPLATE_MARK = " template"
# The trees of the headers that the rule reads, and the trees of the base source files.
HEADER_TREES = ("include/foundation", "include/fixy")
SOURCE_TREES = ("src/foundation/", "src/fixy/")
ACTIONS = {
    FRIEND: "the class names a friend that its header does not define, so a unit that includes only the header can "
    "give the friend a body with the access of the class.  Define the friend in the header, or give a function the "
    "form of an out-of-line entity with its one body in src/foundation/ or src/fixy/",
    ELSEWHERE: "the entity has its definition in a different file, so a unit that includes only the header can give "
    "a definition of its own.  Move the definition into the header that declares the entity, or remove the "
    "declaration and include the header of the definition",
    UNDEFINED: "the header declares the entity and the unit has no definition of it.  Define the entity in the "
    "header, or give it the form of an out-of-line entity with its one body in src/foundation/ or src/fixy/",
}


def finding_of(item: quarantine_sections.Finding) -> check_report.Finding:
    """Return the error of one finding line of the plugin, at the directory of its file (THE LEDGER)."""
    return check_report.Finding(
        "error",
        directory_of(item.file),
        item.line,
        RULE,
        linear_family.message(item.text(), item.kind, ACTIONS),
        item.kind,
    )


def joined(file: str, line: int, text: str) -> check_report.Finding:
    """Return one finding of the join, of the kind elsewhere, at the directory of FILE."""
    return check_report.Finding(
        "error",
        directory_of(file),
        line,
        RULE,
        linear_family.message(text, ELSEWHERE, ACTIONS),
        ELSEWHERE,
    )


def is_base_source(path: str) -> bool:
    """Say if PATH is a source file of the base, where an out-of-line entity has its body."""
    return path.startswith(SOURCE_TREES)


def definers(
    lines: Iterable[quarantine_sections.Finding],
) -> dict[str, list[tuple[str, int]]]:
    """Return the files and lines of each name of the records LINES, each file one time, sorted.

    The name of a record body is its symbol, the first word of its entity.  Complexity: O(n log n).
    """
    found: dict[str, dict[str, int]] = {}
    for item in lines:
        name = item.entity.split(" ", 1)[0] if item.kind == BODY else item.entity
        found.setdefault(name, {}).setdefault(read_as(item.file), item.line)
    return {name: sorted(files.items()) for name, files in found.items()}


def declared_findings(
    declared: Sequence[quarantine_sections.Finding],
    classes: dict[str, list[tuple[str, int]]],
) -> list[check_report.Finding]:
    """Give the finding of each declared class that a different file defines in a way that the rule refuses."""
    found: list[check_report.Finding] = []
    seen: set[tuple[str, str]] = set()
    for item in declared:
        file = read_as(item.file)
        if (file, item.entity) in seen:
            continue
        seen.add((file, item.entity))
        is_template = item.entity.endswith(TEMPLATE_MARK)
        name = item.entity.removesuffix(TEMPLATE_MARK)
        others = [other for other, _line in classes.get(name, []) if other != file]
        is_admitted = not others or (len(others) == 1 and is_base_source(others[0]) and not is_template)
        if not is_admitted:
            found.append(
                joined(
                    item.file,
                    item.line,
                    f"{file}:{item.line}: {name} defined in {', '.join(others)}",
                )
            )
    return found


def duplicate_findings(names: dict[str, list[tuple[str, int]]], what: str) -> list[check_report.Finding]:
    """Give the finding of each later definition of a name that more than one file defines."""
    found: list[check_report.Finding] = []
    for name, places in sorted(names.items()):
        first = places[0][0]
        for file, line in places[1:]:
            found.append(
                joined(
                    file,
                    line,
                    f"{file}:{line}: {what} {name} has a definition in {first} too",
                )
            )
    return found


def header_problems(root: Path, headers: Iterable[str]) -> list[check_report.Finding]:
    """Give one problem for each header of the two layers with no record header (THE JOIN).

    Complexity: linear in the headers of the two layers.
    """
    read = frozenset(headers)
    expected: list[str] = []
    for tree in HEADER_TREES:
        if (root / tree).is_dir():
            expected += [str(path.relative_to(root)) for path in sorted((root / tree).rglob("*.h"))]
    return [
        check_report.Finding(
            "error",
            header,
            0,
            RULE,
            f"{header} has no record header of the rule, so no unit of the build read it.  Give the header a "
            f"sentinel unit in test/layer/CMakeLists.txt, and build the target all",
        )
        for header in expected
        if header not in read
    ]


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give an error for each finding line, each finding of the join, each count row with no reason and each problem.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    census = context.census()
    lines = census.of_rule(RULE)
    found = census.errors(RULE)
    found += [finding_of(item) for item in lines if item.kind not in RECORDS]
    classes = definers(item for item in lines if item.kind == DEFINED)
    found += declared_findings([item for item in lines if item.kind == DECLARED], classes)
    found += duplicate_findings(classes, "the class")
    found += duplicate_findings(definers(item for item in lines if item.kind == BODY), "the symbol")
    if context.build_dir is not None:
        found += header_problems(context.root, (item.entity for item in lines if item.kind == HEADER))
    found += linear_family.reason_errors(declarations.rule_of(context, RULE), context.root)
    return found


def write(rule: rule_registry.Rule, request: runner.WriteRequest) -> int:
    """Write the ledger of the rule from the findings of the builds of the request (THE WRITE of linear_family)."""
    return linear_family.write(rule, request)
