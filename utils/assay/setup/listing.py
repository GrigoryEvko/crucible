"""setup list — print the pin table of utils/pins.txt.

usage: setup list [NAME]...

The command prints one line for each pin, or for each pin of the names: the
name, the platform ("-" for each platform), the via, the version, and the
digest or the commit.  A pin that serves this host has the mark "*".

Exit status: 0, or 2 when the table does not read or a name has no pin.
"""

import sys

from assay.core import pins as pin_table
from assay.core import rows as rows_grammar


def main(argv: list[str]) -> int:
    """Print the pins of the names, or each pin.

    Args:
        argv: The words after `setup list`

    Returns:
        The exit status (the docstring of the module)
    """
    try:
        table = pin_table.load()
    except (rows_grammar.RowsError, OSError) as problem:
        print(f"setup list: the pin table cannot be read: {problem}", file=sys.stderr)
        return 2
    unknown = [name for name in argv if not pin_table.named(table, name)]
    if unknown:
        print(f"setup list: {pin_table.PINS_PATH} has no pin {', '.join(unknown)}.", file=sys.stderr)
        return 2
    host = pin_table.host_platform()
    for pin in sorted(table, key=lambda item: (item.name, item.platform)):
        if argv and pin.name not in argv:
            continue
        serves = pin_table.find(table, pin.name, host) == pin
        identity = pin.digest or pin.commit or "-"
        print(f"{'*' if serves else ' '} {pin.name:<20} {pin.platform or '-':<15} {pin.via.value:<8} "
              f"{pin.version:<20} {identity}")
    return 0
