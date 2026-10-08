"""contract-cites — the count of each kind of contract cite of the production tree does not fall.

THE COUNTS
    The production tree is include/ and src/.  The rule counts these nodes:
      `CRUCIBLE_PRE`, `CRUCIBLE_PRE_FAST`, `CRUCIBLE_PRE_MSG`,
      `CRUCIBLE_POST`, `CRUCIBLE_POST_FAST`, `CRUCIBLE_POST_MSG`
          a call with that name as its callee
      `contract_assert`
          a `contract_assert` statement
      `decide::`
          a name with `decide` as its last namespace, such as
          `decide::in_range` or `::foundation::decide::positive<int>`
    The tree has no P2900 contract specifier.  Each build refuses one, and the
    rule contract-form finds each specifier that a build cannot see.
    Only a cite in use counts.  The rule does not count a cite in the body of
    a test namespace (`tsast.is_test_namespace`), in a `using` declaration or
    a `using` directive, or in a macro definition.  An alias declaration
    (`using X = ...`) counts, because a refinement type can hold a predicate
    that is in use.  A comment and a literal are nodes of their own kinds, and
    a cite in them does not count.  The catalog of the `decide::` procedures
    comes from the parse of `Decide.h` (`assay.guard.engines.tsast.decide_catalog`).
    Each procedure of the catalog gets a count.

THE BASELINE
    utils/ledgers/contract-cites.txt is a ledger of the policy baseline.  It
    holds one row for each counter of `COUNTERS`, and one row for the count of
    each `decide::` procedure:
        - | - | COUNT | COUNTER | -
        - | decide_per_procedure | COUNT | PROCEDURE | -
    The rule reads the baseline itself, and it refuses:
      `fall`      a counter of `COUNTERS` that is less than its row
      `baseline`  a baseline with no file, with no integer for a counter, or
                  with a row that does not obey format 1
      `unread`    a tree with no include/ or no src/, or a file that does not
                  parse
    A row of a procedure is for a reader and for the rule decide-cite-ratio.
    `guard write --rule contract-cites` writes the baseline from the counts
    of the tree (`written()`).  When a counter decreases for a correct reason,
    write the baseline again and give the reason in the commit.

Complexity: linear in the number of bytes of the files of include/ and src/.
"""

from collections import Counter
from dataclasses import dataclass, replace
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.guard.engines.tsast import decide_catalog
from assay.guard.engines.tsast.runner import TsastContext
from assay.readers import tsast

RULE = "contract-cites"
# The kind of the row of the count of one `decide::` procedure.
PROCEDURE_KIND = "decide_per_procedure"
SCOPE = ("include/", "src/")
MACRO_CITES = (
    "CRUCIBLE_PRE",
    "CRUCIBLE_PRE_FAST",
    "CRUCIBLE_PRE_MSG",
    "CRUCIBLE_POST",
    "CRUCIBLE_POST_FAST",
    "CRUCIBLE_POST_MSG",
)
# The counters that the rule holds, in the sequence of the baseline.
COUNTERS = (
    "crucible_pre",
    "crucible_pre_fast",
    "crucible_pre_msg",
    "crucible_post",
    "crucible_post_fast",
    "crucible_post_msg",
    "contract_assert",
    "decide_total",
    "total_pre_cites",
    "total_post_cites",
    "total_contract_cites",
)


class AuditError(Exception):
    """The rule cannot give a correct count: a directory of the scope is missing, or a file does not parse."""


@dataclass(frozen=True, slots=True)
class Counts:
    """The counts of a tree: each macro, the `contract_assert` statements, the `decide::` cites and each procedure."""

    macros: Counter[str]
    contract_assert: int
    decide_total: int
    procedures: dict[str, int]

    def counters(self) -> dict[str, int]:
        """Return the value of each counter of `COUNTERS`."""
        macros = {name.lower(): self.macros[name] for name in MACRO_CITES}
        total_pre = macros["crucible_pre"] + macros["crucible_pre_fast"] + macros["crucible_pre_msg"]
        total_post = macros["crucible_post"] + macros["crucible_post_fast"] + macros["crucible_post_msg"]
        return {
            **macros,
            "contract_assert": self.contract_assert,
            "decide_total": self.decide_total,
            "total_pre_cites": total_pre,
            "total_post_cites": total_post,
            "total_contract_cites": total_pre + total_post + self.contract_assert,
        }


def is_in_use(node: tsast.Node) -> bool:
    """Return True when a node is production code in use: in no test namespace and in no `using` declaration."""
    if node.ancestor_of_type("using_declaration") is not None:
        return False
    return not tsast.is_test_namespace(tsast.namespace_path(node))


def decide_leaf(name: tsast.Node) -> str | None:
    """Return the name after `decide::` when `decide` is the last namespace of a name, or None."""
    parts = tsast.qualified_parts(name)
    if parts is None:
        return None
    segments = parts[1]
    return segments[-1] if len(segments) >= 2 and segments[-2] == "decide" else None


def count_tree(tree: tsast.Tree) -> Counts:
    """Return the cites in use of one file.  Its procedures hold the cites of each name after `decide::`.

    Complexity: linear in the number of nodes of the file.
    """
    macros: Counter[str] = Counter()
    for call in tree.find("call_expression"):
        callee = call.child_by_field("function")
        if callee is not None and callee.type == "identifier" and is_in_use(call):
            name = tsast.lexeme(callee)
            if name in MACRO_CITES:
                macros[name] += 1
    asserts = sum(is_in_use(node) for node in tree.find("contract_assert_statement"))
    leaves: Counter[str] = Counter()
    for name_node in tree.find("qualified_identifier"):
        # Only the outermost node of a nested name is the name that the source spells.
        if name_node.parent is not None and name_node.parent.type == "qualified_identifier":
            continue
        leaf = decide_leaf(name_node)
        if leaf is not None and is_in_use(name_node):
            leaves[leaf] += 1
    return Counts(macros, asserts, leaves.total(), dict(leaves))


def counts_of(context: TsastContext) -> Counts:
    """Count the cites in use of include/ and src/ of the tree of a context (THE COUNTS).

    Raises:
        AuditError: If a directory of the scope is missing, or a file does not parse
    """
    missing = [top for top in SCOPE if not (context.root / top).is_dir()]
    if missing:
        raise AuditError(f"the tree has no {' or '.join(missing)} directory, so the count would be a false zero")
    catalog = decide_catalog.DECIDE_HEADERS[0]
    procedures: list[str] = []
    for tree in context.trees([catalog] if catalog in context.listed() else [], with_broken=True):
        procedures = decide_catalog.procedures(parsed(tree))
    parts = [count_tree(parsed(tree)) for tree in context.trees(context.files(SCOPE), with_broken=True)]
    macros: Counter[str] = Counter()
    leaves: Counter[str] = Counter()
    for part in parts:
        macros.update(part.macros)
        leaves.update(part.procedures)
    asserts = sum(part.contract_assert for part in parts)
    return Counts(macros, asserts, leaves.total(), {name: leaves[name] for name in procedures})


def parsed(tree: tsast.Tree) -> tsast.Tree:
    """Return a tree that parses with no error.

    Raises:
        AuditError: If the file does not parse, because the rule cannot count a cite in it
    """
    error = tsast.first_error(tree)
    if error is not None:
        raise AuditError(f"{tree.path.as_posix()} does not parse at {error[1]}, so a cite in it is not counted")
    return tree


def scan(root: Path) -> Counts:
    """Count the cites in use of include/ and src/ of the git work tree at root (`counts_of()`).

    The rule decide-cite-ratio reads the count of each procedure through this function.

    Raises:
        AuditError: If a directory of the scope is missing, or a file does not parse
        NotApplicable: If the root is not a git work tree
    """
    return counts_of(TsastContext(root))


def baseline_values(ledger: rule_ledger.Ledger) -> dict[str, int | float | str]:
    """Return the value of each counter row of the baseline, by the name of its counter."""
    return {key.entity: entry.value for key, entry in ledger.base.items() if not key.path and not key.kind}


def finding(message: str, kind: str, entity: str = "", line: int = 0) -> check_report.Finding:
    """Return one error of the rule on its ledger."""
    return check_report.Finding("error", rule_ledger.shown_path(RULE), line, RULE, message, kind, entity)


def findings(context: TsastContext) -> list[check_report.Finding]:
    """Compare the counts of the tree with the baseline, and give an error for each counter that fell (THE BASELINE)."""
    try:
        counts = counts_of(context)
    except AuditError as problem:
        return [finding(f"{problem}", "unread")]
    rule = context.rule or rule_ledger.rule_of(RULE)
    path = rule_ledger.ledger_path(RULE, context.root)
    if not path.is_file():
        return [
            finding(
                "the baseline does not exist.  Write it with `python3 utils/bin/guard write --rule contract-cites`",
                "baseline",
            )
        ]
    try:
        ledger = rule_ledger.read(rule, context.root)
    except rows_grammar.RowsError as problem:
        return [finding(f"the baseline does not obey format 1: {problem}", "baseline", line=problem.line)]
    baseline = baseline_values(ledger)
    current = counts.counters()
    found: list[check_report.Finding] = []
    for name in COUNTERS:
        value = baseline.get(name)
        if not isinstance(value, (int, float)) or isinstance(value, float) and not value.is_integer():
            found.append(
                finding(
                    f"the baseline has no integer for {name}.  Write it again with `python3 "
                    f"utils/bin/guard write --rule contract-cites`",
                    "baseline",
                    name,
                )
            )
        elif current[name] < value:
            line = ledger.line_of(rule_ledger.Key("", "", name))
            found.append(
                finding(
                    f"the counter {name} fell from {int(value)} to {current[name]}.  Restore the cites, "
                    f"or write the baseline again with `python3 utils/bin/guard write --rule "
                    f"contract-cites` and give the reason in the commit",
                    "fall",
                    name,
                    line,
                )
            )
    return found


def written(context: TsastContext, ledger: rule_ledger.Ledger, reason: str | None) -> rule_ledger.Ledger:
    """Return the baseline of the counts of the tree, with the head comment of the ledger (THE BASELINE).

    The rows of a baseline have no reason, and the function does not read REASON.

    Raises:
        LedgerError: If the rule cannot count the tree
    """
    try:
        counts = counts_of(context)
    except AuditError as problem:
        raise rule_ledger.LedgerError(f"the baseline is not written: {problem}") from None
    rows = [(rule_ledger.Key("", "", name), value) for name, value in counts.counters().items()]
    rows += [(rule_ledger.Key("", PROCEDURE_KIND, name), value) for name, value in counts.procedures.items()]
    return replace(ledger, base={key: rule_ledger.Entry(key, value, "") for key, value in rows})
