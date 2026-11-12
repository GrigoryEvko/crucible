"""The test of the configuration of a build (assay.core.configuration).

`guard selftest tests.core.test_configuration` runs the test.  Each case plants
a build directory in a throwaway directory.  The cases hold the one word of a
cache value, the test of a parse build and the name of a configuration.
Exit 0 when each case holds, else 1.
"""

import tempfile
from pathlib import Path

from assay.core import configuration as build_config
from assay.core import testing
from assay.readers import cmake


def cache_cases(expect: testing.Expect) -> None:
    """Hold the one word of a cache value, and the test of a parse build."""
    expect(
        "normalized reads a BOOL, a path and a string",
        (
            build_config.normalized("BOOL", "TRUE"),
            build_config.normalized("BOOL", "OFF"),
            build_config.normalized("PATH", "x-NOTFOUND"),
            build_config.normalized("FILEPATH", "/usr/bin/x"),
            build_config.normalized("STRING", ""),
            build_config.normalized("STRING", "Debug"),
        )
        == ("on", "off", "unset", "set", "unset", "Debug"),
    )
    with tempfile.TemporaryDirectory(prefix="test-configuration-") as work:
        expect("a build with no CMake cache is no parse build", not build_config.is_parse_build(Path(work)))
        cmake.plant(Path(work), cache={build_config.SYNTAX_OPTION: ("BOOL", "ON")})
        expect("a build whose cache sets the syntax option is a parse build", build_config.is_parse_build(Path(work)))
        cmake.plant(Path(work), cache={build_config.SYNTAX_OPTION: ("BOOL", "OFF")})
        expect(
            "a build whose cache clears the syntax option is no parse build",
            not build_config.is_parse_build(Path(work)),
        )
    expect(
        "the syntax option is a word of the name", ("syntax", build_config.SYNTAX_OPTION) in build_config.NAME_OPTIONS
    )


def name_cases(expect: testing.Expect) -> None:
    """Hold the name of a configuration: the build kind and the word of each option that is on (THE CONFIGURATION)."""
    with tempfile.TemporaryDirectory(prefix="test-configuration-") as work:
        build = Path(work)
        try:
            build_config.read_configuration(build)
            expect("a build with no build-kind.txt is a ConfigurationError", False)
        except build_config.ConfigurationError as problem:
            expect("a build with no build-kind.txt is a ConfigurationError", "build-kind.txt" in str(problem))
        (build / "build-kind.txt").write_text("x86_64-debug-asan\n", encoding="utf-8")
        try:
            build_config.read_configuration(build)
            expect("a build with no CMake cache is a ConfigurationError", False)
        except build_config.ConfigurationError as problem:
            expect("a build with no CMake cache is a ConfigurationError", "CMake cache" in str(problem))
        cmake.plant(build, cache={"CMAKE_BUILD_TYPE": ("STRING", "Debug")})
        expect(
            "the name of a build with no option on is its build kind",
            build_config.read_configuration(build) == build_config.BuildConfiguration("x86_64-debug-asan"),
        )
        cmake.plant(
            build,
            cache={
                "CRUCIBLE_BENCH": ("BOOL", "ON"),
                "CRUCIBLE_FUZZ": ("BOOL", "ON"),
                build_config.SYNTAX_OPTION: ("BOOL", "OFF"),
            },
        )
        expect(
            "the name adds the word of each option that is on, in the sequence of NAME_OPTIONS",
            build_config.read_configuration(build).name == "x86_64-debug-asan-bench-fuzz",
        )
        (build / "build-kind.txt").write_text("x86_64-release\n", encoding="utf-8")
        cmake.plant(build, cache={"CMAKE_BUILD_TYPE": ("STRING", "Release"), "CRUCIBLE_REQUIRE_VESSEL": ("BOOL", "ON")})
        expect(
            "a build that must compile and test the vessel has the word vessel",
            build_config.read_configuration(build).name == "x86_64-release-vessel",
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (cache_cases, name_cases))
