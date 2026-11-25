"""Compile the probe of the keyed frame door in each form, and fail when one call of the door costs too much.

usage: check_door_cost.py BUILD_DIR SOURCE_ROOT [--measure], or check_door_cost.py --selftest

Probe
    door_cost_with_mut.cpp of this directory gives four writers: a raw loop
    on a C array of 16 words, the keyed frame door with_mut of a keyed
    FixedArray<uint64_t, 16> with the same loop in its body, and the same two
    forms with a body that gives a small class (DOORS).  Each call of a door
    instantiates a body of its own and a ViewMut class of a fresh brand, so a
    cost of the door for each call is a cost of each unit that calls it.  The
    raw loop is the floor, and no door of an owner changes it.

Compile
    The compile of the probe is the compile of REFERENCE_UNIT, the check of
    foundation/core/Scan.h, in the compile database of the build, with the
    probe as its source and the object in a temporary directory.  The compile
    is full: the door and the loop differ in the front end, in the code and in
    the debug information, and a parse alone counts only the first.

Count
    The count is the user instructions of the compiler and of each process
    that it starts, from a hardware counter (the instruction count of
    utils/assay/core/meter.py), as check_ceilings.py counts.  The cost of one
    call of a form is the count of the unit with CALLS instantiations of the
    form, minus the count of the unit with none, divided by CALLS.  The count
    of the headers then cancels.  A host with no exact counter, and a build
    with no REFERENCE_UNIT, give the exit SKIPPED.

Ceilings
    The kind of the build comes from the words of its compile (kind_of).
      asan       a word -fsanitize=address, as in the presets default and
                 bench-fuzz.  The sanitizer and the debug code make the loop
                 of a FixedArray cost many times the raw loop, and the door
                 costs at most CEILING_RATIO times that loop.  So one call of a
                 door costs at most CEILING_RATIO x ARRAY_LOOP_RATIO times one
                 call of its raw loop.  ARRAY_LOOP_RATIO is the measure of the
                 loop on the array against the raw loop on main df8fb4e97.  A
                 word that joins the address sanitizer with a second one, such
                 as -fsanitize=address,undefined, is no such word.
      tsan       a word -fsanitize=thread.  The loop costs about 0.011 G, and
                 each door that takes a closure costs more than 1.4 times
                 that, so the ceiling is the measure of one call of the door
                 with a margin of MARGIN (ABSOLUTE_CEILINGS).
      optimized  -O3 and no sanitizer with a runtime, as in the presets
                 release, release-v1 to release-v4, bench, verify and vessel:
                 an absolute ceiling, as for tsan, for each target level of
                 the last -march word.  Each such preset has trap flags: some
                 words -fsanitize= with the word TRAP_WORD.  They give no
                 runtime and no call, so they do not change the kind.  The
                 level changes the cost: x86-64 and x86-64-v2 cost about 14 %
                 more than native.  A level with no measure has no kind.
    A build of a different kind, such as ubsan-strict, aarch64 or a build
    with a profile or the analyzer (UNMEASURED_PREFIXES), compiles nothing
    and gives the exit SKIPPED.

--measure prints the cost of each form and judges no ceiling.  --selftest
compiles nothing and checks the kind of the words of each preset.  Exit 0 when
each door is at or below its ceiling, 1 when one is above it or a compile
fails, 2 on a usage error, 77 for a skip.  Complexity: at most 1 + 2 x
len(DOORS) compiles.
"""

from __future__ import annotations

import json
import shlex
import sys
import tempfile
from pathlib import Path

# The unit of the compile database whose flags the probe gets (the compile).
REFERENCE_UNIT = "test/layer/checks/foundation/core/Scan.cpp"
PROBE_NAME = "door_cost_with_mut.cpp"
# The instantiations of a form in the counted unit (the count).
CALLS = 32
# Each door form and the raw loop form of its body (the probe).
DOORS = {"DoorWriter": "RawWriter", "DoorSummer": "RawSummer"}
# The most that one call of a door costs, as a multiple of one call of the loop on the array (the ceilings).  An
# emptied source of a moved unique view and the test of a large fixed run raise the keyed doors in the asan kind to
# 0.1081 G and 0.1276 G, which is 1.112 and 1.118 times the loop.  The ceiling keeps a margin of 2 % over the higher
# one, by the owner decision of 2026-11-15 that a correct change raises its limit.  The other kinds rise by about 4 %
# and stay below their ceilings.
CEILING_RATIO = 1.14
# The loop on the FixedArray against the raw loop, in the asan kind, on main df8fb4e97: 0.0972 G against
# 0.0074 G for the writer, and 0.1132 G against 0.0230 G for the summer.  The doors of an array with no key
# measured 0.1014 G and 0.1215 G there.  The keyed doors measure 0.1035 G and 0.1230 G.
ARRAY_LOOP_RATIO = {"DoorWriter": 13.14, "DoorSummer": 4.92}
# The margin of an absolute ceiling over its measure.
MARGIN = 1.10
# The measure of one call of each door, in G, with the store check of the body of a frame door
# (StoresNoBorrowOutside of foundation/core/CaptureStore.h), on main fae4f7ff7 with the commits of that check.
# An optimized build has one measure for each target level of its last -march word, because the level changes
# the cost: release, bench, verify and vessel (native), release-v4, release-v3, release-v2 and release-v1.
MEASURED_CALLS = {
    "tsan": {"DoorWriter": 0.0282, "DoorSummer": 0.0336},
    "optimized native": {"DoorWriter": 0.0303, "DoorSummer": 0.0379},
    "optimized x86-64-v4": {"DoorWriter": 0.0303, "DoorSummer": 0.0379},
    "optimized x86-64-v3": {"DoorWriter": 0.0311, "DoorSummer": 0.0387},
    "optimized x86-64-v2": {"DoorWriter": 0.0341, "DoorSummer": 0.0418},
    "optimized x86-64": {"DoorWriter": 0.0341, "DoorSummer": 0.0418},
}
ABSOLUTE_CEILINGS = {kind: {door: MARGIN * value for door, value in doors.items()}
                     for kind, doors in MEASURED_CALLS.items()}
# The words of a compile whose cost no ceiling measured: the profile of the presets pgo-generate, pgo and
# pgo-release, and the analyzer of the preset analyzer.  Such a build has no kind (the ceilings).
UNMEASURED_PREFIXES = ("-fprofile-", "-fanalyzer", "-fauto-profile")
# The word that makes each sanitizer of a compile trap, with no runtime (the ceilings).
TRAP_WORD = "-fsanitize-trap=all"
# The words of a compile of each preset that decide its kind, and the kind (--selftest).
TRAPS = "-fsanitize=signed-integer-overflow -fsanitize=shift -fsanitize=integer-divide-by-zero " + TRAP_WORD
KIND_CASES = (
    ("default", "-O1 -fsanitize=address", "asan"),
    ("tsan", "-O1 -fsanitize=thread", "tsan"),
    ("ubsan-strict", "-O1 -fsanitize=undefined,float-cast-overflow", None),
    ("release", f"-O3 -march=native {TRAPS}", "optimized native"),
    ("release-v3", f"-O3 -march=native -march=x86-64-v3 {TRAPS}", "optimized x86-64-v3"),
    ("an optimized build with a runtime sanitizer", "-O3 -march=native -fsanitize=undefined", None),
    ("an optimized build with no sanitizer", "-O3 -march=native", "optimized native"),
    ("a level with no measure", f"-O3 -march=armv8.2-a {TRAPS}", None),
    ("a profile", f"-O3 -march=native -fprofile-use=/p {TRAPS}", None),
)
GIGA = 1e9
# The exits of the check.
OK, FAILED, USAGE, SKIPPED = 0, 1, 2, 77
# The words that the compile of the probe drops, and the words whose next word it drops too.
DROPPED_WORDS = frozenset({"-c", "-MD", "-MMD"})
DROPPED_PAIRS = frozenset({"-o", "-MF", "-MT", "-MQ"})


def reference_command(build: Path) -> tuple[list[str], str, str] | None:
    """Return the words, the working directory and the source of the compile of REFERENCE_UNIT, or None."""
    database = build / "compile_commands.json"
    if not database.is_file():
        return None
    for entry in json.loads(database.read_text(encoding="utf-8")):
        if entry["file"].endswith(REFERENCE_UNIT):
            words = entry["arguments"] if "arguments" in entry else shlex.split(entry["command"])
            return list(words), entry["directory"], entry["file"]
    return None


def kind_of(words: list[str]) -> str | None:
    """Return the kind of the build of a compile (the ceilings), or None for a kind with no ceiling."""
    if any(word.startswith(UNMEASURED_PREFIXES) for word in words):
        return None
    if "-fsanitize=address" in words:
        return "asan"
    if "-fsanitize=thread" in words:
        return "tsan"
    has_sanitizer = any(word.startswith("-fsanitize=") for word in words)
    if "-O3" in words and (not has_sanitizer or TRAP_WORD in words):
        levels = [word.removeprefix("-march=") for word in words if word.startswith("-march=")]
        kind = f"optimized {levels[-1]}" if levels else None
        return kind if kind in MEASURED_CALLS else None
    return None


def probe_command(words: list[str], source: str, probe: Path, out: Path, form: str, calls: int) -> list[str]:
    """Return the compile of the probe with calls instantiations of form, and its object at out."""
    kept: list[str] = []
    skip_next = False
    for word in words:
        if skip_next:
            skip_next = False
            continue
        if word in DROPPED_PAIRS:
            skip_next = True
            continue
        if word in DROPPED_WORDS or word == source:
            continue
        kept.append(word)
    return [*kept, f"-DDOOR_COST_FORM={form}", f"-DDOOR_COST_COUNT={calls}", "-c", "-o", str(out), str(probe)]


def verdict(kind: str, door: str, per_call: dict[str, float]) -> bool:
    """Print the verdict of one door in a build of the kind, and return True when it is above its ceiling."""
    if kind == "asan":
        raw = DOORS[door]
        ceiling = CEILING_RATIO * ARRAY_LOOP_RATIO[door]
        ratio = per_call[door] / per_call[raw]
        is_over = ratio > ceiling
        print(f"check_door_cost: {door} is {ratio:.2f} x {raw}, ceiling {ceiling:.2f} x "
              f"({CEILING_RATIO:g} x the loop on the array){'  ABOVE THE CEILING' if is_over else ''}")
        return is_over
    ceiling = ABSOLUTE_CEILINGS[kind][door]
    is_over = per_call[door] > ceiling
    print(f"check_door_cost: {door} in the kind {kind}: ceiling {ceiling:.4f} G "
          f"({MARGIN:g} x {MEASURED_CALLS[kind][door]:.4f} G){'  ABOVE THE CEILING' if is_over else ''}")
    return is_over


def selftest() -> int:
    """Check the kind of the compile words of each preset (KIND_CASES), and return the exit."""
    failures = 0
    for name, words, expected in KIND_CASES:
        found = kind_of(words.split())
        is_right = found == expected
        failures += int(not is_right)
        print(f"check_door_cost: {'ok  ' if is_right else 'FAIL'} the kind of {name} is {found}, expected {expected}")
    return FAILED if failures else OK


def main(argv: list[str]) -> int:
    """Compile the probe in each form, and judge the cost of each door (the ceilings)."""
    if argv == ["--selftest"]:
        return selftest()
    if len(argv) not in (2, 3) or (len(argv) == 3 and argv[2] != "--measure"):
        print(__doc__.splitlines()[2], file=sys.stderr)
        return USAGE
    build, root = Path(argv[0]).resolve(), Path(argv[1]).resolve()
    is_measure = len(argv) == 3
    sys.path.insert(0, str(root / "utils"))
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from check_ceilings import counted_compile

    found = reference_command(build)
    if found is None:
        print(f"check_door_cost: the compile database of {build} has no {REFERENCE_UNIT}, so the check skips")
        return SKIPPED
    words, directory, source = found
    kind = kind_of(words)
    if kind is None and not is_measure:
        print("check_door_cost: the compile of this build has no kind with a ceiling, so the check skips (THE CEILINGS)")
        return SKIPPED
    probe = Path(__file__).resolve().parent / PROBE_NAME
    # A build of an absolute kind needs no raw loop.  --measure counts each form.
    forms = [form for door, raw in DOORS.items()
             for form in ((raw, door) if is_measure or kind == "asan" else (door,))]
    per_call: dict[str, float] = {}
    with tempfile.TemporaryDirectory() as scratch:
        out = Path(scratch) / "probe.o"
        base, base_bytes = 0, 0
        for calls, form in [(0, forms[0]), *((CALLS, form) for form in forms)]:
            count, error = counted_compile(probe_command(words, source, probe, out, form, calls), directory)
            if count is None:
                print("check_door_cost: the host gives no exact instruction counter, so the check skips")
                return SKIPPED
            if count < 0:
                print(f"check_door_cost: {PROBE_NAME} does not compile with {calls} calls of {form}: {error}")
                return FAILED
            if calls == 0:
                base, base_bytes = count, out.stat().st_size
                continue
            per_call[form] = (count - base) / CALLS / GIGA
            print(f"check_door_cost: one call of {form} {per_call[form]:.4f} G and "
                  f"{(out.stat().st_size - base_bytes) / CALLS:.0f} object bytes")
    if is_measure or kind is None:
        return OK
    failures = sum(int(verdict(kind, door, per_call)) for door in DOORS)
    return FAILED if failures else OK


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
