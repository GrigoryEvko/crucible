"""The reader of a .clang-format-ignore file: its patterns, and the files that they ignore.

THE FILE
    clang-format reads the file .clang-format-ignore of the directory of a
    file to format, or of the nearest directory above it.  The file holds one
    pattern on each line.  clang-format removes the spaces at the two ends of
    a line.  A blank line and a line that starts with `#` hold no pattern.  A
    pattern that starts with `!` is negated, and clang-format removes the
    spaces after the `!`.  A `!` with nothing after it holds no pattern.  A
    pattern is relative to the directory of the file.

THE MATCH
    clang-format (clang/lib/Format/MatchFilePath.cpp) matches a pattern to
    the whole path of a file.  `*` matches each run of characters with no
    `/`, and `?` matches one character that is not `/`.  Each other character
    matches itself.  The pattern and the path thus have the same parts
    between their `/`, and each part of the pattern matches its part of the
    path (matches()).  clang-format reads the patterns in their sequence.  A
    file is ignored at the first pattern that matches it and is not negated,
    or that does not match it and is negated (IgnoreFile.is_ignored()).

THE SHAPES THAT THE READER REFUSES
    The reader reads only the shapes above.  It refuses a pattern with `[`,
    `\\`, `**`, a `/` at its start or at its end, or an empty part, a part
    `.` or a part `..`.  clang-format gives some of these shapes a meaning
    that changes between its versions, so the reader refuses each one and
    does not guess (IgnoreError).

Complexity: linear in the bytes of the file, and linear in the patterns and the length of a path for each query.
"""

import fnmatch
from dataclasses import dataclass
from pathlib import Path

COMMENT = "#"
NEGATION = "!"
SEPARATOR = "/"
# The parts of a pattern that the reader refuses (THE SHAPES THAT THE READER REFUSES).
REFUSED_TEXTS = ("[", "\\", "**")
REFUSED_PARTS = frozenset({"", ".", ".."})


class IgnoreError(ValueError):
    """A .clang-format-ignore file that the reader cannot read: its path, the line and the reason."""

    def __init__(self, path: str, line: int, reason: str) -> None:
        """Keep the path of the file, the line of the problem, and the reason."""
        super().__init__(f"{path}:{line}: {reason}")
        self.path = path
        self.line = line
        self.reason = reason


@dataclass(frozen=True, slots=True)
class Pattern:
    """One pattern of the file: its text with no `!`, whether it is negated, and its line."""

    text: str
    is_negated: bool
    line: int


def matches(pattern: str, path: str) -> bool:
    """Return True when a pattern of THE MATCH matches the whole path, relative to the directory of the file.

    Complexity: linear in the parts of the path, with one match of each part.
    """
    pattern_parts = pattern.split(SEPARATOR)
    path_parts = path.split(SEPARATOR)
    return len(pattern_parts) == len(path_parts) and all(
        fnmatch.fnmatchcase(name, part) for part, name in zip(pattern_parts, path_parts, strict=True)
    )


@dataclass(frozen=True, slots=True)
class IgnoreFile:
    """The patterns of a .clang-format-ignore file, in their sequence (THE FILE)."""

    patterns: tuple[Pattern, ...] = ()

    def is_ignored(self, path: str) -> bool:
        """Return True when clang-format ignores the file of a path relative to the directory of the file (THE MATCH).

        Complexity: linear in the patterns, with one match of each.
        """
        return any(matches(pattern.text, path) != pattern.is_negated for pattern in self.patterns)


def refusal(text: str) -> str | None:
    """Return the reason that the reader refuses a pattern (THE SHAPES THAT THE READER REFUSES), or None."""
    refused = next((item for item in REFUSED_TEXTS if item in text), None)
    if refused is not None:
        return f"the pattern {text!r} holds {refused!r}, which the reader does not read"
    if any(part in REFUSED_PARTS for part in text.split(SEPARATOR)):
        return f"the pattern {text!r} has a {SEPARATOR!r} at an end, an empty part, or a part '.' or '..'"
    return None


def parse(text: str, shown: str) -> IgnoreFile:
    """Return the patterns of the text of a .clang-format-ignore file (THE FILE).

    Raises:
        IgnoreError: If a pattern has a shape that the reader refuses
    """
    patterns: list[Pattern] = []
    for number, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith(COMMENT):
            continue
        is_negated = line.startswith(NEGATION)
        body = line.removeprefix(NEGATION).lstrip() if is_negated else line
        if not body:
            continue
        reason = refusal(body)
        if reason is not None:
            raise IgnoreError(
                shown, number, f"{reason}.  Write the pattern with `*` and `?` only, relative to the file"
            )
        patterns.append(Pattern(body, is_negated, number))
    return IgnoreFile(tuple(patterns))


def read(path: Path, shown: str) -> IgnoreFile:
    """Read a .clang-format-ignore file, or give an empty file when the path does not exist (THE FILE).

    Raises:
        IgnoreError: If the file cannot be read, is not UTF-8, or has a pattern that the reader refuses
    """
    try:
        data = path.read_bytes()
    except FileNotFoundError:
        return IgnoreFile()
    except OSError as problem:
        raise IgnoreError(shown, 0, f"the file cannot be read: {problem}") from None
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as problem:
        raise IgnoreError(shown, 0, f"the file is not UTF-8: {problem}") from None
    return parse(text, shown)
