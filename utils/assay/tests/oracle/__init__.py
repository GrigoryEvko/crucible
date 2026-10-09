"""assay.tests.oracle — the tests of the session oracle: its sources, its unit store and its emitted tests.

THE PARTS
    test/CMakeLists.txt gives the group oracle of `guard selftest`
    SESSION_ORACLE_SELF_TEST_PARTS parts, and each part is one test.  Each
    part holds one kind of work.  A part then takes less than the warning
    threshold of test-time, with or without the results of the unit store:
      CHECK_PART   the checks of test_runner that compile nothing: the type
                   grammar of the emitter and the drift of the committed files.
                   It also removes the expired sources of the unit store
      STORE_PART   the cases of the store in test_unit_store
      ROOTS_PART   the cases of the spelled roots in test_unit_store
      each other   the units of test_runner, one unit in each such part in
                   turn (unit_part()).  The wire programs come first in the list
                   of units.  Each such part then gets at most one wire program,
                   which is the largest unit.  The last part also runs the
                   cases of sweep_sources() in test_unit_store and the modules
                   of the group with no parts.
    With FIRST_UNIT_PART parts or fewer, each kind of work goes to the part of
    its number modulo the count, and each part holds units.  A call with no
    part does each kind of work in one part.
"""

CHECK_PART = 0
STORE_PART = 1
ROOTS_PART = 2
FIRST_UNIT_PART = 3


def role_part(role: int, count: int) -> int:
    """Return the part of a kind of work of THE PARTS, such as STORE_PART, in a group of `count` parts."""
    return role % count


def holds_units(index: int, count: int) -> bool:
    """Return True when unit_part() gives units to the part `index` of a group of `count` parts."""
    return count <= FIRST_UNIT_PART or index >= FIRST_UNIT_PART


def unit_part(position: int, count: int) -> int:
    """Return the part of the unit at a position of the list of units of test_runner, in a group of `count` parts.

    With more than FIRST_UNIT_PART parts, the units go to the parts from FIRST_UNIT_PART on, one unit in each part in
    turn.  Else they go to each part in turn (THE PARTS).
    """
    if count <= FIRST_UNIT_PART:
        return position % count
    return FIRST_UNIT_PART + position % (count - FIRST_UNIT_PART)
