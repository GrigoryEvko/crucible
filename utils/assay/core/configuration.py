"""configuration — the name of the configuration of a build, and the test of a build that only parses.

THE CONFIGURATION
    The name of a configuration is the build kind of a build
    (assay.core.target), for example x86_64-debug-asan.  The name adds -bench
    when CRUCIBLE_BENCH is on, -fuzz when CRUCIBLE_FUZZ is on and -syntax
    when CRUCIBLE_SYNTAX_ONLY is on.  A build with CRUCIBLE_SYNTAX_ONLY only
    parses its units (is_parse_build(), `loop syntax`).

    A different configuration compiles other objects or other preprocessor
    arms.  Its findings, its symbols and its totals can then be different.
    One set of rows of a counted ledger judges each build, and its count of a
    key is the largest count over the configurations of the landing gate
    (THE REDUCE of assay.core.ledger).  A rule with rows of one configuration
    only, such as the rule instruction-total, reads the name.

`guard selftest tests.core.test_configuration` does a test of the module.
"""

import dataclasses
from pathlib import Path

from assay.core import target as build_target
from assay.readers import cmake

# The cache option of a build that only parses its units (is_parse_build()).
SYNTAX_OPTION = "CRUCIBLE_SYNTAX_ONLY"
# The options that add a word to the name of a configuration.
NAME_OPTIONS = (("bench", "CRUCIBLE_BENCH"), ("fuzz", "CRUCIBLE_FUZZ"), ("syntax", SYNTAX_OPTION))
CMAKE_FALSE = frozenset({"", "0", "OFF", "NO", "FALSE", "N", "IGNORE", "NOTFOUND"})


class ConfigurationError(Exception):
    """The configuration of a build cannot be read: its build kind or its CMake cache."""


@dataclasses.dataclass(frozen=True, slots=True)
class BuildConfiguration:
    """The name of the configuration of a build (THE CONFIGURATION)."""

    name: str


def normalized(kind: str, value: str) -> str:
    """Return a cache value as one word: on or off for a BOOL, set or unset for a path, else the value or unset."""
    is_false = value.upper() in CMAKE_FALSE or value.upper().endswith("-NOTFOUND")
    if kind == "BOOL":
        return "off" if is_false else "on"
    if kind in ("PATH", "FILEPATH"):
        return "unset" if is_false else "set"
    return value if value.strip() else "unset"


def is_parse_build(build_dir: Path) -> bool:
    """Return True when the CMake cache of the build sets SYNTAX_OPTION: the build then only parses its units.

    A cache that cannot be read gives False, and the caller then reads the objects of the build.
    """
    try:
        entry = cmake.cache(build_dir).get(SYNTAX_OPTION)
    except cmake.CmakeError:
        return False
    return entry is not None and normalized("BOOL", entry.value) == "on"


def read_configuration(build_dir: Path) -> BuildConfiguration:
    """Return the configuration of a build: its build kind and the word of each option of NAME_OPTIONS that is on.

    Raises:
        ConfigurationError: If the build has no build-kind.txt, or its CMake cache cannot be read
    """
    kind = build_target.kind_of(build_dir)
    if kind is None:
        raise ConfigurationError(f"{build_dir} has no build-kind.txt, which cmake/BuildLauncher.cmake writes")
    try:
        entries = cmake.cache(build_dir)
    except cmake.CmakeError as problem:
        raise ConfigurationError(f"the CMake cache of {build_dir} cannot be read: {problem}") from None
    words = [
        word
        for word, name in NAME_OPTIONS
        if (entry := entries.get(name)) is not None and normalized(entry.type_name, entry.value) == "on"
    ]
    return BuildConfiguration("-".join([kind, *words]))
