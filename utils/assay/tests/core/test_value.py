"""The test of the record class of the modules that a step meter imports (assay.core.value).

`guard selftest tests.core.test_value` runs the test.  Each case builds a
planted subclass of Value.  Exit 0 when each case holds, else 1.
"""

from assay.core import testing
from assay.core.value import Value


class Pair(Value):
    """A planted value with two fields."""

    __match_args__ = ("name", "count")
    name: str
    count: int

    def __init__(self, name: str, count: int) -> None:
        """Hold the two fields."""
        super().__init__(name, count)


class Other(Value):
    """A planted value of a different class with the same fields."""

    __match_args__ = ("name", "count")
    name: str
    count: int

    def __init__(self, name: str, count: int) -> None:
        """Hold the two fields."""
        super().__init__(name, count)


def raises(action: object, error: type[Exception]) -> bool:
    """Return True when the call raises the error."""
    try:
        action()  # type: ignore[operator]
    except error:
        return True
    return False


def value_cases(expect: testing.Expect) -> None:
    """Check the fields, the equality, the hash, the text and the refusal of a change (THE VALUE)."""
    first = Pair("a", 1)
    expect(
        "a value gives its fields by their names and in the sequence of the __match_args__ of its class",
        first.name == "a" and first.count == 1 and first.fields() == ("a", 1),
    )
    expect(
        "a value is equal to a value of its class with equal fields, and its hash agrees",
        first == Pair("a", 1) and hash(first) == hash(Pair("a", 1)) and first != Pair("a", 2),
    )
    expect("a value is not equal to a value of a different class with the same fields", first != Other("a", 1))
    expect("the text of a value names its class and each field", repr(first) == "Pair(name='a', count=1)")
    expect("an assignment to a field raises AttributeError", raises(lambda: setattr(first, "count", 2), AttributeError))
    expect("a deletion of a field raises AttributeError", raises(lambda: delattr(first, "count"), AttributeError))
    expect(
        "replaced() gives a copy with the changed fields, and the value itself stays",
        first.replaced(count=5) == Pair("a", 5) and first.count == 1,
    )
    expect("replaced() refuses a name that is not a field", raises(lambda: first.replaced(size=2), TypeError))
    expect(
        "a call with the wrong number of fields raises TypeError", raises(lambda: Value.__init__(first, "a"), TypeError)
    )
    unhashable = Pair("a", [1])  # type: ignore[arg-type]
    expect(
        "the hash of a value with a list field raises TypeError, as a frozen dataclass does",
        raises(lambda: hash(unhashable), TypeError),
    )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (value_cases,))
