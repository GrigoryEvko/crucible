"""compdb — the compile database of a build directory: one entry for each compile.

THE INPUT
    CMake writes BUILD_DIR/compile_commands.json at each configure, because
    the root CMakeLists.txt sets CMAKE_EXPORT_COMPILE_COMMANDS.  The file is
    a JSON array.  Each object names one compile: "directory" (the working
    directory of the compile), "file" (the source), "output" (the object,
    relative to the directory) and "command" (one line with shell quotes) or
    "arguments" (a list of words).  CMake 3.20 or a subsequent version writes
    "output", and CMake writes "command" and no "arguments".

WHAT THE READER GIVES
    entries(build_dir) gives each compile as a CompileEntry, in the order of
    the file.  The file and the output are absolute: a relative path joins
    the directory of its compile, and the output is normalized.  The output
    is None when the object names none.  argv() of an entry splits the
    command with shlex only when the caller asks, because a split of each of
    thousands of commands costs most of the time of a read.
    database_file(build_dir) gives the path of the file, for a check of its
    stat fields.  plant() writes a planted database for a test, as rows or
    as text.

ERRORS
    CompdbError names the file and the command that writes it.
    MissingDatabase, a subclass, says that the file does not exist.

Complexity: linear in the size of the file.
"""

import json
import os

TYPE_CHECKING = False
if TYPE_CHECKING:
    from collections.abc import Iterable, Mapping
    from pathlib import Path

DATABASE_NAME = "compile_commands.json"
WRITER = "CMake writes it at each configure of the build directory, for example `cmake --preset default`"


class CompdbError(Exception):
    """The compile database of a build directory cannot be read, or an object of it is not a compile."""


class MissingDatabase(CompdbError):
    """The build directory holds no compile database."""


class CompileEntry:
    """One compile of the database.

    `command` is the line of the compile, and `arguments` holds the words
    when the database gives a list.  `file` and `output` are absolute paths
    as texts, because a Path for each of thousands of entries costs more
    than the JSON parse of the file.  The class has __slots__ and is no
    dataclass, because `loop fixture` reads the database and the import of
    dataclasses costs approximately 6 ms (-X importtime on the build host).
    """

    __slots__ = ("arguments", "command", "directory", "file", "output")

    def __init__(self, file: str, directory: str, output: str | None, command: str,
                 arguments: tuple[str, ...] | None = None) -> None:
        """Hold the parts of one compile."""
        self.file = file
        self.directory = directory
        self.output = output
        self.command = command
        self.arguments = arguments

    def __eq__(self, other: object) -> bool:
        """Two entries are equal when each part is equal."""
        if not isinstance(other, CompileEntry):
            return NotImplemented
        return (self.file, self.directory, self.output, self.command, self.arguments) == (
            other.file, other.directory, other.output, other.command, other.arguments)

    def __hash__(self) -> int:
        """Hash each part."""
        return hash((self.file, self.directory, self.output, self.command, self.arguments))

    def __repr__(self) -> str:
        """Return the entry for a message."""
        return f"CompileEntry(file={self.file!r}, output={self.output!r})"

    def argv(self) -> tuple[str, ...]:
        """Return the words of the compile command, as a POSIX shell splits the line.

        A line with no quote and no backslash splits at white space, which
        is what shlex.split gives for it, at a small part of the cost.  Most
        lines of the tree have no quote.

        Raises:
            ValueError: If the command line does not obey the shell quoting rules
        """
        if self.arguments is not None:
            return self.arguments
        if "'" not in self.command and '"' not in self.command and "\\" not in self.command:
            return tuple(self.command.split())
        import shlex

        return tuple(shlex.split(self.command))


def database_file(build_dir: Path) -> Path:
    """Return the path of the compile database of a build directory, which can be absent."""
    return build_dir / DATABASE_NAME


def absolute(directory: str, path: str, normalize: bool) -> str:
    """Return a path of the database as an absolute path: a relative path joins the directory of its compile."""
    joined = path if path.startswith("/") else os.path.join(directory, path)
    return os.path.normpath(joined) if normalize else joined


def entry_of(row: object, index: int, shown: str) -> CompileEntry:
    """Return the entry of one object of the database.

    Raises:
        CompdbError: If the object has no text directory or file, or neither a command nor a list of arguments
    """
    if not isinstance(row, dict):
        raise CompdbError(f"{shown}: the item {index} is not an object.  {WRITER}")
    directory, file, output = row.get("directory"), row.get("file"), row.get("output")
    command, arguments = row.get("command"), row.get("arguments")
    if not isinstance(directory, str) or not isinstance(file, str):
        raise CompdbError(f"{shown}: the item {index} has no text directory and file.  {WRITER}")
    if output is not None and not isinstance(output, str):
        raise CompdbError(f"{shown}: the output of the item {index} is not a text.  {WRITER}")
    words: tuple[str, ...] | None = None
    if isinstance(arguments, list) and all(isinstance(word, str) for word in arguments):
        words = tuple(arguments)
        if not isinstance(command, str):
            import shlex

            command = shlex.join(words)
    if not isinstance(command, str):
        raise CompdbError(f"{shown}: the item {index} has no command and no list of arguments.  {WRITER}")
    return CompileEntry(absolute(directory, file, normalize=False), directory,
                        None if output is None else absolute(directory, output, normalize=True), command, words)


def entries(build_dir: Path) -> tuple[CompileEntry, ...]:
    """Return each compile of the database of a build directory, in the order of the file.

    Complexity: linear in the size of the file.

    Raises:
        MissingDatabase: If the build directory holds no compile database
        CompdbError: If the database cannot be read, is not a JSON array, or holds an object that is not a compile
    """
    path = database_file(build_dir)
    try:
        text = path.read_text(encoding="utf-8", errors="surrogateescape")
    except FileNotFoundError:
        raise MissingDatabase(f"{path} does not exist.  {WRITER}") from None
    except OSError as problem:
        raise CompdbError(f"{path} cannot be read: {problem}") from None
    try:
        rows = json.loads(text)
    except ValueError as problem:
        raise CompdbError(f"{path} cannot be read: it is not JSON ({problem}).  {WRITER}") from None
    if not isinstance(rows, list):
        raise CompdbError(f"{path} cannot be read: the top level is not an array of compiles.  {WRITER}")
    return tuple(entry_of(row, index, str(path)) for index, row in enumerate(rows))


def plant(build_dir: Path, rows: Iterable[Mapping[str, object]] | str) -> Path:
    """Write a planted compile database into a directory, for a test, and return its path.

    Args:
        build_dir: The directory
        rows: The objects of the array, or the text of the whole file
    """
    path = database_file(build_dir)
    path.write_text(rows if isinstance(rows, str) else json.dumps([dict(row) for row in rows]), encoding="utf-8")
    return path
