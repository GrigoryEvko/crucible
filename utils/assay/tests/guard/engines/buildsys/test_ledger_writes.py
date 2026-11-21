"""The test of the writes of the rules of a step, of a held rule and of header-constexpr-ops.

`guard selftest tests.guard.engines.buildsys.test_ledger_writes` runs the
test.  Each case makes a planted build with assay.guard.engines.buildsys.planted
and writes a ledger or a list of it with `guard write`, or with the function of
the rule.  The cases of the list compile planted headers with the patched
compiler (assay.core.testing, THE COMPILER).  Exit 0 when each case holds,
else 1.
"""

import tempfile
from collections.abc import Mapping
from dataclasses import replace
from pathlib import Path
from unittest import mock

from assay.core import git, testing
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core.paths import REPO_ROOT
from assay.guard import write as guard_write
from assay.guard.engines.buildsys import cost_budgets, header_constexpr, held, planted
from assay.guard.engines.buildsys.runner import BuildContext
from assay.tests.guard.engines.buildsys.test_planted import context_of

KIND = "x86_64-debug-asan"
# A small compile and a small link give the module of a rule of a step its inputs.  The tests and their records give
# the rows of the kind test.
RECORD_ROWS = f"""\
format 1
kind {KIND}
compile CMakeFiles/lib.dir/small.cpp.o src/small.cpp cpu=1 peak=0.1 instructions=1
link test/small_test cpu=1 peak=0.1
test slow_test | {{build}}/test/slow_test
test big_test | {{build}}/test/big_test
test fast_test | {{build}}/test/fast_test
test counted_slow_test | {{build}}/test/counted_slow_test
test heavy_test | {{build}}/test/heavy_test
test-record slow_test wall=40 cpu=1 peak=0.1
test-record big_test wall=1 cpu=1 peak=2
test-record fast_test wall=1 cpu=1 peak=0.1 instructions=1
test-record counted_slow_test wall=40 cpu=1 peak=0.1 instructions=1
test-record heavy_test wall=1 cpu=1 peak=0.1 instructions=700
test-record gone_test wall=50 cpu=1 peak=3
"""
# A ledger of step-memory with a row of the kind test of a test that the build does not register, which each write
# keeps.
KEPT_LEDGER = """\
# A planted ledger.
format 1
rule step-memory
- | test | 3.5 | gone_test | a row of a test of a different build
"""
# The fixtures of a planted build: one over the limit of the kind fixture, two under it, and one with no count.
FIXTURE_ROWS = f"""\
format 1
kind {KIND}
compile CMakeFiles/lib.dir/small.cpp.o src/small.cpp cpu=1 instructions=1
fixture neg_slow test/neg/slow.cpp user=19 system=1
fixture neg_heavy test/neg/heavy.cpp user=1 system=0 instructions=50
fixture neg_counted test/neg/counted.cpp user=19 system=1 instructions=10
fixture neg_fast test/neg/fast.cpp user=1 system=0 instructions=1
"""
PLANTS = REPO_ROOT / "utils" / "plants" / header_constexpr.OPS_RULE
LIST_HEAD = "# The planted list of the headers with a higher limit.\n"
# A build with one compile job, and the sources of the tree: the source of an object of a different build is there,
# and the source of a deleted unit is not (THE PRUNE of cost_budgets).
OBJECT_PRUNE_ROWS = f"""\
format 1
kind {KIND}
compile CMakeFiles/lib.dir/small.cpp.o small.cpp cpu=1 instructions=1 functions=_Z1fv:1024 text=1024
file small.cpp size=10
file vessel/torch/register.cpp size=10
"""
# The object rows of the prune: a configured object, an object of a different build, a unit whose source git held and
# the tree deleted, an object of a source that the build directory makes, an object of a source that a build with
# other options generates and git never held, and an output of a different shape.
CONFIGURED_OBJECT = "CMakeFiles/lib.dir/small.cpp.o"
OTHER_BUILD_OBJECT = "vessel/torch/CMakeFiles/dispatch_1.dir/register.cpp.o"
DELETED_SOURCE = "test/foundation/test_gone.cpp"
DELETED_OBJECT = "test/foundation/CMakeFiles/test_gone.dir/test_gone.cpp.o"
GENERATED_OBJECT = "gen/CMakeFiles/gen.dir/made.cpp.o"
UNGENERATED_OBJECT = "CMakeFiles/perf.dir/sense_hub_bpf_bytecode.c.o"
ODD_OBJECT = "odd.o"
# A build with the sentinels of four headers of fixy: one over the limit 6.5 MB, one under it, and two under it by less
# than the margin of the unit MB.  Mint.h includes 6.361 MB, the measure at which the prune of 2026-11-21 removed the
# row of EndpointMint.h with a margin of 2 %.  Two more headers of the tree have no sentinel (THE PRUNE of cost_budgets).
HEADER_PRUNE_ROWS = f"""\
format 1
kind {KIND}
file system.h size=1000
file include/fixy/Huge.h size=7340032
file include/fixy/Light.h size=1048576
file include/fixy/Near.h size=6763315
file include/fixy/Mint.h size=6668991
file include/fixy/Unbuilt.h size=10
compile test/layer/CMakeFiles/layer_sentinel_fixy.dir/checks/fixy/Huge.cpp.o test/layer/checks/fixy/Huge.cpp cpu=3
deps test/layer/CMakeFiles/layer_sentinel_fixy.dir/checks/fixy/Huge.cpp.o | test/layer/checks/fixy/Huge.cpp system.h \
include/fixy/Huge.h
compile test/layer/CMakeFiles/layer_sentinel_fixy.dir/checks/fixy/Light.cpp.o test/layer/checks/fixy/Light.cpp cpu=1
deps test/layer/CMakeFiles/layer_sentinel_fixy.dir/checks/fixy/Light.cpp.o | test/layer/checks/fixy/Light.cpp system.h \
include/fixy/Light.h
compile test/layer/CMakeFiles/layer_sentinel_fixy.dir/checks/fixy/Near.cpp.o test/layer/checks/fixy/Near.cpp cpu=1
deps test/layer/CMakeFiles/layer_sentinel_fixy.dir/checks/fixy/Near.cpp.o | test/layer/checks/fixy/Near.cpp system.h \
include/fixy/Near.h
compile test/layer/CMakeFiles/layer_sentinel_fixy.dir/checks/fixy/Mint.cpp.o test/layer/checks/fixy/Mint.cpp cpu=1
deps test/layer/CMakeFiles/layer_sentinel_fixy.dir/checks/fixy/Mint.cpp.o | test/layer/checks/fixy/Mint.cpp system.h \
include/fixy/Mint.h
"""


def run_write(context: BuildContext, rule_id: str, reason: str) -> tuple[int, str]:
    """Run `guard write` for one rule on the tree and the build of a context, and give its status and its output."""
    words = ["--rule", rule_id, "--root", str(context.root), "--build-dir", str(context.require_build())]
    return testing.captured(lambda: guard_write.main([*words, "--reason", reason]))


def ledger_values(context: BuildContext, rule_id: str) -> dict[tuple[str, str], tuple[float, str]]:
    """Return the value and the reason of each row of the ledger of a rule in the tree of a context, by kind and item.

    The item of a row is its entity, or its path when it has no entity.
    """
    rule = rule_registry.load().rules[rule_id]
    ledger = rule_ledger.read(rule, context.root)
    return {
        (key.kind, key.entity or key.path): (float(entry.value), entry.reason) for key, entry in ledger.base.items()
    }


def raised(action: object) -> str:
    """Return the text of the LedgerError that an action raises, or an empty text."""
    try:
        action()  # type: ignore[operator]
    except rule_ledger.LedgerError as problem:
        return str(problem)
    return ""


def record_cases(expect: testing.Expect) -> None:
    """Write the rows of the kind test of the rules of a step from the records of a planted build (THE LEDGERS)."""
    with tempfile.TemporaryDirectory(prefix="test-ledger-writes-") as scratch:
        context = context_of(Path(scratch), RECORD_ROWS)
        ledgers = rule_ledger.ledger_path("step-memory", context.root).parent
        ledgers.mkdir(parents=True)
        rule_ledger.ledger_path("step-memory", context.root).write_text(KEPT_LEDGER, encoding="utf-8")
        registry = rule_registry.load()
        no_rows = rule_ledger.empty(registry.rules["step-memory"])
        expect(
            "the write of step-memory refuses a new row with no reason, and names --reason",
            "--reason" in raised(lambda: cost_budgets.budget_written("step-memory", context, no_rows, None)),
        )
        status, said = run_write(context, "step-memory", "a planted reason")
        expect(
            "guard write writes the ledger of step-memory: a row of the kind test with the peak GB of each "
            "registered test over the limit, and the row of a test of a different build stays",
            status == 0
            and ledger_values(context, "step-memory")
            == {
                ("test", "big_test"): (2.0, "a planted reason"),
                ("test", "gone_test"): (3.5, "a row of a test of a different build"),
            },
        )
        expect("the write tells the ledger that it writes", "step-memory.txt holds 2 rows" in said)
        status, said = run_write(context, "step-cost", "a planted reason")
        expect(
            "guard write writes the ledger of step-cost: a row of the kind test with the count in G of each "
            "registered test over the limit, and no row of a test whose record holds no count",
            status == 0
            and ledger_values(context, "step-cost") == {("test", "heavy_test"): (700.0, "a planted reason")}
            and "step-cost.txt holds 1 rows" in said,
        )
        run_write(context, "step-memory", "a different reason")
        expect(
            "a second write keeps the reason of a row that keeps its test",
            ledger_values(context, "step-memory")[("test", "big_test")] == (2.0, "a planted reason"),
        )
        reason_words = ["--rule", "step-memory", "--root", str(context.root), "--row", "big_test", "--kind", "test"]
        status, said = testing.captured(lambda: guard_write.main([*reason_words, "--reason", "a corrected reason"]))
        expect(
            "guard write --row writes the reason of a kept row of a max ledger with no build, and changes no value "
            "and no other row",
            status == 0
            and ledger_values(context, "step-memory")
            == {
                ("test", "big_test"): (2.0, "a corrected reason"),
                ("test", "gone_test"): (3.5, "a row of a test of a different build"),
            },
        )
        unknown_words = ["--rule", "step-memory", "--root", str(context.root), "--row", "no_such_test"]
        status, said = testing.captured(lambda: guard_write.main([*unknown_words, "--reason", "a reason"]))
        expect(
            "guard write --row refuses an item that names no row, with status 1, and the ledger stays",
            status == 1
            and "no_such_test" in said
            and ledger_values(context, "step-memory")[("test", "big_test")] == (2.0, "a corrected reason"),
        )
        status, said = testing.captured(lambda: guard_write.main([*reason_words, "--raise", "--reason", "a reason"]))
        expect("guard write --row refuses a mode, with status 2", status == 2)
        held_rule = rule_registry.planted_rules([rule_registry.measured_rule("planted-held", 2, "s")])["planted-held"]
        expect(
            "written() of held refuses each held rule, because no function writes its ledger",
            "no function writes its ledger"
            in raised(lambda: held.written(held_rule, context, rule_ledger.empty(held_rule), None)),
        )


def fixture_cases(expect: testing.Expect) -> None:
    """Write the rows of the kind fixture of step-cost from the records of the fixtures of a planted build."""
    with tempfile.TemporaryDirectory(prefix="test-ledger-writes-") as scratch:
        context = context_of(Path(scratch), FIXTURE_ROWS)
        empty = rule_ledger.empty(rule_registry.load().rules["step-cost"])
        expect(
            "a new row of a fixture with no reason is refused, and the message names --reason",
            "--reason" in raised(lambda: cost_budgets.budget_written("step-cost", context, empty, None)),
        )
        written = cost_budgets.budget_written("step-cost", context, empty, "a planted reason")
        values = {(key.kind, key.entity): (entry.value, entry.reason) for key, entry in written.base.items()}
        expect(
            "step-cost writes a row of the kind fixture for the fixture over 45 G, and no row for a fixture with no "
            "count",
            values == {("fixture", "neg_heavy"): (50.0, "a planted reason")},
        )
        again = cost_budgets.budget_written("step-cost", context, written, "a different reason")
        expect("a second write keeps the row of a fixture that the row keeps", again.base == written.base)
        gone_key = rule_ledger.Key("", "fixture", "neg_gone")
        with_gone = replace(written, base={**written.base, gone_key: rule_ledger.Entry(gone_key, 60.0, "a row")})
        kept = cost_budgets.budget_written("step-cost", context, with_gone, None)
        expect("a write from a build with no such fixture keeps its row", gone_key in kept.base)
    with tempfile.TemporaryDirectory(prefix="test-ledger-writes-") as scratch:
        context = context_of(Path(scratch), FIXTURE_ROWS + "fixture neg_unrun test/neg/unrun.cpp record=no\n")
        unrun_key = rule_ledger.Key("", "fixture", "neg_unrun")
        with_unrun = replace(empty, base={unrun_key: rule_ledger.Entry(unrun_key, 70.0, "a row of a fixture")})
        partial, said = testing.captured(
            lambda: cost_budgets.budget_written("step-cost", context, with_unrun, "a planted reason")
        )
        expect(
            "a write from a build with a fixture that has no record names the count of such fixtures, writes the "
            "rows of the fixtures with a record, and keeps the row of the fixture with no record",
            "1 fixtures have no record that counts, such as neg_unrun" in said
            and partial.base.get(unrun_key) == with_unrun.base[unrun_key]
            and rule_ledger.Key("", "fixture", "neg_heavy") in partial.base,
        )


def planted_ledger(rule_id: str, rows: Mapping[rule_ledger.Key, float]) -> rule_ledger.Ledger:
    """Return a ledger of a rule of the registry with one row for each key, each with its value and a planted reason."""
    empty = rule_ledger.empty(rule_registry.load().rules[rule_id])
    return replace(empty, base={key: rule_ledger.Entry(key, value, "a planted reason") for key, value in rows.items()})


def output_key(item: str, kind: str) -> rule_ledger.Key:
    """Return the key of the ledger row of an output of the build."""
    return rule_ledger.Key(f"@build/{item}", kind, "")


def object_prune_cases(expect: testing.Expect) -> None:
    """Prune the object rows that keep nothing in each build, and keep each other row (THE PRUNE of cost_budgets)."""
    with tempfile.TemporaryDirectory(prefix="test-ledger-writes-") as scratch:
        context = context_of(Path(scratch), OBJECT_PRUNE_ROWS)
        made = context.require_build() / "gen" / "made.cpp"
        made.parent.mkdir(parents=True)
        made.write_text("int made;\n", encoding="utf-8")
        deleted = context.root / DELETED_SOURCE
        deleted.parent.mkdir(parents=True)
        deleted.write_text("int gone;\n", encoding="utf-8")
        git.run(context.root, "add", "--", DELETED_SOURCE)
        git.run(context.root, "commit", "-q", "-m", "Add a unit that the tree deletes", "--", DELETED_SOURCE)
        deleted.unlink()
        items = (
            CONFIGURED_OBJECT,
            OTHER_BUILD_OBJECT,
            DELETED_OBJECT,
            GENERATED_OBJECT,
            UNGENERATED_OBJECT,
            ODD_OBJECT,
        )
        sized = planted_ledger("object-size", {output_key(item, "object"): 900.0 for item in items})
        written = cost_budgets.budget_written("object-size", context, sized, None)
        expect(
            "the write of object-size removes the row of a unit whose source git held and the tree deleted, and "
            "keeps the row of a configured object, of an object whose source the tree or the build holds, of a "
            "source that git never held, which a build with other options can generate, and of an output of a "
            "different shape",
            set(written.base) == {output_key(item, "object") for item in items if item != DELETED_OBJECT},
        )
        expect(
            "the write lowers no row of an object, because its size changes with the build",
            written.base[output_key(CONFIGURED_OBJECT, "object")].value == 900.0,
        )
        counted = planted_ledger(
            "step-cost",
            {output_key(DELETED_OBJECT, "compile"): 150.0, output_key(OTHER_BUILD_OBJECT, "compile"): 120.0},
        )
        pruned = cost_budgets.budget_written("step-cost", context, counted, None)
        expect(
            "the write of step-cost removes the compile row of a deleted unit, and keeps the compile row of a unit of "
            "a different build",
            set(pruned.base) == {output_key(OTHER_BUILD_OBJECT, "compile")},
        )
        expect(
            "object_sources() gives the two places of the source of an object path, with '..' for each part '__', "
            "and no place for a path of a different shape",
            cost_budgets.object_sources("a/CMakeFiles/t.dir/__/b/c.cpp.o", Path("/root"), Path("/build"))
            == (Path("/root/b/c.cpp"), Path("/build/b/c.cpp"))
            and cost_budgets.object_sources("CMakeFiles/t.dir/c.cpp.o", Path("/root"), Path("/build"))
            == (Path("/root/c.cpp"), Path("/build/c.cpp"))
            and cost_budgets.object_sources("lib/libx.so", Path("/root"), Path("/build")) == ()
            and cost_budgets.object_sources("CMakeFiles/t/c.cpp.o", Path("/root"), Path("/build")) == (),
        )


def header_prune_cases(expect: testing.Expect) -> None:
    """Prune the rows of header-alone with the measure of one build (THE PRUNE of cost_budgets)."""
    with tempfile.TemporaryDirectory(prefix="test-ledger-writes-") as scratch:
        context = context_of(Path(scratch), HEADER_PRUNE_ROWS)
        headers = ("Huge.h", "Light.h", "Near.h", "Mint.h", "Gone.h", "Unbuilt.h")
        keys = {name: rule_ledger.Key(f"include/fixy/{name}", "fixy", "") for name in headers}
        rows = planted_ledger(
            "header-alone",
            {
                keys["Huge.h"]: 7.5,
                keys["Light.h"]: 7.0,
                keys["Near.h"]: 7.0,
                keys["Mint.h"]: 6.501,
                keys["Gone.h"]: 7.0,
            },
        )
        rows = replace(rows, base={**rows.base, keys["Unbuilt.h"]: rule_ledger.Entry(keys["Unbuilt.h"], 7.0, "kept")})
        written = cost_budgets.budget_written("header-alone", context, rows, None)
        expect(
            "the write of header-alone lowers the row of a header over the limit to its measure, removes the row of "
            "a header under the limit by more than the margin of 5 % and of a header that the tree does not hold, "
            "keeps the row of a header under the limit by less than the margin at its measure, and keeps the row of "
            "a header with no sentinel in the build",
            {key.path: entry.value for key, entry in written.base.items()}
            == {
                "include/fixy/Huge.h": 7.001,
                "include/fixy/Near.h": 6.451,
                "include/fixy/Mint.h": 6.361,
                "include/fixy/Unbuilt.h": 7.0,
            },
        )
        expect(
            "the row of a header at 6.361 MB, the measure at which the margin of 2 % removed the row of "
            "EndpointMint.h on 2026-11-21, stays, so the next small growth over the limit 6.5 MB has a row",
            keys["Mint.h"] in written.base,
        )
        expect(
            "a lowered row keeps its reason",
            written.base[keys["Huge.h"]].reason == "a planted reason",
        )


# Two builds of one tree for a lower (THE LOWER of cost_budgets): the compile of scan.cpp ran 111 G in the first build
# and is a ccache hit of a compile of 112 G in the second, light.cpp stays under the limit 110 G with the margin, and the
# second build gives the object blind.cpp.o as a hit with no cost in the cost store.
LOWER_FIRST = f"""\
format 1
kind {KIND}
compile CMakeFiles/lib.dir/scan.cpp.o src/scan.cpp cpu=1 instructions=111 functions=_Z1fv:1024 text=802816
compile CMakeFiles/lib.dir/light.cpp.o src/light.cpp cpu=1 instructions=100
compile CMakeFiles/lib.dir/blind.cpp.o src/blind.cpp cpu=1 instructions=130
"""
LOWER_SECOND = f"""\
format 1
kind {KIND}
compile CMakeFiles/lib.dir/scan.cpp.o src/scan.cpp result=hit last-cpu=1 last-instructions=112
compile CMakeFiles/lib.dir/light.cpp.o src/light.cpp cpu=1 instructions=101
compile CMakeFiles/lib.dir/blind.cpp.o src/blind.cpp result=hit
"""


def lower_cases(expect: testing.Expect) -> None:
    """Lower named rows to the largest measure over the builds, and refuse each lower that THE LOWER refuses."""
    with tempfile.TemporaryDirectory(prefix="test-ledger-lower-") as scratch:
        first = context_of(Path(scratch) / "first", LOWER_FIRST)
        second = context_of(Path(scratch) / "second", LOWER_SECOND)
        scan, light, blind = (
            output_key(f"CMakeFiles/lib.dir/{name}.cpp.o", "compile") for name in ("scan", "light", "blind")
        )
        rows = planted_ledger("step-cost", {scan: 123.912, light: 120.0, blind: 140.0})
        cut = cost_budgets.LowerRequest(
            ("CMakeFiles/lib.dir/scan.cpp.o", "CMakeFiles/lib.dir/light.cpp.o"), "", "a cut"
        )
        lowered, lines = cost_budgets.lowered_ledger("step-cost", [first, second], rows, cut)
        expect(
            "a lower sets a named row to the largest measure over the builds, also the measure of a ccache hit from "
            "the cost store, with the reason of the cut, removes a row whose measure with the margin is under the "
            "limit, and keeps each other row",
            {key: (entry.value, entry.reason) for key, entry in lowered.base.items()}
            == {scan: (112.0, "a cut"), blind: (140.0, "a planted reason")}
            and lines
            == [
                "CMakeFiles/lib.dir/light.cpp.o of the kind compile: 120 G instructions removed, at 101 G instructions",
                "CMakeFiles/lib.dir/scan.cpp.o of the kind compile: 123.912 -> 112 G instructions",
            ],
        )
        refusals = {
            "an item that names no row": cost_budgets.LowerRequest(("CMakeFiles/lib.dir/none.cpp.o",), "", "r"),
            "a hit with no cost in the cost store": cost_budgets.LowerRequest(
                ("CMakeFiles/lib.dir/blind.cpp.o",), "", "r"
            ),
            "a kind that names no row of the item": cost_budgets.LowerRequest(
                ("CMakeFiles/lib.dir/scan.cpp.o",), "test", "r"
            ),
        }
        for what, request in refusals.items():
            expect(
                f"a lower refuses {what}",
                bool(
                    raised(
                        lambda request=request: cost_budgets.lowered_ledger("step-cost", [first, second], rows, request)
                    )
                ),
            )
        low = planted_ledger("step-cost", {scan: 111.5, light: 120.0})
        kept, kept_lines = cost_budgets.lowered_ledger("step-cost", [first, second], low, cut)
        expect(
            "a lower keeps a row below the largest measure with a line, and still lowers the other named rows",
            {key: entry.value for key, entry in kept.base.items()} == {scan: 111.5}
            and "scan.cpp.o of the kind compile: 111.5 G instructions stays" in " ".join(kept_lines),
        )
        test_key = rule_ledger.Key("", "test", "heavy_test")
        tested = replace(rows, base={**rows.base, test_key: rule_ledger.Entry(test_key, 700.0, "a row")})
        kind_refusal = raised(
            lambda: cost_budgets.lowered_ledger(
                "step-cost", [first, second], tested, cost_budgets.LowerRequest(("heavy_test",), "test", "r")
            )
        )
        expect(
            "a lower refuses a row of the kind test, which has a record only after its run", "kind test" in kind_refusal
        )
        sized = planted_ledger("object-size", {output_key("CMakeFiles/lib.dir/scan.cpp.o", "object"): 852.123})
        objects, _ = cost_budgets.lowered_ledger(
            "object-size",
            [first],
            sized,
            cost_budgets.LowerRequest(("CMakeFiles/lib.dir/scan.cpp.o",), "object", "a cut"),
        )
        expect(
            "a lower of object-size sets the row of the object to its machine code",
            [entry.value for entry in objects.base.values()] == [785.0],
        )
        root = first.root
        rule_ledger.store(rows, root)
        step_cost = rule_registry.load().rules["step-cost"]
        words = ["--rule", "step-cost", "--root", str(root), "--row", "CMakeFiles/lib.dir/scan.cpp.o", "--lower"]
        builds = ["--build-dir", str(first.require_build()), "--build-dir", str(second.require_build())]
        status, said = testing.captured(lambda: guard_write.main([*words, "--reason", "a cut", *builds]))
        refusal = guard_write.lower_refusal(REPO_ROOT, [first.require_build(), second.require_build()])
        expect(
            "guard write --lower refuses builds that are not one build of each configuration of objects of the gate, "
            "and the ledger stays",
            status == 2
            and "is not written" in said
            and "one build of each configuration of objects of the gate" in refusal
            and rule_ledger.read(step_cost, root).base[scan].value == 123.912,
        )
        status, said = testing.captured(lambda: guard_write.main([*words, *builds]))
        expect("guard write --lower refuses a call with no reason", status == 2 and "--reason" in said)
        with mock.patch.object(guard_write, "lower_refusal", lambda _root, _builds: ""):
            status, said = testing.captured(lambda: guard_write.main([*words, "--reason", "a cut", *builds]))
        expect(
            "guard write --lower on builds of each configuration writes the named row and tells its old and new value",
            status == 0
            and "scan.cpp.o of the kind compile: 123.912 -> 112 G instructions" in said
            and rule_ledger.read(step_cost, root).base[scan].value == 112.0,
        )


def list_text(context: BuildContext) -> str:
    """Return the text of the list of header-constexpr-ops in the tree of a context."""
    return (context.root / header_constexpr.LIST).read_text(encoding="utf-8")


def list_cases(expect: testing.Expect) -> None:
    """Remove the rows that no header makes necessary from a list with no compile, through `guard write` (THE WRITE)."""
    with tempfile.TemporaryDirectory(prefix="test-ledger-writes-") as scratch:
        context = context_of(Path(scratch), f"format 1\nkind {KIND}\n")
        header = context.root / header_constexpr.INCLUDE / "crucible" / "Light.h"
        header.parent.mkdir(parents=True)
        header.write_text("#pragma once\n", encoding="utf-8")
        listed = context.root / header_constexpr.LIST
        listed.parent.mkdir(parents=True)
        listed.write_text(
            f"{LIST_HEAD}crucible/Gone.h | 2000000 | a header that is not there\n"
            "crucible/Light.h | 1000 | a limit at the threshold of the registry or below it\n",
            encoding="utf-8",
        )
        status, said = run_write(context, header_constexpr.OPS_RULE, "unused")
        expect(
            "guard write removes a row of a header that is not in include/ and a row with no limit over the threshold, "
            "and it keeps the head comment",
            status == 0 and list_text(context) == LIST_HEAD and "holds 0 rows, and the write removed 2" in said,
        )
        listed.write_text(f"{LIST_HEAD}crucible/Light.h | 2000000\n", encoding="utf-8")
        status, said = run_write(context, header_constexpr.OPS_RULE, "unused")
        expect(
            "guard write refuses a row with no reason, with status 1, and the list stays",
            status == 1
            and "is not written" in said
            and list_text(context) == f"{LIST_HEAD}crucible/Light.h | 2000000\n",
        )


def compile_cases(expect: testing.Expect) -> None:
    """Remove each row of a header that compiles alone at the threshold, on copies of the plants of the rule."""
    compiler = testing.patched_compiler()
    expect(
        "ASSAY_TEST_CXX or CRUCIBLE_GCC16_PREFIX names the patched compiler",
        bool(compiler) and Path(compiler).is_file(),
    )
    if not compiler or not Path(compiler).is_file():
        return
    with tempfile.TemporaryDirectory(prefix="test-ledger-writes-") as scratch:
        listed = planted.plant_context(PLANTS / "control-listed", Path(scratch) / "listed")
        before = list_text(listed)
        expect(
            "the write keeps each row of a header that does not compile alone at the threshold, and the list stays",
            "holds 2 rows, and the write removed 0" in header_constexpr.ops_refresh(listed)
            and list_text(listed) == before,
        )
        unneeded = planted.plant_context(PLANTS / "plant-unneeded", Path(scratch) / "unneeded")
        expect(
            "the write removes the row of a header that compiles alone at the threshold",
            "holds 0 rows, and the write removed 1" in header_constexpr.ops_refresh(unneeded)
            and list_text(unneeded) == LIST_HEAD,
        )
        twice = planted.plant_context(PLANTS / "plant-list-duplicate", Path(scratch) / "twice")
        expect(
            "the write removes the second row of a header, and it keeps the first row",
            "holds 1 rows, and the write removed 1" in header_constexpr.ops_refresh(twice)
            and list_text(twice) == f"{LIST_HEAD}crucible/Heavy.h | 100000 | spin(2000) at namespace scope\n",
        )
        shape = planted.plant_context(PLANTS / "plant-list-shape", Path(scratch) / "shape")
        expect(
            "the write refuses a row that does not have its shape",
            "does not have its shape" in raised(lambda: header_constexpr.ops_refresh(shape)),
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(
        __name__.removeprefix("assay."),
        argv,
        (record_cases, fixture_cases, object_prune_cases, header_prune_cases, lower_cases, list_cases, compile_cases),
    )
