"""The pin table: the pinned tools of the tree (utils/pins.txt), and the platform of a host.

THE ROWS
    The pin table obeys the rows grammar of assay.core.rows.  It has a head
    comment, a format row and one row kind:

        format 1
        pin NAME via=VIA version=VERSION [source=URL] [sha256=DIGEST | commit=SHA] [platform=PLATFORM] | WHAT

    The fields of a pin row:
      via       archive, pip, git or host (Via)
      version   the pinned version.  For a row of via=host, the minimum version
      source    the URL of the download, or of the git repository
      sha256    the SHA-256 of the download: 64 lowercase hexadecimal digits.
                A row of via=archive or via=pip has it
      commit    the commit of a git repository: 40 lowercase hexadecimal
                digits.  A row of via=git has it, and no other row does
      platform  the host that the row serves, such as linux-x86_64.  A row
                with no platform serves each host
    A row of via=host has no source, no sha256 and no commit.  The cell tells
    what the tool is.  The key of a row is its name and its platform, so a
    second row with the same key is an error.

THE PLATFORM
    host_platform() gives the system and the machine of a host in the words
    of the table: the system in lowercase (linux, darwin), and the machine
    x86_64 or aarch64.  A machine that calls itself arm64 is aarch64.

`guard selftest tests.core.test_pins` does a test of the module.
"""

import enum
import os
from dataclasses import dataclass
from pathlib import Path

from assay.core import rows as rows_grammar
from assay.core.paths import REPO_ROOT

PINS = REPO_ROOT / "utils" / "pins.txt"
# The path of the pin table in each copy of the tree, from its root.
PINS_PATH = "utils/pins.txt"
FORMAT = "1"
PIN_FIELDS = frozenset({"via", "version", "source", "sha256", "commit", "platform"})
DIGEST_LENGTH = 64
COMMIT_LENGTH = 40
HEX_DIGITS = frozenset("0123456789abcdef")
# The machine of a host as the table names it, from the name that the kernel gives.
MACHINES = {"x86_64": "x86_64", "amd64": "x86_64", "aarch64": "aarch64", "arm64": "aarch64"}


class Via(enum.StrEnum):
    """How a pinned tool comes to the host (THE ROWS)."""

    ARCHIVE = "archive"
    PIP = "pip"
    GIT = "git"
    HOST = "host"


@dataclass(frozen=True, slots=True)
class Pin:
    """One pin row.  `digest` is the SHA-256 of a download and `commit` the commit of a repository, each "" when the
    row has none, and `platform` is "" for a row that serves each host."""

    name: str
    via: Via
    version: str
    source: str
    digest: str
    commit: str
    platform: str
    text: str
    line: int


def is_hex(text: str, length: int) -> bool:
    """Whether a text is a run of lowercase hexadecimal digits of one length."""
    return len(text) == length and all(character in HEX_DIGITS for character in text)


def is_platform(text: str) -> bool:
    """Whether a text is a platform of the table: a system of lowercase letters, a hyphen and a machine of MACHINES."""
    system, hyphen, machine = text.partition("-")
    return bool(hyphen and system.isalpha() and system.islower() and machine in set(MACHINES.values()))


def parse_pin(row: rows_grammar.Row) -> Pin:
    """Read one pin row.

    Raises:
        rows_grammar.RowsError: If the row does not have the form of a pin row
    """
    if len(row.words) < 2 or len(row.cells) != 1 or not row.cells[0]:
        raise row.error("a pin row is 'pin NAME via=VIA version=VERSION ... | WHAT IT IS'")
    name = row.words[1]
    if "=" in name:
        raise row.error(f"the pin row has no name before its field {name!r}")
    values = rows_grammar.fields(row, PIN_FIELDS)
    for required in ("via", "version"):
        if required not in values:
            raise row.error(f"the pin {name} has no field {required}")
    try:
        via = Via(values["via"])
    except ValueError:
        raise row.error(f"the via {values['via']!r} of the pin {name} is not one of "
                        f"{', '.join(item.value for item in Via)}") from None
    source = values.get("source", "")
    digest = values.get("sha256", "")
    commit = values.get("commit", "")
    platform = values.get("platform", "")
    if via == Via.HOST and (source or digest or commit):
        raise row.error(f"the pin {name} has via=host, and a row of the host has no source, sha256 or commit")
    if via != Via.HOST and not source:
        raise row.error(f"the pin {name} has no field source")
    if via in (Via.ARCHIVE, Via.PIP) and (not is_hex(digest, DIGEST_LENGTH) or commit):
        raise row.error(f"the pin {name} has via={via.value}, so it has a field sha256 of {DIGEST_LENGTH} lowercase "
                        f"hexadecimal digits and no field commit")
    if via == Via.GIT and (not is_hex(commit, COMMIT_LENGTH) or digest):
        raise row.error(f"the pin {name} has via=git, so it has a field commit of {COMMIT_LENGTH} lowercase "
                        f"hexadecimal digits and no field sha256")
    if platform and not is_platform(platform):
        raise row.error(f"the platform {platform!r} of the pin {name} is not a system and a machine, such as "
                        f"linux-x86_64 or darwin-aarch64")
    return Pin(name, via, values["version"], source, digest, commit, platform, row.cells[0], row.line)


def parse(text: str, shown: str = PINS_PATH) -> tuple[Pin, ...]:
    """Read the pins of the text of a pin table.

    Raises:
        rows_grammar.RowsError: If a row does not obey the grammar or the schema, or two rows have one key

    Complexity: linear in the length of the table.
    """
    rows = rows_grammar.parse(text, shown)
    if not rows or rows[0].words != ("format", FORMAT) or rows[0].cells:
        raise rows_grammar.RowsError(shown, rows[0].line if rows else 0, f"the first row of the pin table is "
                                     f"'format {FORMAT}'")
    seen: dict[tuple[str, str], Pin] = {}
    for row in rows[1:]:
        if row.words[0] != "pin":
            raise row.error(f"the row kind {row.words[0]!r} is not format or pin")
        pin = parse_pin(row)
        earlier = seen.get((pin.name, pin.platform))
        if earlier is not None:
            raise row.error(f"the pin {pin.name} has a second row for {pin.platform or 'each platform'}.  The first "
                            f"is at line {earlier.line}")
        seen[(pin.name, pin.platform)] = pin
    return tuple(seen.values())


def load(path: Path = PINS, shown: str = PINS_PATH) -> tuple[Pin, ...]:
    """Read the pin table.

    Raises:
        rows_grammar.RowsError: If a row does not obey the grammar or the schema
        OSError: If the file cannot be read
    """
    return parse(path.read_text(encoding="utf-8"), shown)


def host_platform(system: str | None = None, machine: str | None = None) -> str:
    """Return the platform of a host in the words of the table (THE PLATFORM), by default of this host.

    A machine that the table does not name stays as the kernel gives it, so no
    row serves it.
    """
    uname = os.uname()
    system_name = (system if system is not None else uname.sysname).lower()
    machine_name = machine if machine is not None else uname.machine
    return f"{system_name}-{MACHINES.get(machine_name, machine_name)}"


def find(pins: tuple[Pin, ...], name: str, platform: str | None = None) -> Pin | None:
    """Return the pin of a name for a platform: the row of that platform, else the row with no platform, else None.

    Args:
        pins: The pins of the table
        name: The name of the pin
        platform: The platform, by default the platform of this host
    """
    wanted = host_platform() if platform is None else platform
    general: Pin | None = None
    for pin in pins:
        if pin.name != name:
            continue
        if pin.platform == wanted:
            return pin
        if not pin.platform:
            general = pin
    return general


def named(pins: tuple[Pin, ...], name: str) -> tuple[Pin, ...]:
    """Return each row of a name, for each platform."""
    return tuple(pin for pin in pins if pin.name == name)
