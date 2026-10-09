"""value — the record class of the modules that a step meter imports.

THE COST
    CMake and ctest start a step meter for each compile, each link, each test
    and each fixture.  The step meters are `loop step`, `loop test-step` and
    `loop fixture`.  The modules that the import of a step meter adds are a
    cost of each run (the rule import-closure).  The measurements of
    2026-10-09 give the p50 of 15 runs of one import in a new interpreter
    with -S.  The import of dataclasses took 10.9 ms, enum took 2.2 ms and
    pathlib took 7.6 ms.  A step meter itself takes approximately 10 ms.  A record that a step meter builds or writes then
    derives from Value, and not from a frozen dataclass.  This module imports
    no module.

THE VALUE
    The fields of a value are the names of the __match_args__ of its class, in
    their sequence, as for a dataclass.  A subclass gives its fields to
    Value.__init__ in that sequence.  A value is equal to a value of the same
    class with equal fields, and its hash and its text come from its fields.
    No field changes after the constructor: an assignment or a deletion raises
    AttributeError, as a frozen dataclass does.  A field can hold a list or a
    dictionary.  The hash of such a value then raises TypeError, as the hash
    of a frozen dataclass does.

`guard selftest tests.core.test_value` does a test of the module.
"""

# The names are only for the types, and a step meter does not pay the import of typing.
TYPE_CHECKING = False
if TYPE_CHECKING:
    from typing import NoReturn, Self


class Value:
    """A record with a constant set of fields, equal to a record of its class with equal fields (THE VALUE)."""

    __slots__ = ()
    __match_args__: tuple[str, ...] = ()

    def __init__(self, *fields: object) -> None:
        """Give each field of the value, in the sequence of the __match_args__ of its class.

        Raises:
            TypeError: If the call gives a different number of fields than the class has
        """
        names: tuple[str, ...] = type(self).__match_args__
        if len(fields) != len(names):
            raise TypeError(f"{type(self).__name__} has {len(names)} fields, and the call gives {len(fields)}")
        for name, field in zip(names, fields, strict=True):
            object.__setattr__(self, name, field)

    def __setattr__(self, name: str, value: object) -> NoReturn:
        """Refuse each change of a field (THE VALUE).

        Raises:
            AttributeError: Always
        """
        raise AttributeError(f"the field {name} of a {type(self).__name__} does not change after its constructor")

    def __delattr__(self, name: str) -> NoReturn:
        """Refuse each deletion of a field (THE VALUE).

        Raises:
            AttributeError: Always
        """
        raise AttributeError(f"the field {name} of a {type(self).__name__} does not change after its constructor")

    def fields(self) -> tuple[object, ...]:
        """Return the fields of the value, in the sequence of the __match_args__ of its class."""
        return tuple(getattr(self, name) for name in type(self).__match_args__)

    def replaced(self, **changes: object) -> Self:
        """Return a value of the same class with the fields of `changes` replaced.

        Raises:
            TypeError: If a key of `changes` is not a field of the class
        """
        names: tuple[str, ...] = type(self).__match_args__
        unknown = sorted(set(changes) - set(names))
        if unknown:
            raise TypeError(f"{type(self).__name__} has no field {', '.join(unknown)}")
        copy = object.__new__(type(self))
        for name in names:
            object.__setattr__(copy, name, changes.get(name, getattr(self, name)))
        return copy

    def __eq__(self, other: object) -> bool:
        """Return True when `other` is a value of the same class with equal fields."""
        if not isinstance(other, Value) or type(other) is not type(self):
            return NotImplemented
        return self.fields() == other.fields()

    def __hash__(self) -> int:
        """Return the hash of the class and the fields.

        Raises:
            TypeError: If a field cannot give a hash, such as a list
        """
        return hash((type(self), self.fields()))

    def __repr__(self) -> str:
        """Return the name of the class with each field and its value."""
        shown = ", ".join(f"{name}={getattr(self, name)!r}" for name in type(self).__match_args__)
        return f"{type(self).__name__}({shown})"
