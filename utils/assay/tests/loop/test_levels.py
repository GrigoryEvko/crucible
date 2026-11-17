"""The test of `loop levels` (assay.loop.levels).

`guard selftest tests.loop.test_levels` runs the test.  Each case writes one line.  Exit 0 when each case holds, else 1.

The cases plant a presets file with the preset vessel and the four level presets in a scratch tree.  They read the
levels from it, and they run the steps of each level with a planted runner that records each command and its
variables, and with a planted level of the host.  They hold the words of each step in each share of THE JOBS, the share
that each environment gives, the level above the host with no test run, a step that fails, each level that fails, and
the presets that do not read.  One case runs a real command through the runner of a call.
"""

import json
import os
import sys
import tempfile
from collections.abc import Mapping, Sequence
from pathlib import Path
from unittest import mock

from assay.core import jobserver, testing, x86level
from assay.loop import gate, heavy
from assay.loop import levels as loop_levels

# The flags of the planted level presets.
FLAGS = {
    "v1": "-O1 -march=x86-64 -DNDEBUG -g",
    "v2": "-O1 -march=x86-64-v2 -DNDEBUG -g",
    "v3": "-O1 -march=x86-64-v3 -DNDEBUG -g",
    "v4": "-O1 -march=x86-64-v4 -DNDEBUG -g",
}


def plant_presets(root: Path, skipped: str = "") -> None:
    """Write a presets file with the preset release, the preset vessel and the level presets, apart from `skipped`."""
    configure: list[dict[str, object]] = [
        {"name": "release", "binaryDir": "${sourceDir}/build-release", "cacheVariables": {"CMAKE_CXX_FLAGS": "-O1"}},
        {"name": gate.VESSEL, "inherits": "release", "binaryDir": "${sourceDir}/build-vessel"},
    ]
    for word, flags in FLAGS.items():
        name = f"release-{word}"
        if name == skipped:
            continue
        variables = {"CMAKE_CXX_FLAGS": flags, "CRUCIBLE_X86_LEVEL": word}
        configure.append({"name": name, "inherits": "release", "cacheVariables": variables})
    (root / "CMakePresets.json").write_text(json.dumps({"version": 6, "configurePresets": configure}), encoding="utf-8")


class Recorder:
    """A planted runner: it records each command and its variables, and fails each command whose words hold a text."""

    def __init__(self, failing: str = "") -> None:
        """Start with no command, and with the text of a command that fails, or an empty text for none."""
        self.calls: list[tuple[list[str], dict[str, str]]] = []
        self.failing = failing

    def __call__(self, words: Sequence[str], variables: Mapping[str, str], log: Path) -> int:
        """Record one command, write its log, and return 1 for a failing command, else 0."""
        self.calls.append((list(words), dict(variables)))
        log.write_text("a planted log\n", encoding="utf-8")
        return 1 if self.failing and self.failing in " ".join(words) else 0


def read_cases(expect: testing.Expect) -> None:
    """Hold the levels that the presets file gives, and the presets that do not read."""
    with tempfile.TemporaryDirectory(prefix="loop-levels-") as work:
        root = Path(work)
        plant_presets(root)
        found = loop_levels.read_levels(root)
        expect(
            "the four level presets give their level, their flags and the build directory of their vessel pair",
            [(level.word, level.flags, level.build.name) for level in found]
            == [(word, flags, f"build-vessel-{word}") for word, flags in FLAGS.items()],
        )
        plant_presets(root, skipped="release-v2")
        try:
            loop_levels.read_levels(root)
            expect("a missing level preset is refused", False)
        except loop_levels.LevelsError as problem:
            expect("a missing level preset is refused, and the error names it", "release-v2" in str(problem))


def step_cases(expect: testing.Expect) -> None:
    """Hold the commands of each level, the level above the host, and the words of a step and of a call by hand."""
    with tempfile.TemporaryDirectory(prefix="loop-levels-") as work:
        root = Path(work)
        plant_presets(root)
        found = loop_levels.read_levels(root)
        recorder = Recorder()
        outcomes = loop_levels.run_levels(root, found, 48, loop_levels.Share.STEP, recorder, lambda probe: 3)
        expect(
            "each level gets its configure, build and install, and the levels at or below the host get their tests",
            [outcome.state for outcome in outcomes]
            == [
                loop_levels.State.PASSED,
                loop_levels.State.PASSED,
                loop_levels.State.PASSED,
                loop_levels.State.ABOVE_HOST,
            ]
            and len(recorder.calls) == 4 * 3 + 3,
        )
        configure = recorder.calls[0][0]
        expect(
            "the configure of a level takes the preset vessel, the build directory of the level, and the flags and "
            "the level of its preset",
            configure[:5] == ["cmake", "--preset", gate.VESSEL, "-B", str(root / "build-vessel-v1")]
            and f"-DCMAKE_CXX_FLAGS={FLAGS['v1']}" in configure
            and "-DCRUCIBLE_X86_LEVEL=v1" in configure,
        )
        build = recorder.calls[1][0]
        install = recorder.calls[2][0]
        expect(
            "in a heavy step of an entry, the build takes no lease and gets -j with the job count of the step",
            build[:3] == ["cmake", "--build", str(root / "build-vessel-v1")]
            and build[-2:] == ["-j", "48"]
            and all(target in build for target in gate.VESSEL_TARGETS),
        )
        expect(
            "the install puts the component vessel into the level root of the tree",
            install
            == [
                "cmake",
                "--install",
                str(root / "build-vessel-v1"),
                "--component",
                "vessel",
                "--prefix",
                str(root / loop_levels.ROOT_DIRECTORY),
            ],
        )
        tests = [call for call in recorder.calls if call[0][:1] == ["ctest"]]
        expect(
            "the vessel tests of each level at or below the host run in the build of the level, with the root and the "
            "request of that level",
            [(call[0][2], call[1][x86level.REQUEST_VARIABLE]) for call in tests]
            == [(str(root / f"build-vessel-{word}"), word) for word in ("v1", "v2", "v3")]
            and all(
                call[1][loop_levels.ROOT_VARIABLE] == str(root / loop_levels.ROOT_DIRECTORY / "lib" / "crucible")
                for call in tests
            ),
        )
        expect(
            "a test run fails when no test has the label vessel, and it gets -j with the job count",
            all("--no-tests=error" in call[0] and call[0][-2:] == ["-j", "48"] for call in tests),
        )
        by_hand = Recorder()
        loop_levels.run_levels(root, found, 32, loop_levels.Share.LEASE, by_hand, lambda probe: 4)
        heavy_calls = [call[0] for call in by_hand.calls if "heavy" in call[0]]
        expect(
            "a call by hand runs each build and each test run behind loop heavy with -j, and the build gets no -j of "
            "its own, so it takes the jobserver of the lease",
            len(heavy_calls) == 8
            and all(words[words.index("heavy") + 1 : words.index("--")] == ["-j", "32"] for words in heavy_calls)
            and all(words[-2:] != ["-j", "32"] for words in heavy_calls if "--build" in words),
        )
        served = Recorder()
        loop_levels.run_levels(root, found, 16, loop_levels.Share.JOBSERVER, served, lambda probe: 4)
        builds = [call[0] for call in served.calls if "--build" in call[0]]
        runs = [call[0] for call in served.calls if call[0][:1] == ["ctest"]]
        expect(
            "with the jobserver of the caller, no command goes behind loop heavy, the build gets no -j so ninja obeys "
            "the jobserver, and ctest keeps -j",
            not any("heavy" in call[0] for call in served.calls)
            and len(builds) == 4
            and not any("-j" in words for words in builds)
            and len(runs) == 4
            and all(words[-2:] == ["-j", "16"] for words in runs),
        )
        expect(
            "the root holds the logs of the call, and a new call removes the root of the last call",
            sorted(path.name for path in (root / loop_levels.ROOT_DIRECTORY).iterdir()) == [loop_levels.LOGS]
            and (root / loop_levels.ROOT_DIRECTORY / loop_levels.LOGS / "v4-test.log").is_file(),
        )


def failure_cases(expect: testing.Expect) -> None:
    """Hold a build that fails: its level fails at that step and runs no test, and the next level goes on."""
    with tempfile.TemporaryDirectory(prefix="loop-levels-") as work:
        root = Path(work)
        plant_presets(root)
        found = loop_levels.read_levels(root)
        recorder = Recorder(failing=f"--build {root / 'build-vessel-v2'}")
        outcomes = loop_levels.run_levels(root, found, 8, loop_levels.Share.STEP, recorder, lambda probe: 4)
        states = [(outcome.state, outcome.step) for outcome in outcomes]
        ran = [call[0][2] for call in recorder.calls if call[0][:1] == ["ctest"]]
        expect(
            "the level with a failing build fails at the step build, gets no install and no test, and the other levels "
            "go on",
            states[1] == (loop_levels.State.FAILED, loop_levels.BUILD)
            and [state for state, _ in states].count(loop_levels.State.PASSED) == 3
            and str(root / "build-vessel-v2") not in ran
            and not any("--install" in call[0] and str(root / "build-vessel-v2") in call[0] for call in recorder.calls),
        )
        probes: list[Path] = []

        def host_of(probe: Path) -> int:
            """Record a read of the host, which a call with no installed level must not do."""
            probes.append(probe)
            return 4

        each_fails = Recorder(failing="--build")
        outcomes = loop_levels.run_levels(root, found, 8, loop_levels.Share.STEP, each_fails, host_of)
        expect(
            "when each level fails at its build, each one gives the step build, and the call reads no level of the "
            "host from the root with no probe",
            [(outcome.state, outcome.step) for outcome in outcomes]
            == [(loop_levels.State.FAILED, loop_levels.BUILD)] * 4
            and not probes,
        )


def share_cases(expect: testing.Expect) -> None:
    """Hold the share that each environment of a call gives, and the environment of a real command of a call."""
    fifo = f"{jobserver.AUTH_PREFIX}{jobserver.FIFO_PREFIX}/tmp/planted-jobserver.fifo"
    step = {heavy.STEP_VARIABLE: heavy.STEP_VALUE}
    expect("a call with no variable is a call by hand", loop_levels.share_of({}) is loop_levels.Share.LEASE)
    expect("a step of an entry with no jobserver is a step", loop_levels.share_of(step) is loop_levels.Share.STEP)
    expect(
        "the command of loop heavy, with the variable of a step and a FIFO jobserver, takes the jobserver",
        loop_levels.share_of({**step, jobserver.VARIABLE: f"-j32 {fifo}"}) is loop_levels.Share.JOBSERVER,
    )
    expect(
        "a jobserver of two descriptors is no FIFO, and a step with it is a step",
        loop_levels.share_of({**step, jobserver.VARIABLE: f"{jobserver.AUTH_PREFIX}3,4"}) is loop_levels.Share.STEP,
    )
    with tempfile.TemporaryDirectory(prefix="loop-levels-") as work:
        log = Path(work) / "command.log"
        words = [sys.executable, "-c", f"import os; print(os.environ.get('{heavy.BUILD_PARALLEL_VARIABLE}', 'none'))"]
        with mock.patch.dict(os.environ, {heavy.BUILD_PARALLEL_VARIABLE: "99"}):
            status = loop_levels.run_command(words, {}, log)
        expect(
            "a command of a call gets no job count of cmake --build from the environment, so ninja keeps the jobs of "
            "its share",
            status == 0 and log.read_text(encoding="utf-8").split() == ["none"],
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (read_cases, step_cases, failure_cases, share_cases))
