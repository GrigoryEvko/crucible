"""A planted record with no slots, through an alias of the module dataclasses."""

import dataclasses as records


@records.dataclass(frozen=True)
class Record:
    """A frozen record with no slots=True."""

    name: str


@records.dataclass
class Bare:
    """A record with the decorator and no argument: no frozen=True and no slots=True."""

    count: int
