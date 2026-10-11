"""guard asm-compare — print the instruction difference of named functions of two objects, and write nothing.

usage: guard asm-compare --function NAME... --objects BEFORE AFTER [--require-same]
       guard asm-compare --function NAME... --source FILE --builds BEFORE_DIR AFTER_DIR [--with-sanitizers]
                         [--require-same]
       guard asm-compare --function NAME... --source FILE --build-dir DIR --base REVISION [--with-sanitizers]
                         [--require-same]

THE SIDES
    The command compares two sides: the code before a change and the code
    after it.  The code of a side is the code of one object:
      --objects  two object files, such as the objects of one unit in two
                 builds
      --builds   the unit of FILE, compiled with the command of the compile
                 database of each build directory
      --base     the unit of FILE at REVISION and in the tree, each compiled
                 with the command of the compile database of DIR.  `git
                 archive` writes the files of REVISION into a scratch
                 directory, and the command of the side REVISION names that
                 directory as the source root.  A file that the build
                 writes, such as a header that a build step writes, is the
                 same on the two sides.
    FILE is a path relative to the source root of the build, or an absolute
    path.  A compile writes its object into a scratch directory.  It has no
    word of the quarantine plugin (PLUGIN_PREFIX, PLUGIN_DEFINE), because the
    plugin reads the code and does not change it.  With no
    --with-sanitizers, a compile also has no sanitizer word, as the verdict
    of the rule codegen-shapes has none (THE COMPILES of
    assay.guard.engines.elf.codegen_shapes).

THE FUNCTIONS
    NAME is the demangled name of a function, such as
    fixy::concurrent::AtomicSnapshot<Payload>::load.  The full name of a
    function of a side has its parameter list and its qualifiers.  The
    function agrees with NAME when its full name is NAME.  It also agrees
    when its full name with no parameter list and no qualifier (QUALIFIERS)
    is NAME.  A NAME that agrees with two functions of one side, such as two
    overloads, is an error, and the message gives each full name.  A NAME
    with a parameter list, such as twice(int), then selects one overload.  A
    NAME that agrees with no function is an error, and the message gives the
    nearest names.  The full name has no unnamed namespace, and the cold
    part of a function is a part of it (codes_of() of codegen_shapes).

THE DIFFERENCE
    The command reads the instructions of each function on its returning
    paths, with the normalization of the rule codegen-shapes.  THE WALK and
    THE NORMALIZATION of codegen_shapes give that normalization.  For each
    NAME the command prints the count of the instructions of each side.
    Then it prints each instruction that only one side holds, with its class:
    check, branch, call, division, indexed or plain.  The two lists come from
    one longest subsequence that the two sides share (family_only() of
    codegen_shapes).  A function with no such instruction has the same
    normalized code on the two sides.

THE EXIT
    0 when the command prints each function.  With `--require-same`, 1 when
    a function has an instruction that only one side holds.  3 when a
    side cannot be read or compiled, or when a NAME agrees with no function
    or with two functions of a side.  2 on a usage error.

Complexity: one disassembly of each side, and for each NAME one longest shared subsequence, quadratic in the
instructions of the function.

`guard selftest tests.guard.test_asm_compare` does the test of the module.
"""

import difflib
import shlex
import tarfile
import tempfile
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.core import git
from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.guard.engines.elf import codegen_shapes as shapes
from assay.readers import cmake, compdb

PROGRAM = "guard asm-compare"
# The qualifiers at the end of a demangled member function, which a NAME does not hold (THE FUNCTIONS).
QUALIFIERS = (" const", " volatile", " &&", " &", " noexcept")
# The words of the quarantine plugin that a compile of a side does not have (THE SIDES).
PLUGIN_PREFIX = "-fplugin"
PLUGIN_DEFINE = "-DCRUCIBLE_QUARANTINE_ACTIVE"
# The number of the nearest names in the message of a NAME that agrees with no function.
NEAREST = 5
INDENT = "    "


class SideError(Exception):
    """A side cannot be read: an object is missing, a compile or a disassembly does not succeed, or a NAME is bad."""


@dataclass(frozen=True, slots=True)
class Side:
    """The code of one side: the name of the side in a message, and the code of each function by its name."""

    label: str
    codes: Mapping[str, shapes.Code]


@dataclass(frozen=True, slots=True)
class Difference:
    """The difference of one function: its NAME, the count of each side, and the instructions of only one side."""

    name: str
    before_count: int
    after_count: int
    only_before: tuple[shapes.Step, ...]
    only_after: tuple[shapes.Step, ...]

    @property
    def is_same(self) -> bool:
        """True when the two sides have the same normalized instructions."""
        return not self.only_before and not self.only_after


# ── The sides ──────────────────────────────────────────────────────


def object_side(path: Path) -> Side:
    """Return the side of one object file (THE SIDES).

    Raises:
        SideError: If the file is missing, or objdump or c++filt does not succeed
    """
    if not path.is_file():
        raise SideError(f"{path} is no object file")
    try:
        return Side(str(path), shapes.codes_of(shapes.disassembled(path), keeps_arguments=True))
    except shapes.CompileFailed as problem:
        raise SideError(f"{path} cannot be disassembled: {problem}") from None


def source_root(build_dir: Path) -> Path:
    """Return the source root of a build directory, from the reply of the CMake file API.

    Raises:
        SideError: If the reply cannot be read
    """
    try:
        return Path(cmake.roots(build_dir)[0])
    except cmake.CmakeError as problem:
        raise SideError(f"the source root of {build_dir} cannot be read: {problem}") from None


def unit_entry(build_dir: Path, source: Path) -> compdb.CompileEntry:
    """Return the row of the compile database of a build that compiles one source file.

    Complexity: linear in the rows of the database.

    Raises:
        SideError: If the database cannot be read, or it has no row of the file
    """
    wanted = source if source.is_absolute() else source_root(build_dir) / source
    try:
        rows = compdb.entries(build_dir)
    except compdb.CompdbError as problem:
        raise SideError(f"the compile database of {build_dir} cannot be read: {problem}") from None
    found = [row for row in rows if Path(row.file).resolve() == wanted.resolve()]
    if not found:
        raise SideError(f"the compile database of {build_dir} has no compile of {wanted}")
    return found[0]


def side_entry(entry: compdb.CompileEntry, old_root: Path, new_root: Path) -> compdb.CompileEntry:
    """Return the row of a compile with no plugin word, and with new_root as the root of each word that names old_root.

    Complexity: linear in the length of the command.
    """
    old_text, new_text = f"{old_root}/", f"{new_root}/"
    words = tuple(
        word.replace(old_text, new_text)
        for word in entry.argv()
        if not word.startswith(PLUGIN_PREFIX) and word != PLUGIN_DEFINE
    )
    return compdb.CompileEntry(
        entry.file.replace(old_text, new_text), entry.directory, entry.output, shlex.join(words), words
    )


def compiled_side(label: str, entry: compdb.CompileEntry, with_sanitizers: bool) -> Side:
    """Compile the unit of a row into a scratch object, and return its side (THE SIDES).

    Raises:
        SideError: If the compile or the disassembly does not succeed
    """
    try:
        return Side(label, shapes.codes_of(shapes.compiled_functions(entry, with_sanitizers), keeps_arguments=True))
    except (shapes.CompileFailed, ValueError) as problem:
        raise SideError(f"the side {label} cannot be compiled: {problem}") from None


def build_sides(builds: Sequence[Path], source: Path, with_sanitizers: bool) -> tuple[Side, Side]:
    """Return the sides of one unit compiled in two build directories (THE SIDES, --builds).

    Raises:
        SideError: If a build cannot be read, or a compile does not succeed
    """
    sides: list[Side] = []
    for build_dir in builds:
        root = source_root(build_dir)
        entry = side_entry(unit_entry(build_dir, source), root, root)
        sides.append(compiled_side(str(build_dir), entry, with_sanitizers))
    return sides[0], sides[1]


def exported(root: Path, revision: str, scratch: Path) -> Path:
    """Write the files of one revision of the repository of root into a scratch directory, and return it.

    Complexity: linear in the bytes of the tree of the revision.

    Raises:
        SideError: If git does not resolve the revision, or the archive cannot be written or read
    """
    archive, tree = scratch / "tree.tar", scratch / "tree"
    try:
        if git.resolve(root, revision) is None:
            raise SideError(f"git names no commit {revision} in {root}")
        git.run(root, "archive", "--format=tar", f"--output={archive}", revision)
        with tarfile.open(archive) as files:
            files.extractall(tree, filter="data")
    except (git.GitError, tarfile.TarError, OSError) as problem:
        raise SideError(f"the files of {revision} cannot be written: {problem}") from None
    archive.unlink()
    return tree


def base_sides(build_dir: Path, source: Path, revision: str, with_sanitizers: bool) -> tuple[Side, Side]:
    """Return the sides of one unit at a revision and in the tree, with the command of one build (THE SIDES, --base).

    Raises:
        SideError: If the build or the revision cannot be read, or a compile does not succeed
    """
    root = source_root(build_dir)
    entry = unit_entry(build_dir, source)
    with tempfile.TemporaryDirectory(prefix="asm-compare-") as scratch_text:
        tree = exported(root, revision, Path(scratch_text))
        before = compiled_side(revision, side_entry(entry, root, tree), with_sanitizers)
    after = compiled_side(str(root), side_entry(entry, root, root), with_sanitizers)
    return before, after


# ── The difference ─────────────────────────────────────────────────


def plain_name(name: str) -> str:
    """Return a function name with no qualifier of QUALIFIERS at its end and no parameter list (THE FUNCTIONS)."""
    trimmed = name
    while True:
        qualifier = next((item for item in QUALIFIERS if trimmed.endswith(item)), None)
        if qualifier is None:
            return shapes.strip_arguments(trimmed)
        trimmed = trimmed[: -len(qualifier)]


def code_of(side: Side, name: str) -> shapes.Code:
    """Return the code of the one function of a side that agrees with NAME (THE FUNCTIONS).

    Complexity: linear in the functions of the side.

    Raises:
        SideError: If no function or two functions of the side agree with NAME
    """
    found = sorted(key for key in side.codes if key == name or plain_name(key) == name)
    if len(found) == 1:
        return side.codes[found[0]]
    if found:
        raise SideError(f"{name} agrees with {len(found)} functions of {side.label}: {'; '.join(found)}.  Give one")
    nearest = difflib.get_close_matches(name, sorted({plain_name(key) for key in side.codes}), n=NEAREST)
    hint = f".  The nearest names: {'; '.join(nearest)}" if nearest else ""
    raise SideError(f"{side.label} holds no function {name}{hint}")


def difference(name: str, before: Side, after: Side) -> Difference:
    """Return the difference of one function of the two sides (THE DIFFERENCE).

    Raises:
        SideError: If a side has no function or two functions that agree with NAME
    """
    old_steps = shapes.steps_of(code_of(before, name))
    new_steps = shapes.steps_of(code_of(after, name))
    return Difference(
        name,
        len(old_steps),
        len(new_steps),
        tuple(shapes.family_only(old_steps, new_steps)),
        tuple(shapes.family_only(new_steps, old_steps)),
    )


def lines_of(item: Difference) -> list[str]:
    """Return the lines that the command prints for one function (THE DIFFERENCE)."""
    lines = [f"{item.name}: before {item.before_count} instructions, after {item.after_count} instructions"]
    if item.is_same:
        return [*lines, f"{INDENT}the same normalized instructions"]
    lines += [f"{INDENT}- {step.text}  [{step.kind}]" for step in item.only_before]
    lines += [f"{INDENT}+ {step.text}  [{step.kind}]" for step in item.only_after]
    return lines


# ── The command ────────────────────────────────────────────────────


@dataclass(frozen=True, slots=True)
class Request:
    """The words of one call of the command: the NAMEs, the sides and the two flags (THE SIDES)."""

    names: tuple[str, ...]
    objects: tuple[Path, Path] | None = None
    builds: tuple[Path, Path] | None = None
    base: str | None = None
    source: Path | None = None
    build_dir: Path | None = None
    with_sanitizers: bool = False
    require_same: bool = False

    def problem(self) -> str | None:
        """Return the problem of a request that does not name each part of a side, or None."""
        if self.objects is not None:
            return None
        if self.source is None:
            return "--builds and --base need --source FILE"
        if self.builds is None and self.build_dir is None:
            return "--base needs --build-dir DIR, the build of the command of the unit"
        return None


def sides_of(request: Request) -> tuple[Side, Side]:
    """Return the two sides that a request names (THE SIDES).

    Raises:
        SideError: If a side cannot be read or compiled
    """
    if request.objects is not None:
        return object_side(request.objects[0]), object_side(request.objects[1])
    if request.source is None:
        raise SideError("--builds and --base need --source FILE")
    if request.builds is not None:
        builds = [path.resolve() for path in request.builds]
        return build_sides(builds, request.source, request.with_sanitizers)
    if request.build_dir is None or request.base is None:
        raise SideError("--base needs --build-dir DIR, the build of the command of the unit")
    return base_sides(request.build_dir.resolve(), request.source, request.base, request.with_sanitizers)


def compare(request: Request) -> int:
    """Print the difference of each NAME of the two sides of a request (THE EXIT).

    Returns:
        The exit code of THE EXIT
    """
    problem = request.problem()
    if problem is not None:
        check_report.write_line(f"{PROGRAM}: {problem}", is_error=True)
        return ExitCode.USAGE
    try:
        before, after = sides_of(request)
        differences = [difference(name, before, after) for name in request.names]
    except SideError as error:
        check_report.write_line(f"{PROGRAM}: {error}", is_error=True)
        return ExitCode.MISSING_INPUT
    check_report.write_line(f"before: {before.label}\nafter:  {after.label}")
    for item in differences:
        check_report.write_line("\n".join(lines_of(item)))
    if request.require_same and not all(item.is_same for item in differences):
        return ExitCode.FINDINGS
    return ExitCode.OK


def main(argv: list[str]) -> int:
    """Read the words of the command, and print the difference of each NAME of two sides (THE EXIT).

    Returns:
        The exit code of THE EXIT
    """
    from assay.core import cli

    parser = cli.parser(PROGRAM, "print the normalized instruction difference of named functions of two objects")
    parser.add_argument("--function", action="append", required=True, metavar="NAME")
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--objects", nargs=2, type=Path, metavar=("BEFORE", "AFTER"))
    modes.add_argument("--builds", nargs=2, type=Path, metavar=("BEFORE_DIR", "AFTER_DIR"))
    modes.add_argument("--base", metavar="REVISION")
    parser.add_argument("--source", type=Path, metavar="FILE")
    parser.add_argument("--build-dir", type=Path, metavar="DIR")
    parser.add_argument("--with-sanitizers", action="store_true")
    parser.add_argument("--require-same", action="store_true")
    try:
        options = parser.parse_args(argv)
    except SystemExit as stop:
        return ExitCode.OK if stop.code == 0 else ExitCode.USAGE
    return compare(
        Request(
            tuple(options.function),
            None if options.objects is None else (options.objects[0], options.objects[1]),
            None if options.builds is None else (options.builds[0], options.builds[1]),
            options.base,
            options.source,
            options.build_dir,
            options.with_sanitizers,
            options.require_same,
        )
    )
