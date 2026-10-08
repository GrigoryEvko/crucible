"""contract-cites — count the contract cites that the production tree uses, read from the parse tree.

The production tree is include/ and src/.  The audit reads the parse of the
pinned tree-sitter kit (utils/assay/readers/tsast.py) and counts these nodes:

  CRUCIBLE_PRE, CRUCIBLE_PRE_FAST, CRUCIBLE_PRE_MSG,
  CRUCIBLE_POST, CRUCIBLE_POST_FAST, CRUCIBLE_POST_MSG
      a call expression whose callee is that name
  contract_assert
      a contract_assert statement

The tree has no P2900 contract specifier: each build rejects one, and the
rule contract-form (`guard check --rule contract-form`) finds each one that a
build cannot see.
  decide::
      a qualified name whose last scope is `decide`, as in decide::in_range
      or ::foundation::decide::positive<int>

Only a cite in use counts, so the audit skips these places:
  * the body of a test namespace (tsast.is_test_namespace).  A header's
    self-test is test code, as test/ is.
  * a using-declaration or using-directive.  It re-exports a name and
    checks nothing.  An alias declaration (`using X = ...`) stays, because
    a refinement type can hold a predicate that is in use.
  * a macro definition.  Its body is a raw preproc_arg node, and a #define
    defines a cite, it does not use one.
A comment or a literal is a node of its own kind, so a cite that it names
never counts.

The decide:: catalog comes from the parse of Decide.h
(utils/assay/guard/engines/tsast/decide_catalog.py), so a procedure that
Decide.h defines is always counted.  The cite-ratio audit reads the
per-procedure counts from --json.

Modes of `guard check --rule contract-cites [MODE]`:
  (none)           a summary for a reader
  --json           one JSON object
  --baseline       write the counters to the baseline, utils/ledgers/contract-cites.txt
  --check          compare with the baseline, and fail on a counter that fell
  --self-test      plant each case and check each count

THE BASELINE
    utils/ledgers/contract-cites.txt is a ledger of the policy baseline, in
    format 1 of assay.core.ledger.  It holds one row for each counter of the
    JSON object, and one row for the count of each decide:: procedure:
        - | - | COUNT | COUNTER | -
        - | decide_per_procedure | COUNT | PROCEDURE | -
    --check reads the counters of CHECKED, and a row of a procedure is for a
    reader.

A file of the production tree that the kit cannot parse stops the audit,
because a cite inside it would go uncounted.

Exit status: 0 success, 1 a regression under --check, 2 a bad invocation, a
malformed baseline, a parse failure or a failed self-test, 3 the kit is missing.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import json
import sys
import tempfile
from collections import Counter
from dataclasses import dataclass, field, replace
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import rows as rows_grammar
from assay.guard.engines.tsast import decide_catalog
from assay.readers import tsast

RULE = "contract-cites"
# The kind of the row of the count of one decide:: procedure.
PROCEDURE_KIND = "decide_per_procedure"
ROOTS = ("include", "src")
MACRO_CITES = ("CRUCIBLE_PRE", "CRUCIBLE_PRE_FAST", "CRUCIBLE_PRE_MSG",
               "CRUCIBLE_POST", "CRUCIBLE_POST_FAST", "CRUCIBLE_POST_MSG")
# The counters that --check guards.
CHECKED =("crucible_pre", "crucible_pre_fast", "crucible_pre_msg",
           "crucible_post", "crucible_post_fast", "crucible_post_msg",
           "contract_assert", "decide_total", "total_pre_cites", "total_post_cites", "total_contract_cites")
TOP_FILES = 10


class AuditError(Exception):
    """The audit cannot give a true count: a root is missing or a file does not parse."""


@dataclass
class Counts:
    """The counters of one scan, and the contract cites of each file."""

    macros: Counter = field(default_factory=Counter)
    contract_assert: int = 0
    decide_total: int = 0
    procedures: dict[str, int] = field(default_factory=dict)
    per_file: Counter = field(default_factory=Counter)

    def as_json(self) -> dict:
        """The flat JSON object: the counters, the totals, then the count of each procedure."""
        macros = {name.lower(): self.macros[name] for name in MACRO_CITES}
        total_pre = macros["crucible_pre"] + macros["crucible_pre_fast"] + macros["crucible_pre_msg"]
        total_post = macros["crucible_post"] + macros["crucible_post_fast"] + macros["crucible_post_msg"]
        return {**macros, "contract_assert": self.contract_assert, "decide_total": self.decide_total,
                "total_pre_cites": total_pre, "total_post_cites": total_post,
                "total_contract_cites": total_pre + total_post + self.contract_assert,
                "decide_per_procedure": dict(self.procedures)}


def is_in_use(node: tsast.Node) -> bool:
    """Report whether a node is live production code: outside a test namespace and outside a re-export."""
    if node.ancestor_of_type("using_declaration") is not None:
        return False
    return not tsast.is_test_namespace(tsast.namespace_path(node))


def decide_leaf(name: tsast.Node) -> str | None:
    """The name after `decide::` when a qualified name's last scope is decide, or None."""
    parts = tsast.qualified_parts(name)
    if parts is None:
        return None
    _is_global, segments = parts
    return segments[-1] if len(segments) >= 2 and segments[-2] == "decide" else None


def count_tree(tree: tsast.Tree, counts: Counts, shown: str) -> None:
    """Add the cites in use of one parsed file to the counters.

    Complexity: linear in the number of nodes of the file."""
    for call in tree.find("call_expression"):
        callee = call.child_by_field("function")
        if callee is not None and callee.type == "identifier" and callee.text in MACRO_CITES and is_in_use(call):
            counts.macros[callee.text] += 1
            counts.per_file[shown] += 1
    for statement in tree.find("contract_assert_statement"):
        if is_in_use(statement):
            counts.contract_assert += 1
            counts.per_file[shown] += 1
    for name in tree.find("qualified_identifier"):
        # A nested name is one node inside another.  Only the outermost
        # node is the name as written, so the inner ones are not counted.
        if name.parent is not None and name.parent.type == "qualified_identifier":
            continue
        leaf = decide_leaf(name)
        if leaf is None or not is_in_use(name):
            continue
        counts.decide_total += 1
        if leaf in counts.procedures:
            counts.procedures[leaf] += 1


def scan(root: Path) -> Counts:
    """Count the cites in use under include/ and src/ of one tree.

    Raises:
        AuditError: If a root is missing or a file does not parse
    """
    missing = [top for top in ROOTS if not (root / top).is_dir()]
    if missing:
        raise AuditError(f"{root} has no {' or '.join(missing)} directory, so the count would be a false zero")
    catalog_path = root / decide_catalog.DECIDE_HEADERS[0]
    procedures: list[str] = []
    if catalog_path.is_file():
        procedures = decide_catalog.procedures(next(tsast.parse([catalog_path], strict=True)))
    counts = Counts(procedures={name: 0 for name in procedures})
    # A rostered file is not C++ (vmlinux.h is generated BPF C), so it is
    # out of scope, as it is for every AST gate.
    files = [path for top in ROOTS for path in sorted((root / top).rglob("*"))
             if path.is_file() and path.suffix in tsast.CPP_SUFFIXES
             and path.relative_to(root).as_posix() not in tsast.UNPARSEABLE]
    for tree in tsast.parse(files, strict=False):
        path = Path(tree.path)
        shown = path.relative_to(root).as_posix() if path.is_relative_to(root) else path.as_posix()
        if tree.diagnostic is not None:
            raise AuditError(f"{shown} does not parse, so a cite inside it would go uncounted.  "
                             f"{tree.diagnostic.strip()}")
        count_tree(tree, counts, shown)
    return counts


def print_human(counts: Counts) -> None:
    """Print the counters, the count of each procedure and the densest files."""
    data = counts.as_json()
    print("=== Crucible contract cites in use ===")
    print("(production tree: include/ and src/)\n")
    rows = [("CRUCIBLE_PRE", "crucible_pre"), ("CRUCIBLE_PRE_FAST", "crucible_pre_fast"),
            ("CRUCIBLE_PRE_MSG", "crucible_pre_msg"), ("CRUCIBLE_POST", "crucible_post"),
            ("CRUCIBLE_POST_FAST", "crucible_post_fast"), ("CRUCIBLE_POST_MSG", "crucible_post_msg"),
            ("contract_assert", "contract_assert"),
            ("decide:: cites", "decide_total"), ("Total pre cites", "total_pre_cites"),
            ("Total post cites", "total_post_cites"), ("Total contract cites", "total_contract_cites")]
    for label, key in rows:
        print(f"  {label:<22} {data[key]}")
    print("\n-- Cites of each decide:: procedure --")
    for name, count in data["decide_per_procedure"].items():
        print(f"  decide::{name:<30} {count}")
    print(f"\n-- The {TOP_FILES} files with the most contract cites --")
    for shown, count in sorted(counts.per_file.items(), key=lambda item: (-item[1], item[0]))[:TOP_FILES]:
        print(f"  {shown:<60} {count}")


def read_baseline(rule: rule_registry.Rule, root: Path) -> dict:
    """Read the baseline as the JSON object of --json: each counter, and the count of each procedure.

    A whole number is an int, as in the JSON object.  A row of another form
    keeps its value, and --check refuses it as a counter with no integer.

    Raises:
        OSError: If the ledger does not exist or cannot be read
        rows_grammar.RowsError: If the ledger does not obey format 1
    """
    path = rule_ledger.ledger_path(rule.id, root)
    if not path.is_file():
        raise FileNotFoundError("the file does not exist.  Write it with --baseline")
    counters: dict = {}
    procedures: dict = {}
    for key, entry in rule_ledger.read(rule, root).base.items():
        value = int(entry.value) if isinstance(entry.value, float) and entry.value.is_integer() else entry.value
        if not key.path and not key.kind:
            counters[key.entity] = value
        elif not key.path and key.kind == PROCEDURE_KIND:
            procedures[key.entity] = value
    return {**counters, "decide_per_procedure": procedures}


def write_baseline(rule: rule_registry.Rule, root: Path, counts: Counts) -> None:
    """Write the counters and the count of each procedure to the baseline, and keep its head comment.

    Raises:
        OSError: If the ledger cannot be read or written
        rows_grammar.RowsError: If the ledger exists and does not obey format 1
    """
    data = counts.as_json()
    procedures = data.pop("decide_per_procedure")
    keys = [(rule_ledger.Key("", "", name), value) for name, value in data.items()]
    keys += [(rule_ledger.Key("", PROCEDURE_KIND, name), value) for name, value in procedures.items()]
    table = rule_ledger.read(rule, root)
    rule_ledger.store(replace(table, base={key: rule_ledger.Entry(key, value, "") for key, value in keys}), root)


def check(counts: Counts, rule: rule_registry.Rule, root: Path) -> int:
    """Compare the counters with the baseline and report each one that fell.

    Returns:
        0 when no guarded counter fell, 1 on a regression, 2 on a bad baseline
    """
    shown = rule_ledger.shown_path(rule.id)
    try:
        baseline = read_baseline(rule, root)
    except (OSError, rows_grammar.RowsError) as error:
        print(f"{RULE}: cannot read the baseline {shown}: {error}", file=sys.stderr)
        return 2
    absent = [name for name in CHECKED if not isinstance(baseline.get(name), int)]
    if absent:
        print(f"{RULE}: the baseline {shown} has no integer for {', '.join(absent)}.  Write it again with --baseline.",
              file=sys.stderr)
        return 2
    current = counts.as_json()
    regressed = False
    for name in CHECKED:
        if current[name] < baseline[name]:
            print(f"{RULE}: REGRESSION {name}: {baseline[name]} -> {current[name]}", file=sys.stderr)
            regressed = True
    if regressed:
        print(f"{RULE}: a contract counter fell.  Restore the cites, or write the baseline again with --baseline and "
              f"say why in the commit.", file=sys.stderr)
        return 1
    print(f"{RULE}: no regression against {shown}", file=sys.stderr)
    return 0


def run(argv: list[str], root: Path) -> int:
    """Parse the arguments, run one mode over the tree at root, and return the exit status.

    The baseline is utils/ledgers/contract-cites.txt under the root.
    """
    parser = argparse.ArgumentParser(prog=f"guard check --rule {RULE}",
                                     description="Count the contract cites that the production tree uses.")
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--json", action="store_true")
    modes.add_argument("--baseline", action="store_true", help=f"write the baseline {rule_ledger.shown_path(RULE)}")
    modes.add_argument("--check", action="store_true", help=f"compare with the baseline {rule_ledger.shown_path(RULE)}")
    modes.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    rule: rule_registry.Rule | None = None
    if args.baseline or args.check:
        try:
            rule = rule_ledger.rule_of(RULE)
        except (OSError, rule_ledger.LedgerError, rows_grammar.RowsError) as error:
            print(f"{RULE}: {error}", file=sys.stderr)
            return 2
    try:
        counts = scan(root)
    except tsast.KitMissing as missing:
        print(f"{RULE}: SKIP, {missing}", file=sys.stderr)
        return 3
    except (AuditError, tsast.ParseError) as error:
        print(f"{RULE}: {error}", file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(counts.as_json(), separators=(",", ":")))
    elif args.baseline and rule is not None:
        try:
            write_baseline(rule, root, counts)
        except (OSError, rows_grammar.RowsError) as error:
            print(f"{RULE}: the baseline is not written: {error}", file=sys.stderr)
            return 2
        print(f"{RULE}: baseline written to {rule_ledger.shown_path(RULE)}", file=sys.stderr)
    elif args.check and rule is not None:
        return check(counts, rule, root)
    else:
        print_human(counts)
    return 0


# ── the self-test ──────────────────────────────────────────────────────

PLANTED_CATALOG = ("alpha", "beta", "coprime")

FIXTURE_HEAD = """#pragma once
namespace crucible::planted {
inline void planted_macro_forms(int n) {
    CRUCIBLE_PRE(n > 0);
    CRUCIBLE_PRE_FAST(n > 0);
    CRUCIBLE_PRE_MSG(n > 0, "synthetic");
    CRUCIBLE_POST(r, r > 0);
    CRUCIBLE_POST_FAST(r, r > 0);
    CRUCIBLE_POST_MSG(r, r > 0, "synthetic");
    contract_assert(n > 0);
}
inline void planted_decide_cites(int n) {
"""
FIXTURE_TAIL = "}\n}  // namespace crucible::planted\n"

# Each cite here is a re-export, a namespace head or alias, or sits in the
# body of a test namespace, except one real cite of coprime in a namespace
# whose name holds the letters "test" inside a longer word.  The test
# namespace also holds braces inside a character, a string, a raw string
# and a digit separator.
ALIAS_FIXTURE = """#pragma once
namespace fixy::decide {
using ::foundation::decide::coprime;
using ::foundation::decide::
    Interval;
namespace catalog_alias = ::foundation::decide;
}  // namespace fixy::decide
namespace foundation::decide::oracle {
}  // namespace foundation::decide::oracle
namespace fixy::decide::self_test {
static_assert(std::is_same_v<decltype(&::fixy::decide::coprime<int>),
                             decltype(&::foundation::decide::coprime<int>)>);
inline constexpr char close_brace = '}';
inline constexpr const char* closers = "}}";
inline constexpr const char* raw_closers = R"(}})";
inline constexpr int separated = 1'000;
inline void runtime_smoke_test(int n) {
    if (n > 0) { CRUCIBLE_PRE(decide::coprime(n, 3)); }
    contract_assert(decide::coprime(n, 3));
}
}  // namespace fixy::decide::self_test
namespace crucible::detail::coprime_self_test {
inline bool probe(int n) { return decide::coprime(n, 9); }
}  // namespace crucible::detail::coprime_self_test
namespace crucible::lattice_test {
inline bool probe(int n) { return decide::coprime(n, 11); }
}  // namespace crucible::lattice_test
namespace crucible::attestation {
inline bool real_use(int n) {
    CRUCIBLE_PRE(decide::coprime(n, 7));
    return true;
}
}  // namespace crucible::attestation
"""

# Each shape here fooled the text count that this audit replaces: a macro
# definition read as two cites, and a cite split after `decide::` read as
# none.  A function named pre or post is no cite.  A cite inside a block
# comment and a string must stay at zero.
TRAPS_FIXTURE = """#pragma once
#define CRUCIBLE_PRE(condition) contract_assert(condition)
namespace crucible::traps {
inline int pre(int value) { return value; }
inline int post(int value) { return pre(value); }
inline void split_cite(int n) {
    CRUCIBLE_PRE(decide::
        alpha(n));
    int unused = 0; /* CRUCIBLE_PRE(decide::beta(n)); */
    const char* text = "contract_assert(decide::beta(n))";
    (void)unused; (void)text;
}
}  // namespace crucible::traps
"""


def _plant(root: Path, files: dict[str, str]) -> None:
    """Write one planted tree: a Decide.h that defines the planted catalog, an anchor source, and the files."""
    decide = root / decide_catalog.DECIDE_HEADERS[0]
    decide.parent.mkdir(parents=True)
    body = "".join(f"constexpr bool {name}(int) noexcept {{ return true; }}\n" for name in PLANTED_CATALOG)
    decide.write_text(f"#pragma once\nnamespace foundation::decide {{\n{body}}}\n")
    (root / "src").mkdir()
    (root / "src" / "anchor.cpp").write_text("int crucible_selftest_anchor = 0;\n")
    for rel, text in files.items():
        (root / rel).parent.mkdir(parents=True, exist_ok=True)
        (root / rel).write_text(text)


def _plant_baseline(rule: rule_registry.Rule, root: Path, counters: dict[str, int | float]) -> None:
    """Write a planted baseline under the root with one row for each counter."""
    keys = [rule_ledger.Key("", "", name) for name in counters]
    base = {key: rule_ledger.Entry(key, counters[key.entity], "") for key in keys}
    rule_ledger.store(replace(rule_ledger.empty(rule), base=base), root)


def _quiet(action) -> tuple[int, str]:
    """Run one action with stdout and stderr captured, and return its status and its stderr."""
    err = io.StringIO()
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(err):
        status = action()
    return status, err.getvalue()


def self_test() -> int:
    """Plant each counted form and each trap, and check every count and every --check verdict."""
    try:
        tsast.kit_dir()
    except tsast.KitMissing as missing:
        print(f"{RULE}: SKIP, {missing}", file=sys.stderr)
        return 3
    failures: list[str] = []

    def expect(label: str, ok: bool) -> None:
        if not ok:
            failures.append(label)

    with tempfile.TemporaryDirectory(prefix="contract-count-self-test-") as tmp_name:
        tmp = Path(tmp_name)
        planted = tmp / "planted"
        cites = "".join(f"    decide::{name}(n);\n" * 2 for name in PLANTED_CATALOG)
        _plant(planted, {"include/selftest_cites.h": FIXTURE_HEAD + cites + FIXTURE_TAIL})
        data = scan(planted).as_json()
        want = {name.lower(): 1 for name in MACRO_CITES} | {"contract_assert": 1,
                                                             "decide_total": 2 * len(PLANTED_CATALOG)}
        for key, value in want.items():
            expect(f"the planted tree reports {key}={value}, not {data[key]}", data[key] == value)
        expect(f"every planted procedure has 2 cites: {data['decide_per_procedure']}",
               data["decide_per_procedure"] == {name: 2 for name in PLANTED_CATALOG})

        rule = rule_ledger.rule_of(RULE)
        baseline = rule_ledger.ledger_path(RULE, planted)
        status, report = _quiet(lambda: run(["--check"], planted))
        expect(f"a missing baseline must stop with 2, not {status}", status == 2 and "does not exist" in report)
        status, _ = _quiet(lambda: run(["--baseline"], planted))
        expect(f"--baseline writes the counters and the count of each procedure, not {status}",
               status == 0 and read_baseline(rule, planted) == data)
        expect("an equal baseline passes", _quiet(lambda: run(["--check"], planted))[0] == 0)
        _plant_baseline(rule, planted, {name: 9999 for name in CHECKED})
        status, report = _quiet(lambda: run(["--check"], planted))
        expect(f"a higher baseline must fail with 1, not {status}", status == 1)
        for name in CHECKED:
            expect(f"a REGRESSION line for {name}", f"REGRESSION {name}: 9999 ->" in report)
        _plant_baseline(rule, planted, {name: 0 for name in CHECKED})
        expect("a lower baseline passes", _quiet(lambda: run(["--check"], planted))[0] == 0)
        _plant_baseline(rule, planted, {name: 0 for name in CHECKED[1:]})
        status, report = _quiet(lambda: run(["--check"], planted))
        expect(f"a baseline without {CHECKED[0]} must stop with 2, not {status}", status == 2 and CHECKED[0] in report)
        _plant_baseline(rule, planted, {name: 0.5 if name == CHECKED[0] else 0 for name in CHECKED})
        status, report = _quiet(lambda: run(["--check"], planted))
        expect(f"a baseline with a fraction for {CHECKED[0]} must stop with 2, not {status}",
               status == 2 and CHECKED[0] in report)
        baseline.write_text(baseline.read_text(encoding="utf-8") + "- | - | many | planted_counter | -\n",
                            encoding="utf-8")
        status, report = _quiet(lambda: run(["--check"], planted))
        expect(f"a malformed baseline must stop with 2 at its line, not {status}",
               status == 2 and f"{rule_ledger.shown_path(RULE)}:" in report)

        alias = tmp / "alias"
        _plant(alias, {"include/alias_cites.h": ALIAS_FIXTURE})
        data = scan(alias).as_json()
        for key, value in {"decide_total": 1, "crucible_pre": 1, "contract_assert": 0,
                           "total_contract_cites": 1}.items():
            expect(f"the alias tree reports {key}={value}, not {data[key]}", data[key] == value)
        expect(f"the alias tree has one coprime cite, not {data['decide_per_procedure']['coprime']}",
               data["decide_per_procedure"]["coprime"] == 1)

        traps = tmp / "traps"
        _plant(traps, {"include/traps.h": TRAPS_FIXTURE})
        data = scan(traps).as_json()
        for key, value in {"crucible_pre": 1, "contract_assert": 0, "total_contract_cites": 1,
                           "decide_total": 1}.items():
            expect(f"the trap tree reports {key}={value}, not {data[key]}", data[key] == value)
        expect(f"the split cite counts for alpha: {data['decide_per_procedure']}",
               data["decide_per_procedure"] == {"alpha": 1, "beta": 0, "coprime": 0})

        zero = tmp / "zero"
        _plant(zero, {})
        data = scan(zero).as_json()
        expect("a tree with no cites reports zeros",
               all(data[name] == 0 for name in CHECKED) and not any(data["decide_per_procedure"].values()))
        expect("the summary of a tree with no cites runs", _quiet(lambda: run([], zero))[0] == 0)

        broken = tmp / "broken"
        (broken / "include").mkdir(parents=True)
        expect("a tree with no src/ stops the audit", _quiet(lambda: run(["--json"], broken))[0] == 2)
        unparsed = tmp / "unparsed"
        _plant(unparsed, {"include/broken.h": "void f() { g(1) { } }\n"})
        expect("a file that does not parse stops the audit", _quiet(lambda: run(["--json"], unparsed))[0] == 2)
    for line in failures:
        print(f"{RULE}: SELF-TEST FAILED, {line}", file=sys.stderr)
    if failures:
        return 2
    print(f"{RULE}: self-test passed, every form is counted once, the traps count nothing, --baseline writes the "
          f"baseline, and --check fails on a fall, on a short baseline and on a malformed one", file=sys.stderr)
    return 0


def main(argv: list[str]) -> int:
    """Run one mode over the tree of this repository (`guard check --rule contract-cites`).

    Args:
        argv: The words after the rule ID

    Returns:
        The exit status of run()
    """
    return run(argv, tsast.REPO_ROOT)
