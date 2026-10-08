"""The test of the table of the STE dictionary (assay.guard.engines.meta.ste_dictionary): the table and the shapes of
a word.

`guard selftest tests.guard.engines.meta.test_ste_dictionary` runs the test.
Each case writes one line.  Exit 0 when each case holds, else 1.
"""

from assay.core import testing
from assay.guard.engines.meta import ste_dictionary

NONE: frozenset[str] = frozenset()


def table_cases(expect: testing.Expect) -> None:
    """Check the rows of the table."""
    table = ste_dictionary.REFUSED
    expect(
        "each word of the table is lowercase, with letters and hyphens only",
        all(word == word.lower() and word.replace("-", "").isalpha() for word in table),
    )
    expect(
        "each word has one or more word classes and an alternative",
        all(classes.split() and alternative for classes, alternative in table.values()),
    )
    expect(
        "the table gives the alternative of a refused word",
        table["ensure"][1] == "MAKE SURE (v)" and table["utilize"][1] == "USE (v)",
    )
    expect(
        "the table holds no approved word",
        not any(word in table for word in ("use", "make", "test", "check", "damage", "function")),
    )
    expect("no word of CONFUSABLE is a word of the table", not ste_dictionary.CONFUSABLE & set(table))


def shape_cases(expect: testing.Expect) -> None:
    """Check the shapes of a word and the words of the glossary."""
    base = ste_dictionary.refused_base
    expect("a word of the table is its own base", base("ensure", NONE) == "ensure")
    expect(
        "a plural or a third person gives the base",
        base("ensures", NONE) == "ensure" and base("utilizes", NONE) == "utilize",
    )
    expect("-ies gives the base with y", base("certifies", NONE) == "certify")
    expect(
        "a verb shape with -d, -ed, -ied or -ing gives the base",
        base("ensured", NONE) == "ensure"
        and base("ensuring", NONE) == "ensure"
        and base("certified", NONE) == "certify"
        and base("performed", NONE) == "perform",
    )
    expect("a verb shape with the last consonant two times gives the base", base("admitted", NONE) == "admit")
    expect("a verb suffix on a word that is not a verb gives no base", base("majored", NONE) is None)
    expect("an approved word gives None", base("use", NONE) is None and base("removed", NONE) is None)
    expect("a word of CONFUSABLE gives None", all(base(word, NONE) is None for word in ste_dictionary.CONFUSABLE))
    glossary = frozenset({"ensure"})
    expect(
        "a word of the glossary gives None for each of its shapes",
        all(base(word, glossary) is None for word in ("ensure", "ensures", "ensured", "ensuring")),
    )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (table_cases, shape_cases))
