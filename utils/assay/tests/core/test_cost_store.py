"""The test of the cost store of the build launcher (assay.core.cost_store): the key, the entries and the bound.

`guard selftest tests.core.test_cost_store` runs the test.  Each case
writes one line, in a scratch root of the caches.  Exit 0 when each case
holds, else 1.
"""

import os
import tempfile
from pathlib import Path

from assay.core import cache, cache_root, cost_store, testing

# A compile command of a build directory in a source root, as `loop step` gives it.
COMMAND = (
    "/usr/bin/ccache",
    "base_dir={root}",
    "/opt/gcc/bin/g++",
    "-I{root}/include",
    "-fdebug-prefix-map={root}=..",
    "-fdebug-prefix-map={build}=.",
    "-fplugin-arg-crucible_quarantine-build={build}",
    "-o",
    "test/unit.o",
    "-c",
    "{root}/test/unit.cpp",
)


def command(root: str, build: str, *extra: str) -> list[str]:
    """Return COMMAND for a source root and a build directory, with more words at its end."""
    return [word.format(root=root, build=build) for word in COMMAND] + list(extra)


def relocation_cases(expect: testing.Expect) -> None:
    """Check that relocated() gives the mark of a root only at the boundaries of a path (THE KEY)."""
    roots = [("/work/tree/build", cost_store.BUILD_MARK), ("/work/tree", cost_store.ROOT_MARK)]
    shapes = {
        "-I/work/tree/include": "-I<root>/include",
        "/work/tree/src/a.cpp": "<root>/src/a.cpp",
        "base_dir=/work/tree": "base_dir=<root>",
        "-fdebug-prefix-map=/work/tree=..": "-fdebug-prefix-map=<root>=..",
        "-fdebug-prefix-map=/work/tree/build=.": "-fdebug-prefix-map=<build>=.",
        "/work/tree/build/test/unit.o": "<build>/test/unit.o",
        "-Dpaths=/work/tree:/work/tree/build": "-Dpaths=<root>:<build>",
        "/work/tree2/src/a.cpp": "/work/tree2/src/a.cpp",
        "/other/work/tree/a.cpp": "/other/work/tree/a.cpp",
        "-I/work/treex": "-I/work/treex",
    }
    found = {word: cost_store.relocated(word, roots) for word in shapes}
    expect(
        "relocated() gives the build directory and the source root their marks where a path starts and ends, also "
        "after an option such as -I, and keeps a path that only holds the text of a root",
        found == shapes,
    )


def key_cases(expect: testing.Expect, scratch: Path) -> None:
    """Check that the key holds the bytes of the object and the command with its roots as marks (THE KEY)."""
    places = [scratch / "first", scratch / "second" / "deeper"]
    objects = []
    for place in places:
        (place / "build" / "test").mkdir(parents=True)
        objects.append(place / "build" / "test" / "unit.o")
        objects[-1].write_bytes(b"planted object: the same bytes")
    first, second = (
        cost_store.key_of(str(obj), command(str(place), str(place / "build")), str(place / "build"), str(place))
        for obj, place in zip(objects, places, strict=True)
    )
    expect("a key is a SHA-256 in hexadecimal digits", first is not None and len(first) == 64)
    expect(
        "the same bytes and the same command in two places give one key, so a hit in a different work tree finds "
        "the cost",
        first == second,
    )
    other_flag = cost_store.key_of(
        str(objects[0]),
        command(str(places[0]), str(places[0] / "build"), "-DONE_MORE"),
        str(places[0] / "build"),
        str(places[0]),
    )
    expect("a different option gives a different key, also with the same bytes", other_flag not in (None, first))
    objects[1].write_bytes(b"planted object: other bytes")
    changed = cost_store.key_of(
        str(objects[1]), command(str(places[1]), str(places[1] / "build")), str(places[1] / "build"), str(places[1])
    )
    expect("different bytes give a different key", changed not in (None, first))
    expect(
        "an object that cannot be read gives no key",
        cost_store.key_of(str(scratch / "missing.o"), ["c++"], None, str(scratch)) is None,
    )
    expect(
        "an object in no build directory gets a key with the source root alone",
        cost_store.key_of(str(objects[0]), ["c++", f"{places[0]}/a.cpp"], None, str(places[0])) is not None,
    )


def entry_cases(expect: testing.Expect, scratch: Path) -> None:
    """Check the write and the read of an entry, its touch, a damaged entry and the caches off (THE STORE)."""
    key = "ab" + "0" * 62
    block = '{"cpu_s": 1.000, "end": 2.000}'
    with testing.environment_variable(cache_root.ROOT_VARIABLE, str(scratch / "caches")):
        expect("a key that the store does not hold gives no block", cost_store.get(key, 0.0) is None)
        cost_store.put(key, block)
        directory = cost_store.store_directory()
        entry = Path(cost_store.entry_path(directory or "", key))
        expect(
            "put() writes the block in the file of the key below the first two digits, and get() reads it back",
            directory == str(scratch / "caches" / cost_store.STORE_NAME)
            and entry.parent.name == "ab"
            and cost_store.get(key, 0.0) == block
            and not [path for path in sorted(entry.parent.iterdir()) if path.name.startswith(".")],
        )
        old = entry.stat().st_mtime - 2 * cache_root.TOUCH_INTERVAL
        os.utime(entry, (old, old))
        cost_store.get(key, old + cache_root.TOUCH_INTERVAL / 2)
        kept = entry.stat().st_mtime
        cost_store.get(key, old + 2 * cache_root.TOUCH_INTERVAL)
        expect(
            "a use within TOUCH_INTERVAL of the last touch keeps the mtime, and a later use touches the entry",
            kept == old and entry.stat().st_mtime > old,
        )
        entry.write_text("\n", encoding="utf-8")
        expect("an empty entry gives no block", cost_store.get(key, 0.0) is None)
    with testing.environment_variable(cache_root.ROOT_VARIABLE, cache_root.OFF):
        cost_store.put("cd" + "0" * 62, block)
        expect(
            "with the caches off, put() writes nothing and get() gives no block",
            cost_store.store_directory() is None and cost_store.get(key, 0.0) is None,
        )


def bound_cases(expect: testing.Expect, scratch: Path) -> None:
    """Check that hold_bound() takes a due turn, keeps an entry of each age, and holds the byte limit (THE STORE)."""
    with testing.environment_variable(cache_root.ROOT_VARIABLE, str(scratch / "bounded")):
        expect("with no store directory, hold_bound() takes no turn", not cost_store.hold_bound())
        aged_key, used_key = "ef" + "1" * 62, "ef" + "2" * 62
        for key in (aged_key, used_key):
            cost_store.put(key, "{}")
        directory = cost_store.store_directory() or ""
        aged = Path(cost_store.entry_path(directory, aged_key))
        old = aged.stat().st_mtime - 2 * cache.MAX_AGE
        os.utime(aged, (old, old))
        expect(
            "a first turn is due, and it keeps an entry past the age limit of the other caches, because ccache keeps "
            "an object with no age limit",
            cost_store.hold_bound() and aged.exists() and Path(cost_store.entry_path(directory, used_key)).exists(),
        )
        expect("a second turn within the interval is not due", not cost_store.hold_bound())
    keys = [f"{index:02x}" + "3" * 62 for index in range(8)]
    with testing.environment_variable(cache_root.ROOT_VARIABLE, str(scratch / "sized")):
        directory = ""
        # keys[0] has the newest use, and each next key a use 100 s older.
        for age, key in enumerate(keys, start=1):
            cost_store.put(key, "{}")
            directory = cost_store.store_directory() or ""
            used = Path(cost_store.entry_path(directory, key)).stat().st_mtime - 100 * age
            os.utime(cost_store.entry_path(directory, key), (used, used))
        size = cache.allocated(os.stat(cost_store.entry_path(directory, keys[0])))
        saved = cache.LIMITS[cost_store.STORE_NAME]
        cache.LIMITS[cost_store.STORE_NAME] = 4 * size
        try:
            took = cost_store.hold_bound()
        finally:
            cache.LIMITS[cost_store.STORE_NAME] = saved
        kept = [key for key in keys if os.path.exists(cost_store.entry_path(directory, key))]
        expect(
            "over the byte limit, the entries with the oldest use go until the rest is less than EVICT_TO of the limit, "
            "and the entry of the newest use stays",
            took and 0 < len(kept) <= int(cache.EVICT_TO * 4) and kept == keys[: len(kept)],
        )
    with testing.environment_variable(cache_root.ROOT_VARIABLE, cache_root.OFF):
        expect("with the caches off, hold_bound() takes no turn", not cost_store.hold_bound())


def store_cases(expect: testing.Expect) -> None:
    """Run the cases of the key, the entries and the bound in a scratch directory."""
    with tempfile.TemporaryDirectory(prefix="test-cost-store-") as scratch:
        key_cases(expect, Path(scratch))
        entry_cases(expect, Path(scratch))
        bound_cases(expect, Path(scratch))
    expect("the store has a row of LIMITS", cache.LIMITS.get(cost_store.STORE_NAME, 0) > 0)


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (relocation_cases, store_cases))
