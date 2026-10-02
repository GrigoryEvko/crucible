"""layer_rules — read the rule table of the base layers and of the quarantine.

utils/scripts/layer-rules.txt holds the table, and its head comment gives the
format and the meaning of each row.  The quarantine plugin of
utils/tools/quarantine/ reads the same file in C++, and it refuses each table
that load() refuses.  utils/tools/quarantine/test/check_plugin.py compares the
two readers on the same malformed tables.

The layer guard (check-layer-boundary.py) reads the layer rows.  It names the
project roots at the granularity of a directory under include/, so
guard_layers() folds the layers of the table into one layer for each root.
"""

from __future__ import annotations

import dataclasses
from pathlib import Path

TABLE = Path(__file__).resolve().parent / "layer-rules.txt"
MODES = ("report", "error")
ROW_KINDS = ("layer", "allow", "door", "admit", "quarantine", "enforce")


class TableError(ValueError):
    """The rule table does not obey its format."""


@dataclasses.dataclass(frozen=True)
class Layer:
    """One layer row: a name, a rank and the paths of its files."""

    name: str
    rank: int
    paths: tuple[str, ...]


@dataclasses.dataclass(frozen=True)
class Admit:
    """One admit row: the entry, the family that replaces it (or ""), the reason and the line."""

    entry: str
    until: str
    reason: str
    line: int


@dataclasses.dataclass(frozen=True)
class RuleTable:
    """The rows of one rule table, in the order of the file."""

    layers: tuple[Layer, ...]
    allows: tuple[tuple[str, str], ...]
    doors: tuple[tuple[str, str], ...]
    admits: tuple[Admit, ...]
    quarantines: tuple[str, ...]
    enforces: tuple[tuple[str, str], ...]

    def layer_named(self, name: str) -> Layer | None:
        """Return the layer with NAME, or None."""
        return next((layer for layer in self.layers if layer.name == name), None)

    def class_of(self, rel: str) -> tuple[str, Layer | None]:
        """Return the class of a path relative to the source root, and its layer.

        The longest path of a layer row or a quarantine row that holds the
        file decides.  Complexity: O(rows).

        Returns:
            ("base", layer), ("quarantined", None) or ("", None) when no row holds the file
        """
        best = ("", None)
        best_length = -1
        for layer in self.layers:
            for path in layer.paths:
                if holds(path, rel) and len(path) > best_length:
                    best, best_length = ("base", layer), len(path)
        for path in self.quarantines:
            if holds(path, rel) and len(path) > best_length:
                best, best_length = ("quarantined", None), len(path)
        return best

    def mode_of(self, rel: str) -> str:
        """Return the enforce mode of a path relative to the source root: report or error."""
        mode = "report"
        best_length = -1
        for path, row_mode in self.enforces:
            if holds(path, rel) and len(path) > best_length:
                mode, best_length = row_mode, len(path)
        return mode


def holds(path: str, rel: str) -> bool:
    """Say whether a row PATH holds the file REL: a directory holds each file under it."""
    return rel.startswith(path) if path.endswith("/") else rel == path


def _check_path(path: str, where: str) -> None:
    """Refuse a PATH that is not a plain relative path."""
    parts = (path[:-1] if path.endswith("/") else path).split("/")
    if not path or path.startswith("/") or any(part in ("", ".", "..") for part in parts):
        raise TableError(f"{where}: the path {path!r} must be relative to the source root, with no '.', '..' "
                         f"or empty component")


def _check_header(header: str, where: str) -> None:
    """Refuse a HEADER that is not a name in angle brackets."""
    if len(header) < 3 or not header.startswith("<") or not header.endswith(">"):
        raise TableError(f"{where}: the header {header!r} must be a name in angle brackets, for example <cstdint>")


def parse(text: str, name: str = "layer-rules.txt") -> RuleTable:
    """Parse the text of a rule table.

    Complexity: O(rows squared) for the duplicate checks, and the table has
    about one hundred rows.

    Args:
        text: The text of the table
        name: The name of the table in each error message

    Raises:
        TableError: When a row does not obey the format of the head comment
    """
    layers: list[Layer] = []
    allows: list[tuple[str, str]] = []
    doors: list[tuple[str, str]] = []
    admits: list[Admit] = []
    quarantines: list[str] = []
    enforces: list[tuple[str, str]] = []
    class_paths: dict[str, str] = {}
    allow_lines: list[tuple[str, str]] = []
    for number, line in enumerate(text.splitlines(), start=1):
        content = line.strip()
        if not content or content.startswith("#"):
            continue
        where = f"{name}:{number}"
        words_text, _, reason = content.partition("|")
        words = words_text.split()
        reason = reason.strip()
        if not words:
            raise TableError(f"{where}: the row has a reason and no kind")
        kind, args = words[0], words[1:]
        if kind not in ROW_KINDS:
            raise TableError(f"{where}: the row kind {kind!r} is unknown; the kinds are {', '.join(ROW_KINDS)}")
        if kind == "layer":
            if len(args) < 3 or not args[1].isdigit():
                raise TableError(f"{where}: a layer row is 'layer NAME RANK PATH...', with a RANK of digits")
            if any(layer.name == args[0] for layer in layers):
                raise TableError(f"{where}: the layer {args[0]} has a second row")
            if any(layer.rank == int(args[1]) for layer in layers):
                raise TableError(f"{where}: the rank {args[1]} belongs to a second layer")
            for path in args[2:]:
                _check_path(path, where)
                if path in class_paths:
                    raise TableError(f"{where}: the path {path} is also in {class_paths[path]}")
                class_paths[path] = f"the layer {args[0]}"
            layers.append(Layer(args[0], int(args[1]), tuple(args[2:])))
        elif kind == "allow":
            if len(args) != 2:
                raise TableError(f"{where}: an allow row is 'allow LAYER HEADER'")
            _check_header(args[1], where)
            allows.append((args[0], args[1]))
            allow_lines.append((args[0], where))
        elif kind == "door":
            if len(args) != 2 or args[1].endswith("/"):
                raise TableError(f"{where}: a door row is 'door HEADER OWNER', and OWNER is one file")
            _check_header(args[0], where)
            _check_path(args[1], where)
            if any(header == args[0] for header, _ in doors):
                raise TableError(f"{where}: the header {args[0]} has a second door")
            doors.append((args[0], args[1]))
        elif kind == "admit":
            if len(args) not in (1, 3) or (len(args) == 3 and args[1] != "until") or not reason:
                raise TableError(f"{where}: an admit row is 'admit ENTRY | REASON' or 'admit ENTRY until FAMILY | "
                                 f"REASON', and the reason is necessary")
            admits.append(Admit(args[0], args[2] if len(args) == 3 else "", reason, number))
        elif kind == "quarantine":
            if len(args) != 1:
                raise TableError(f"{where}: a quarantine row is 'quarantine PATH'")
            _check_path(args[0], where)
            if args[0] in class_paths:
                raise TableError(f"{where}: the path {args[0]} is also in {class_paths[args[0]]}")
            class_paths[args[0]] = "a quarantine row"
            quarantines.append(args[0])
        else:
            if len(args) != 2 or args[1] not in MODES:
                raise TableError(f"{where}: an enforce row is 'enforce PATH MODE', and MODE is report or error")
            _check_path(args[0], where)
            if any(path == args[0] for path, _ in enforces):
                raise TableError(f"{where}: the path {args[0]} has a second enforce row")
            enforces.append((args[0], args[1]))
    known = {layer.name for layer in layers}
    for layer_name, where in allow_lines:
        if layer_name not in known:
            raise TableError(f"{where}: the allow row names the layer {layer_name}, and no layer row gives it")
    return RuleTable(tuple(layers), tuple(allows), tuple(doors), tuple(admits), tuple(quarantines),
                     tuple(enforces))


def load(path: Path = TABLE) -> RuleTable:
    """Read and parse the rule table at PATH.

    Raises:
        TableError: When the table does not obey its format
        OSError: When the file cannot be read
    """
    return parse(path.read_text(encoding="utf-8"), path.name)


def guard_layers(table: RuleTable) -> tuple[tuple[tuple[str, str], ...], dict[str, frozenset[str]]]:
    """Fold the layers of TABLE into one layer for each project root, for the layer guard.

    The root of a layer path is its second component: include/fixy/Fn.h and
    src/fixy/os/ have the root fixy.  The roots are in the order of the
    lowest rank of their layers, and a root can name itself and each root
    before it.

    Returns:
        The scope prefix of each root, such as ("include/fixy/", "fixy"), and the
        roots that each root can name
    """
    lowest: dict[str, int] = {}
    prefixes: list[tuple[str, str]] = []
    for layer in table.layers:
        for path in layer.paths:
            parts = path.split("/")
            if len(parts) < 3:
                raise TableError(f"the layer path {path} has no project root: it must start with a directory "
                                 f"and a root, such as include/fixy/")
            root = parts[1]
            lowest[root] = min(lowest.get(root, layer.rank), layer.rank)
            prefix = f"{parts[0]}/{root}/"
            if (prefix, root) not in prefixes:
                prefixes.append((prefix, root))
    order = sorted(lowest, key=lambda root: lowest[root])
    allowed = {root: frozenset(order[:index + 1]) for index, root in enumerate(order)}
    return tuple(prefixes), allowed
