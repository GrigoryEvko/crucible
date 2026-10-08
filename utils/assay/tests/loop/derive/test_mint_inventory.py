"""The test of the mint inventory (assay.loop.derive.mint_inventory), on planted mint records and fixtures.

`guard selftest tests.loop.derive.test_mint_inventory` runs the test.  A
case writes a planted registry, a rule table, a ledger of the rule
mint-pattern and the fixture directories into a scratch tree.  It gives
planted mint records to a context of the engine plugin with a planted
census.  The fixtures parse with the pinned tree-sitter kit.  The cases hold:
  * The layer and the section of a header path
  * The fixture directories of each layer, and the files that are not fixtures
  * The count of the fixtures of a member mint of a name that two classes have
  * The rows of the records: the count of each mint, the carve-out of the
    ledger of mint-pattern and the label of an overload
  * The finding of drift() before and after a write of the inventory
  * The command line of main.
Exit 0 when each case holds, else 1.
"""

import functools
import tempfile
from pathlib import Path

from assay.core import registry as rule_registry
from assay.core import testing
from assay.core.exit import ExitCode
from assay.guard.engines.plugin import declarations, runner
from assay.loop.derive import mint_inventory as inventory
from assay.readers import sections as plugin_sections

RULE = declarations.MINT_RULE
REGISTRY_ROWS = (
    (
        "rule mint-pattern engine=plugin scope=include/ ledger=admit kinds=mint,mint-alias,mint-not-constexpr "
        "doc=XXI | a planted mint rule | give the mint its shape"
    ),
)
TABLE_TEXT = "layer base 0 include/fixy/\nquarantine include/crucible/\nquarantine test/\n"
FACTS = "static=0 template=0 nodiscard=1 noexcept=1 constrained=0 gated=0 projection=0 defined=1"
# The ledger of mint-pattern of the tree: one row that admits a mint that is not constexpr.
LEDGER = (
    "format 1\nrule mint-pattern\n"
    "include/fixy/A.h | mint-not-constexpr | admit | fixy::mint_cell(int) | the mint allocates\n"
)
# Each fixture: its path, and the code that it spells.
FIXTURES = (
    ("test/fixy/neg/neg_cell_first.cpp", "int main() { return fixy::mint_cell(0); }\n"),
    ("test/fixy/neg/neg_cell_second.cpp", "int main() { return fixy::mint_cell(1); }\n"),
    ("test/fixy/neg/neg_holder.cpp", "int main() { return Holder{}.mint_member(); }\n"),
    ("test/fixy/neg/neg_kept.cpp", "int main() { return Kept{}.mint_member(); }\n"),
    ("test/fixy/neg/neg_unnamed.cpp", "int main(auto any) { return any.mint_member(); }\n"),
    ("test/fixy/neg/neg_comment.cpp", "// mint_lonely is in a comment.\nint main() { return 0; }\n"),
    ("test/forge_neg/neg_lonely.cpp", "int main() { return mint_lonely(); }\n"),
    ("test/fixy/notes/neg_lonely.cpp", "int main() { return mint_lonely(); }\n"),
    ("test/fixy/neg/neg_lonely.txt", "mint_lonely\n"),
)


def record(
    entity: str, name: str, shape: str = "token", constexpr: int = 1, class_name: str = "-", owner: str = "-"
) -> plugin_sections.Finding:
    """Return a planted mint record of include/fixy/A.h with the facts of FACTS."""
    facts = f"name={name} shape={shape} constexpr={constexpr} {FACTS} class={class_name} owner={owner}"
    return plugin_sections.Finding(
        "include/fixy/A.h", len(entity), 1, RULE, declarations.MINT_RECORD, f"{entity} | {facts}"
    )


RECORDS = (
    record("fixy::mint_cell(int)", "mint_cell", constexpr=0),
    record("fixy::mint_cell(int, int)", "mint_cell"),
    record("fixy::Holder::mint_member()", "mint_member", "member", class_name="Holder", owner="fixy::Holder"),
    record("fixy::Keeper::mint_member()", "mint_member", "member", class_name="Keeper", owner="fixy::Keeper"),
    record("fixy::mint_lonely()", "mint_lonely"),
    plugin_sections.Finding(
        "include/fixy/A.h", 40, 1, RULE, declarations.ALIAS_RECORD, "fixy::Kept | alias=Kept owner=fixy::Keeper"
    ),
)


def scratch_tree(root: Path) -> None:
    """Write the planted registry, rule table, ledger of mint-pattern and fixtures into a scratch tree."""
    (root / "utils" / "ledgers").mkdir(parents=True)
    (root / rule_registry.REGISTRY_PATH).write_text(rule_registry.planted(REGISTRY_ROWS), encoding="utf-8")
    (root / declarations.TABLE).write_text(TABLE_TEXT, encoding="utf-8")
    (root / "utils" / "ledgers" / "mint-pattern.txt").write_text(LEDGER, encoding="utf-8")
    for path, text in FIXTURES:
        (root / path).parent.mkdir(parents=True, exist_ok=True)
        (root / path).write_text(text, encoding="utf-8")


def context_of(root: Path) -> runner.PluginContext:
    """Return a context of the scratch tree with a planted census of RECORDS."""
    return runner.PluginContext(root, census=runner.Census(1, frozenset(RECORDS)))


def layer_cases(expect: testing.Expect) -> None:
    """Hold the layer and the section of a header path."""
    expect(
        "a header of include/fixy/ is of the substrate layer", inventory.family_of("include/fixy/A.h") == "substrate"
    )
    expect(
        "a header of include/crucible/ is of the crucible layer",
        inventory.family_of("include/crucible/B.h") == "crucible",
    )
    expect(
        "the section of a deep header is its root and first directory",
        inventory.group_of("include/crucible/cntp/Pacing.h") == "include/crucible/cntp/",
    )
    expect(
        "the section of a header at the root of a layer is the root",
        inventory.group_of("include/fixy/A.h") == "include/fixy/",
    )


def fixture_cases(expect: testing.Expect) -> None:
    """Hold the fixture directories of each layer, and the files that are not fixtures."""
    with tempfile.TemporaryDirectory(prefix="mint-inventory-") as scratch_text:
        root = Path(scratch_text)
        scratch_tree(root)
        files = inventory.fixture_files(root)
        expect(
            "a directory neg of test/fixy/ holds fixtures of the substrate layer",
            Path("test/fixy/neg/neg_cell_first.cpp") in files[inventory.SUBSTRATE],
        )
        expect(
            "a directory NAME_neg holds fixtures of the crucible layer",
            files[inventory.CRUCIBLE] == [Path("test/forge_neg/neg_lonely.cpp")],
        )
        expect(
            "a directory with another name and a file with another suffix hold no fixture",
            all(path.parent.name == "neg" and path.suffix == ".cpp" for path in files[inventory.SUBSTRATE]),
        )


def count_cases(expect: testing.Expect) -> None:
    """Hold the count of a member mint of a name that two classes have, through its class and an alias of it."""
    names = [frozenset({"Holder", "mint_member"}), frozenset({"Kept", "mint_member"}), frozenset({"mint_member"})]
    keeper = declarations.Mint(
        entity="fixy::Keeper::mint_member()",
        path="include/fixy/A.h",
        line=1,
        name="mint_member",
        shape="member",
        class_name="Keeper",
        owner="fixy::Keeper",
        is_static=False,
        is_template=False,
        is_nodiscard=True,
        is_constexpr=True,
        is_noexcept=True,
        is_constrained=False,
        is_gated=False,
        is_projection=False,
        is_defined=True,
    )
    aliases = {"fixy::Keeper": frozenset({"Kept"})}
    expect(
        "a fixture counts for the class of a mint through an alias of the class",
        inventory.fixture_count(keeper, 2, names, aliases) == 1,
    )
    expect(
        "with one class that carries the name, each fixture that names the mint counts",
        inventory.fixture_count(keeper, 1, names, aliases) == 3,
    )


def row_cases(expect: testing.Expect) -> None:
    """Hold the rows of the records, the render and the finding of drift()."""
    with tempfile.TemporaryDirectory(prefix="mint-inventory-") as scratch_text:
        root = Path(scratch_text)
        scratch_tree(root)
        context = context_of(root)
        rows = {row.mint.entity: row for row in inventory.rows_of(context)}
        expect("each mint record gives one row", len(rows) == 5)
        expect("a mint named by two fixtures of its layer has the count two", rows["fixy::mint_cell(int)"].hs14 == 2)
        expect(
            "a fixture of a different layer, a comment and a file outside a fixture directory do not count",
            rows["fixy::mint_lonely()"].hs14 == 0,
        )
        expect(
            "the classes Holder and Keeper count only the fixtures that name them or their alias",
            rows["fixy::Holder::mint_member()"].hs14 == 1 and rows["fixy::Keeper::mint_member()"].hs14 == 1,
        )
        expect(
            "a row of the kind mint-not-constexpr of the ledger of mint-pattern carves the cx cell",
            rows["fixy::mint_cell(int)"].is_cx_carved and "- (alloc)" in inventory.cells(rows["fixy::mint_cell(int)"]),
        )
        expect(
            "an overload names its parameter types, and a member mint names its class",
            rows["fixy::mint_cell(int, int)"].label == "mint_cell(int, int)"
            and rows["fixy::Holder::mint_member()"].label == "Holder::mint_member",
        )
        found = inventory.drift(context)
        expect(
            "drift() gives one error when the tree has no inventory",
            [(item.level, item.path) for item in found] == [("error", inventory.INVENTORY)],
        )
        (root / "misc").mkdir()
        (root / inventory.INVENTORY).write_text(inventory.render(inventory.rows_of(context)), encoding="utf-8")
        expect("drift() gives no finding after a write of the inventory", inventory.drift(context) == [])
        expect(
            "two renders of one census are equal",
            inventory.render(inventory.rows_of(context)) == inventory.render(inventory.rows_of(context)),
        )


# The command lines that main refuses, each with the words of its case.
USAGE_ERRORS = (
    ("a call with no mode", ()),
    ("two modes", ("--write", "--check")),
    ("an unknown word", ("--check-floor",)),
)


def command_cases(expect: testing.Expect) -> None:
    """Hold the usage errors of main (USAGE_ERRORS), and a build directory that holds no build."""
    for label, words in USAGE_ERRORS:
        status, _text = testing.captured(functools.partial(inventory.main, list(words)))
        expect(f"main refuses {label}", status == ExitCode.USAGE)
    with tempfile.TemporaryDirectory(prefix="mint-inventory-") as scratch_text:
        status, _text = testing.captured(functools.partial(inventory.main, ["--check", "--build-dir", scratch_text]))
        expect("main --check with a directory that holds no build is a missing input", status == ExitCode.MISSING_INPUT)


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(
        __name__.removeprefix("assay."), argv, (layer_cases, fixture_cases, count_cases, row_cases, command_cases)
    )
