#!/usr/bin/env python3
"""The loader of the vessel reads the x86-64 level of each library before it loads it.

Each vessel library carries an ELF note with the level of its flags (THE NOTE
of vessel/torch/level_note.h), and the level probe libcrucible_level.so gives
the level of the host (THE LEVEL OF A LIBRARY of crucible_native.py).

The checks:

    built     the probe of the build says exact v1, the two vessel libraries
              of the build have one note, and their level is at or below the
              level of the host
    host      the pick with no request agrees with the flags of /proc/cpuinfo,
              and a request at or below the host gives the requested level
    fatal     a request that is no level ends the process, and the message
              names the text
    root      a level root gives the library of the picked level, with and
              with no request, and the loader takes the path that the root
              gave again when a caller names it, with the probe of the root
    refused   the loader refuses each of these before it loads the library:
              a library of a root of a different level, a library of the kind
              host in a root, a library of a build above the picked level, a
              named library of a level directory above the picked level, a
              named library of a directory that is no level directory and
              has no probe beside it, a library with no note, and a pair of
              two notes
    loaded    with CRUCIBLE_LEVEL_ROOT in the environment, as `loop levels`
              runs the vessel tests of each level, the loader loads the two
              vessel libraries from the directory of the picked level of that
              root, and the pick is the level of CRUCIBLE_ISA_LEVEL.  So a pass
              at each level comes from the libraries of that level

A planted library is a copy of the probe with a different descriptor.  Each
planted file is in a temporary directory of the test.  A refused library never
reaches ctypes.CDLL.

Usage:
    python test_isa_level.py

Exit 0 when each check passes, 1 otherwise.  On a host other than x86_64 the
libraries carry no note, and the test passes with no check.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
from collections.abc import Callable
from pathlib import Path
from unittest import mock

# A caller starts this file by its path, so the vessel directory is not always
# on the import path.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import crucible_native  # noqa: E402
from crucible_native import LevelKind  # noqa: E402

VESSEL = crucible_native.VESSEL_STAMP.library
DISPATCH = crucible_native.DISPATCH_STAMP.library
PROBE = crucible_native.PROBE_STAMP.library

# The flags of /proc/cpuinfo of each feature of v2, v3 and v4.  Linux clears
# the flags of AVX and AVX-512 when the OS does not save their state.
CPUINFO_LEVELS = (
    (2, {"cx16", "lahf_lm", "popcnt", "pni", "sse4_1", "sse4_2", "ssse3"}),
    (3, {"avx", "avx2", "bmi1", "bmi2", "f16c", "fma", "abm", "movbe", "xsave"}),
    (4, {"avx512f", "avx512bw", "avx512cd", "avx512dq", "avx512vl"}),
)


def cpuinfo_level() -> int:
    """Give the level of the host from the flags of the first core of /proc/cpuinfo."""
    for line in Path("/proc/cpuinfo").read_text().splitlines():
        if line.startswith("flags"):
            flags = set(line.split(":", 1)[1].split())
            level = 1
            for candidate, needed in CPUINFO_LEVELS:
                if not needed <= flags:
                    break
                level = candidate
            return level
    raise RuntimeError("/proc/cpuinfo has no flags line")


def plant(source: Path, target: Path, level: int, kind: LevelKind) -> None:
    """Copy the probe SOURCE to TARGET, with the level LEVEL and the kind KIND in its note."""
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
    note = crucible_native.level_note(str(source))
    assert note is not None
    image = bytearray(target.read_bytes())
    image[note.offset] = level
    image[note.offset + 1] = int(kind)
    target.write_bytes(bytes(image))


def plant_without_note(source: Path, target: Path) -> None:
    """Copy the probe SOURCE to TARGET, with a different owner in its note, so the loader finds no level note."""
    shutil.copyfile(source, target)
    image = bytearray(target.read_bytes())
    at = image.index(b"Crucible\0")
    image[at:at + 9] = b"Cruciblx\0"
    target.write_bytes(bytes(image))


class Run:
    """The failures of one run of the test."""

    def __init__(self) -> None:
        """Start with no failure."""
        self.problems: list[str] = []

    def expect(self, holds: bool, text: str) -> None:
        """Record TEXT when HOLDS is false."""
        if not holds:
            self.problems.append(text)

    def refused(self, call: Callable[[], object], needle: str, text: str) -> None:
        """Expect CALL to raise CrucibleLevelError with NEEDLE in its message."""
        try:
            call()
        except crucible_native.CrucibleLevelError as refusal:
            self.expect(needle in str(refusal), f"{text}: the message does not name {needle!r}: {refusal}")
            return
        except Exception as other:  # noqa: BLE001 - each other error is a failure of the check
            self.problems.append(f"{text}: {type(other).__name__} in place of a refusal of the level: {other}")
            return
        self.problems.append(f"{text}: no refusal")


def with_environment(values: dict[str, str], call: Callable[[], object]) -> object:
    """Run CALL with VALUES in the environment and with each other level variable removed."""
    names = (crucible_native.LEVEL_ROOT_VARIABLE, crucible_native.LEVEL_REQUEST_VARIABLE)
    cleared = {name: values.get(name, "") for name in names}
    with mock.patch.dict(os.environ, cleared):
        for name in names:
            if not cleared[name]:
                del os.environ[name]
        return call()


def check_built(run: Run, build: Path, host: int) -> None:
    """The notes of the libraries of the build."""
    probe_note = crucible_native.level_note(str(build / PROBE))
    run.expect(probe_note is not None and (probe_note.level, probe_note.kind) == (1, LevelKind.EXACT),
               f"the probe of the build has the note {probe_note}, not exact v1")
    notes = [crucible_native.level_note(str(build / name)) for name in (VESSEL, DISPATCH) if (build / name).exists()]
    run.expect(bool(notes) and all(note is not None for note in notes), f"a vessel library of {build} has no note")
    pairs = {(note.level, note.kind) for note in notes if note is not None}
    run.expect(len(pairs) == 1, f"the vessel libraries of {build} have the notes {pairs}")
    run.expect(all(level <= host for level, _ in pairs), f"the vessel libraries of {build} are above v{host}")
    path = with_environment({}, lambda: crucible_native.checked_lib_path(VESSEL, None, crucible_native.RELEASE_BUILD))
    run.expect(Path(str(path)).parent == build, f"the loader takes {path}, not the library of {build}")


def check_host(run: Run, build: Path, host: int) -> None:
    """The pick of the probe agrees with /proc/cpuinfo, and a request at or below the host gives its level."""
    probe = build / PROBE
    picked = with_environment({}, lambda: crucible_native.picked_level(probe))
    run.expect(picked == host, f"the probe picks v{picked}, and /proc/cpuinfo gives v{host}")
    for level in range(1, host + 1):
        request = {crucible_native.LEVEL_REQUEST_VARIABLE: f"v{level}"}
        got = with_environment(request, lambda: crucible_native.picked_level(probe))
        run.expect(got == level, f"the request v{level} picks v{got}")


def check_fatal(run: Run, build: Path) -> None:
    """A request that is no level ends the process, and the message names the text."""
    code = ("import sys; from pathlib import Path; sys.path.insert(0, sys.argv[1]); import crucible_native; "
            "crucible_native.picked_level(Path(sys.argv[2]))")
    environment = dict(os.environ, **{crucible_native.LEVEL_REQUEST_VARIABLE: "v5"})
    environment.pop(crucible_native.LEVEL_ROOT_VARIABLE, None)
    child = subprocess.run([sys.executable, "-E", "-B", "-c", code, str(Path(__file__).resolve().parent),
                            str(build / PROBE)], env=environment, capture_output=True, text=True, timeout=600,
                           check=False)
    run.expect(child.returncode < 0, f"the request v5 ends the child with {child.returncode}, not a signal")
    run.expect("the request of an x86-64 level is 'v5'" in child.stderr,
               f"the request v5 ends the child with the text {child.stderr!r}")


def check_root(run: Run, build: Path, host: int, folder: Path) -> None:
    """A level root gives the library of the picked level."""
    root = folder / "root"
    root.mkdir()
    shutil.copyfile(build / PROBE, root / PROBE)
    for level in range(1, 5):
        for name in (VESSEL, DISPATCH):
            plant(build / PROBE, root / f"x86-64-v{level}" / name, level, LevelKind.EXACT)

    def path_of(request: str) -> str:
        """The path that the loader takes from the root with the request REQUEST."""
        values = {crucible_native.LEVEL_ROOT_VARIABLE: str(root), crucible_native.LEVEL_REQUEST_VARIABLE: request}
        return str(with_environment(values, lambda: crucible_native.checked_lib_path(VESSEL, None, "")))

    def named(path: Path, request: str) -> str:
        """The path that the loader gives back for the named library PATH, with the request REQUEST and no root."""
        values = {crucible_native.LEVEL_REQUEST_VARIABLE: request}
        return str(with_environment(values, lambda: crucible_native.checked_lib_path(VESSEL, str(path), "")))

    run.expect(path_of("") == str(root / f"x86-64-v{host}" / VESSEL), f"the root with no request gives {path_of('')}")
    for level in range(1, host + 1):
        got = path_of(f"v{level}")
        run.expect(got == str(root / f"x86-64-v{level}" / VESSEL), f"the root with the request v{level} gives {got}")
        # A caller can name a path that the root gave, and the probe of the root then reads the level.
        try:
            again = named(Path(got), f"v{level}")
        except crucible_native.CrucibleLevelError as refusal:
            again = f"a refusal: {refusal}"
        run.expect(again == got, f"the named library {got} of the root with the request v{level} gives {again}")


def check_refused(run: Run, build: Path, folder: Path) -> None:
    """The loader refuses each library whose note does not suit, before it loads it."""
    probe = build / PROBE
    root = folder / "refused-root"
    root.mkdir()
    shutil.copyfile(probe, root / PROBE)
    plant(probe, root / "x86-64-v2" / VESSEL, 4, LevelKind.EXACT)
    plant(probe, root / "x86-64-v1" / VESSEL, 1, LevelKind.HOST)
    loaded: list[str] = []
    real_cdll = crucible_native.ctypes.CDLL

    def recording_cdll(path: str, *args: object, **kwargs: object) -> object:
        """Record each path that the loader opens, and open only the probe."""
        loaded.append(str(path))
        return real_cdll(path, *args, **kwargs)

    def load_from_root(request: str) -> object:
        """Load the vessel library of the planted root with the request REQUEST."""
        values = {crucible_native.LEVEL_ROOT_VARIABLE: str(root), crucible_native.LEVEL_REQUEST_VARIABLE: request}
        return with_environment(values, crucible_native.load_vessel_library)

    with mock.patch.object(crucible_native.ctypes, "CDLL", recording_cdll):
        run.refused(lambda: load_from_root("v2"), "a level root takes only an exact library of the picked level v2",
                    "a library of v4 in the directory of v2")
        run.refused(lambda: load_from_root("v1"), "of the kind host", "a library of the kind host in a root")
        run.refused(lambda: load_from_root("v3"), "has no libcrucible_vessel.so at the picked level v3",
                    "a root with no library of the picked level")

        def load_named(path: Path, request: str) -> object:
            """Load the named vessel library PATH with the request REQUEST and no root."""
            values = {crucible_native.LEVEL_REQUEST_VARIABLE: request}
            return with_environment(values, lambda: crucible_native.load_vessel_library(str(path)))

        run.refused(lambda: load_named(root / "x86-64-v2" / VESSEL, "v3"), "the picked level is v3",
                    "a named library of v4 in a level directory of a root, with the request v3")
        plant(probe, root / "x86-64-v5" / VESSEL, 1, LevelKind.EXACT)
        run.refused(lambda: load_named(root / "x86-64-v5" / VESSEL, ""),
                    f"{root / 'x86-64-v5' / PROBE} does not exist",
                    "a named library of a directory that is no level directory, with no probe beside it")

        flat = folder / "flat"
        plant(probe, flat / VESSEL, 4, LevelKind.HOST)
        shutil.copyfile(probe, flat / PROBE)
        request = {crucible_native.LEVEL_REQUEST_VARIABLE: "v2"}
        run.refused(lambda: with_environment(request, lambda: crucible_native.load_vessel_library(str(flat / VESSEL))),
                    "the picked level is v2", "a library of a build above the requested level")

        bare = folder / "bare"
        bare.mkdir()
        shutil.copyfile(probe, bare / PROBE)
        plant_without_note(probe, bare / VESSEL)
        run.refused(lambda: with_environment({}, lambda: crucible_native.load_vessel_library(str(bare / VESSEL))),
                    "has no level note", "a library with no note")

        pair = folder / "pair"
        plant(probe, pair / VESSEL, 1, LevelKind.HOST)
        plant(probe, pair / DISPATCH, 2, LevelKind.HOST)
        shutil.copyfile(probe, pair / PROBE)
        run.refused(lambda: with_environment({}, lambda: crucible_native.load_vessel_library(str(pair / VESSEL))),
                    "must have one level", "a pair of two notes")

    opened = [path for path in loaded if not path.endswith(PROBE)]
    run.expect(not opened, f"the loader opened a refused library: {opened}")


def check_loaded(run: Run) -> None:
    """With a level root in the environment, the loader loads the pair of the picked level of the root."""
    root_text = os.environ.get(crucible_native.LEVEL_ROOT_VARIABLE, "")
    if not root_text:
        return
    root = Path(root_text)
    picked = crucible_native.picked_level(root / PROBE)
    request = os.environ.get(crucible_native.LEVEL_REQUEST_VARIABLE, "")
    run.expect(not request or request == f"v{picked}", f"the request {request} picks v{picked}")
    crucible_native.load_vessel_library()
    crucible_native.load_dispatch_library()
    names = (VESSEL, DISPATCH)
    loaded = {
        Path(words[-1])
        for words in (line.split() for line in Path("/proc/self/maps").read_text().splitlines())
        if words and words[-1].endswith(names)
    }
    folder = root / f"x86-64-v{picked}"
    run.expect(loaded == {folder / name for name in names},
               f"the root {root} with the pick v{picked} loads {sorted(str(path) for path in loaded)}, not the pair "
               f"of {folder}")


def main() -> int:
    """Run each check and report."""
    if not crucible_native._is_x86_64():
        print("test_isa_level: the host is not x86_64, and its libraries carry no note")
        return 0
    found = crucible_native._find_lib(PROBE)
    if found is None:
        print(f"  FAIL: no {PROBE} in the build.  Build it with:\n{crucible_native.RELEASE_BUILD}")
        return 1
    build = Path(found).parent
    host = cpuinfo_level()
    run = Run()
    check_built(run, build, host)
    check_host(run, build, host)
    check_fatal(run, build)
    with tempfile.TemporaryDirectory(prefix="test_isa_level_") as temporary:
        folder = Path(temporary)
        check_root(run, build, host, folder)
        check_refused(run, build, folder)
    check_loaded(run)
    for line in run.problems:
        print(f"  FAIL: {line}")
    if run.problems:
        return 1
    print("test_isa_level: each check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
