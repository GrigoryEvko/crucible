"""The test of the test launcher `loop test-step` (assay.loop.teststep).

`guard selftest tests.loop.test_teststep` runs the test.  The cases run the
launcher as ctest starts it, on planted tests in a scratch build directory,
with a planted registry and planted ledgers.  Each case writes one line.  Exit
0 when each case holds, else 1.
"""

import json
import os
import resource
import signal
import string
import sys
import tempfile
from pathlib import Path

from assay.core import meter as cost_meter
from assay.core import proc, testing
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.loop import teststep as test_launcher


class Planted:
    """A scratch build directory with planted tests, a planted registry and planted ledgers."""

    def __init__(self, root: Path) -> None:
        """Make the build directory, its planted tests, and the directory of the ledgers in `root`."""
        self.root = root
        self.build = root / "build"
        (self.build / "test").mkdir(parents=True)
        (self.build / cost_meter.KIND_FILE).write_text("x86_64-debug-asan\n")
        self.ledgers = root / "ledgers"
        self.ledgers.mkdir()
        self.table = root / "registry.txt"
        self.warnings = self.build / "check-warnings"
        self.program = self.plant_test("test_planted", '#!/bin/sh\nexit "${1:-0}"\n')
        self.killer = self.plant_test("test_killed", "#!/bin/sh\nkill -TERM $$\n")
        # The launcher as ctest starts it: the master loop with the subcommand test-step.
        self.launcher = [sys.executable, "-S", cost_meter.LOOP_MASTER, "test-step"]

    def plant_test(self, name: str, text: str) -> Path:
        """Write one planted test program in the directory test of the build directory, and return its path."""
        path = self.build / "test" / name
        path.write_text(text)
        path.chmod(0o755)
        return path

    def budget(self, time_rule: tuple[float, float], memory_rule: tuple[float, float]) -> None:
        """Write the planted registry with the warning and the error thresholds of the two rules."""
        self.table.write_text(
            rule_registry.planted(
                [
                    rule_registry.measured_rule("test-time", *time_rule, "s", "the wall time"),
                    rule_registry.measured_rule("test-memory", *memory_rule, "GB", "the peak memory"),
                ]
            )
        )

    def ledger(self, row: str, text: str) -> None:
        """Write the planted ledger of one rule, or remove it for an empty text."""
        # A ledger with no row has no file (assay.core.ledger).
        path = Path(cost_meter.ledger_file(row, str(self.ledgers)))
        if text:
            path.write_text(f"# a planted ledger\nformat 1\nrule {row}\n{text}")
        else:
            path.unlink(missing_ok=True)

    def _environment_of(self, extra: dict[str, str]) -> dict[str, str]:
        """Return the environment of one run of the launcher, with the planted registry and ledgers."""
        # A case for a CI runner or for kept cores sets the variable itself.  Each verdict is then the same in each
        # environment.
        environment = proc.environment(
            {test_launcher.REGISTRY_ENV: str(self.table), test_launcher.LEDGERS_ENV: str(self.ledgers)}
        )
        environment.pop(cost_meter.GITHUB_ACTIONS_ENV, None)
        environment.pop(test_launcher.CORES_ENV, None)
        environment.update(extra)
        return environment

    def launch(self, target: Path, *args: str, **extra: str) -> tuple[int, str]:
        """Run the launcher on one planted executable test, and return its exit code and its standard error."""
        done = proc.run(
            proc.Command(
                [*self.launcher, "--warnings-dir", str(self.warnings), str(target), *args],
                timeout_s=proc.TOOL_S,
                env=self._environment_of(extra),
            )
        )
        return done.exit, done.err

    def launch_for_wait_status(self, target: Path, **extra: str) -> int:
        """Run the launcher on one planted test, and return the wait status, which holds the core dump bit."""
        done = proc.run(
            proc.Command(
                [*self.launcher, "--warnings-dir", str(self.warnings), str(target)],
                timeout_s=proc.TOOL_S,
                env=self._environment_of(extra),
                stdout=self.root / "wait-status.log",
                stderr=proc.Output.MERGE,
            )
        )
        return done.wait_status

    def launch_script(self, name: str, *command: str, **extra: str) -> tuple[int, str]:
        """Run the launcher on a script test as crucible_script_test() registers it: a program of PATH, with --name."""
        done = proc.run(
            proc.Command(
                [
                    *self.launcher,
                    "--warnings-dir",
                    str(self.warnings),
                    "--build-dir",
                    str(self.build),
                    "--name",
                    name,
                    *command,
                ],
                timeout_s=proc.TOOL_S,
                env=self._environment_of(extra),
            )
        )
        return done.exit, done.err

    def _record_of(self, name: str) -> dict[str, object]:
        """Return the record of one test of the build directory."""
        found: object = json.loads(Path(cost_meter.test_record_file(str(self.build), name)).read_text())
        return found if isinstance(found, dict) else {}

    def status_cases(self, expect: testing.Expect) -> None:
        """Check that the status and the signal of a test go through, and the record of the test."""
        self.ledger("test-time", "")
        self.ledger("test-memory", "")
        self.budget((1000, 2000), (1000, 2000))
        for code in ("0", "1", "3"):
            status, said = self.launch(self.program, code)
            expect(
                f"the status {code} of the test goes through, with no finding",
                status == int(code) and "[test-" not in said,
            )
        record = self._record_of("test_planted")
        expect(
            "the record names the test, its step, its result, its program and its cost, in the directory "
            "test-records of the build directory that holds the program",
            record["step"] == "test"
            and record["test"] == "test_planted"
            and record["result"] == "failed"
            and record["exit"] == 3
            and record["command"] == str(self.program)
            and isinstance(record["cost"], dict)
            and record["cost"]["wall_s"] >= 0,
        )
        status, _ = self.launch(self.killer)
        expect("a test that a signal stops ends on the same signal", status == -15)

    def script_cases(self, expect: testing.Expect) -> None:
        """Check a script test, the usage errors, and the characters of a test name."""
        self.budget((1000, 2000), (1000, 2000))
        launched = test_launcher.LAUNCHED_ENV
        status, said = self.launch_script("planted_script", "sh", "-c", f'test "${launched}" = planted_script')
        record = self._record_of("planted_script")
        expect(
            "a script test runs a program of PATH, gets the name of the test in the environment, and has a record "
            "with its name and its program",
            status == 0
            and record["test"] == "planted_script"
            and record["command"] == "sh"
            and record["result"] == "passed",
        )
        usage_errors = (
            ("an option with no value", ["--name"]),
            ("an option two times", ["--name", "a", "--name", "b", "sh"]),
            ("no program", ["--build-dir", str(self.build)]),
            ("the option of a self-test, which the launcher does not have", ["--self-test"]),
        )
        for label, words in usage_errors:
            done = proc.run(proc.Command([*self.launcher, *words], timeout_s=proc.TOOL_S, env=self._environment_of({})))
            expect(f"a usage error gives status 2: {label}", done.exit == 2 and "usage:" in done.err)
        status, said = self.launch_script("planted/script", "sh", "-c", "exit 0")
        expect("a test name that is not a writer key gives status 2", status == 2 and "is not a word" in said)
        writer_keys = ("a", "Z_9", "neg_x-y", "_a")
        refused_keys = ("", "-a", "a.b", "a/b", "a b")
        expect(
            "is_test_name agrees with WRITER_KEY of utils/assay/core/report.py",
            all(
                test_launcher.is_test_name(key) == bool(check_report.WRITER_KEY.fullmatch(key))
                for key in (*writer_keys, *refused_keys)
            )
            and frozenset(string.ascii_letters + string.digits + "_-") == test_launcher.NAME_CHARACTERS,
        )

    def threshold_cases(self, expect: testing.Expect) -> None:
        """Check the warning and the error thresholds of the two rules, on a CI runner and in a tsan build too."""
        program, warnings = self.program, self.warnings
        self.budget((0, 2000), (0, 2000))
        status, said = self.launch(program)
        own = warnings / "test-time.test_planted.txt"
        expect(
            "a test over the warning thresholds passes, with a warning for each row",
            status == 0
            and said.count(": warning: [test-") == 2
            and own.is_file()
            and (warnings / "test-memory.test_planted.txt").is_file(),
        )
        self.budget((1000, 2000), (1000, 2000))
        status, said = self.launch(program)
        expect(
            "a test under the thresholds removes its warnings files",
            status == 0 and not own.exists() and not (warnings / "test-memory.test_planted.txt").exists(),
        )

        self.budget((0, 0), (1000, 2000))
        status, said = self.launch(program)
        expect(
            "a passed test over the time error threshold fails, with an error line that names its program",
            status == 1 and f"{program}:0: error: [test-time] " in said and "no row of" in said,
        )
        record = self._record_of("test_planted")
        expect("the record of the failed test says rejected", record["result"] == "rejected")
        status, said = self.launch_script("planted_script", "sh", "-c", "exit 0")
        expect(
            "a passed script test over the time error threshold fails, with an error line that names its record",
            status == 1
            and f"{cost_meter.test_record_file(str(self.build), 'planted_script')}:0: error: [test-time] " in said
            and self._record_of("planted_script")["result"] == "rejected",
        )
        status, said = self.launch(program, "3")
        expect(
            "a test that failed keeps its status over the error threshold",
            status == 3 and ": error: [test-time] " in said,
        )
        status, said = self.launch(program, GITHUB_ACTIONS="true")
        record = self._record_of("test_planted")
        expect(
            "on a CI runner, a passed test over the time error threshold passes, with a warning that says that "
            "the error was demoted",
            status == 0
            and ": warning: [test-time] " in said
            and "demoted" in said
            and ": error: " not in said
            and record["result"] == "passed",
        )
        self.budget((1000, 2000), (0, 0))
        status, said = self.launch(program, GITHUB_ACTIONS="true")
        expect(
            "on a CI runner, a passed test over the memory error threshold still fails",
            status == 1 and ": error: [test-memory] " in said,
        )
        self.budget((0, 0), (1000, 2000))
        (self.build / cost_meter.KIND_FILE).write_text("x86_64-debug-tsan\n")
        status, said = self.launch(program)
        expect(
            "a build of a tsan kind warns on a time over the error threshold, and the test passes",
            status == 0 and ": warning: [test-time] " in said and "budgets no wall time" in said,
        )
        (self.build / cost_meter.KIND_FILE).write_text("x86_64-debug-asan\n")
        self.budget((1000, 2000), (0, 0))
        status, said = self.launch(program)
        expect("a passed test over the memory error threshold fails", status == 1 and ": error: [test-memory] " in said)

    def ledger_cases(self, expect: testing.Expect) -> None:
        """Check the rows of a ledger, and a test that cannot start."""
        program = self.program
        self.budget((1000, 2000), (0, 0))
        self.ledger("test-memory", "- | x86_64-debug-asan | 0.01 | test_planted | a planted reason\n")
        status, said = self.launch(program)
        expect(
            "a ledger row of the kind admits the test, with a warning",
            status == 0 and ": warning: [test-memory] " in said and "a planted reason" in said,
        )
        self.ledger("test-memory", "- | x86_64-release | 0.01 | test_planted | a planted reason\n")
        status, said = self.launch(program)
        expect("a ledger row of another kind admits nothing", status == 1)
        self.ledger("test-memory", "@build/test/test_planted | x86_64-debug-asan | 0.01 | - | a planted reason\n")
        status, said = self.launch(program)
        expect("a ledger row that names a file and not a test admits nothing", status == 1)
        self.ledger("test-memory", "- | x86_64-debug-asan | 0.01 | test_planted | a planted reason\n")
        self.budget((1000, 2000), (1000, 2000))
        status, said = self.launch(program)
        expect(
            "a ledger row whose test stays under the error threshold fails the test",
            status == 1 and "Remove its row" in said,
        )
        status, said = self.launch(self.root / "no-such-test")
        expect("a test that cannot start gives status 127", status == 127 and "cannot start" in said)

    def core_cases(self, expect: testing.Expect) -> None:
        """Check the core limit of a test, of a child of the test and of the launcher (CORE DUMPS of the launcher)."""
        # This process sets its own soft core limit to 0.  The limit that a test sees then tells the launcher apart
        # from the environment.  A core dump that the launcher does not stop goes to the handler of
        # kernel.core_pattern, which ignores a limit of 0.  At a hard limit of 0, no process can set 1 byte, and the
        # cases then check only the limit that the environment gives.
        self.budget((1000, 2000), (1000, 2000))
        cores_env, cores_keep = test_launcher.CORES_ENV, test_launcher.CORES_KEEP
        _, hard_core = resource.getrlimit(resource.RLIMIT_CORE)
        resource.setrlimit(resource.RLIMIT_CORE, (0, hard_core))
        can_stop = hard_core == resource.RLIM_INFINITY or hard_core >= test_launcher.NO_CORE_LIMIT_BYTES
        stopped = test_launcher.NO_CORE_LIMIT_BYTES if can_stop else 0
        shebang = f"#!{sys.executable} -S\n"
        limit_probe = self.plant_test(
            "test_core_limit",
            shebang + "import resource, sys\nsoft, hard = resource.getrlimit(resource.RLIMIT_CORE)\n"
            "print(f'core-limit {soft} {hard}', file=sys.stderr)\n",
        )
        child_probe = self.plant_test(
            "test_core_child",
            shebang + "import os, sys\nchild = os.fork()\nif child == 0:\n    os.abort()\n"
            "_, status = os.waitpid(child, 0)\n"
            "signal = os.WTERMSIG(status) if os.WIFSIGNALED(status) else 0\n"
            "print(f'child-core {int(os.WCOREDUMP(status))} signal {signal}', file=sys.stderr)\n",
        )
        # This planted test stops its own core dump.  Only the launcher can then write one.
        aborter = self.plant_test(
            "test_core_abort",
            shebang + "import os, resource\n_, hard = resource.getrlimit(resource.RLIMIT_CORE)\n"
            "if hard == resource.RLIM_INFINITY or hard >= 1:\n"
            "    resource.setrlimit(resource.RLIMIT_CORE, (1, hard))\nos.abort()\n",
        )
        status, said = self.launch(limit_probe)
        expect(
            "a test gets a soft core limit of 1 byte, and the hard core limit of the environment",
            status == 0 and f"core-limit {stopped} {hard_core}\n" in said and "loop test-step:" not in said,
        )
        status, said = self.launch(limit_probe, **{cores_env: cores_keep})
        expect(
            f"with {cores_env}={cores_keep}, a test keeps the core limit of the environment",
            status == 0 and f"core-limit 0 {hard_core}\n" in said and "loop test-step:" not in said,
        )
        status, said = self.launch(limit_probe, **{cores_env: "yes"})
        expect(
            f"another value of {cores_env} stops the core dumps, with a note",
            status == 0 and f"core-limit {stopped} {hard_core}\n" in said and f"only {cores_keep!r} keeps" in said,
        )
        status, said = self.launch(child_probe)
        expect(
            "a child of a test that aborts writes no core dump",
            status == 0 and f" signal {signal.SIGABRT.value}\n" in said and (not can_stop or "child-core 0 " in said),
        )
        for keep_label, extra in (("", {}), (f", also with {cores_env}={cores_keep}", {cores_env: cores_keep})):
            wait_status = self.launch_for_wait_status(aborter, **extra)
            expect(
                f"a test that aborts ends the launcher on SIGABRT, and the launcher writes no core dump{keep_label}",
                os.WIFSIGNALED(wait_status)
                and os.WTERMSIG(wait_status) == signal.SIGABRT
                and (not can_stop or not os.WCOREDUMP(wait_status)),
            )


def launched_cases(expect: testing.Expect) -> None:
    """Check the command that a test runs behind the launcher, which a reader of the tests of a build takes."""
    master = "/export/utils/bin/loop"
    launcher = [sys.executable, "-S", master, test_launcher.SUBCOMMAND]
    executable = ["/build/test/test_planted", "1"]
    expect(
        "an executable test runs its program behind the launcher",
        test_launcher.launched([*launcher, "--warnings-dir", "/w", "--build-dir", "/build", *executable]) == executable,
    )
    expect(
        "a script test with a name runs its program behind the launcher",
        test_launcher.launched([*launcher, "--name", "planted", "python3", "check.py"]) == ["python3", "check.py"],
    )
    expect("a command with no launcher is its own command", test_launcher.launched(executable) == executable)
    expect(
        "a launcher with an option and no program gives its own command",
        test_launcher.launched([*launcher, "--name", "planted"]) == [*launcher, "--name", "planted"],
    )
    fixture = [sys.executable, "-S", master, "fixture", "/build", "neg.cpp", "neg", "regex"]
    expect("the driver of a negative fixture is not the launcher", test_launcher.launched(fixture) == fixture)


def main(argv: list[str]) -> int:
    """Run each case in a scratch directory, and write one line for each."""
    with tempfile.TemporaryDirectory(prefix="test-step-") as work:
        planted = Planted(Path(work))
        return testing.run(
            __name__.removeprefix("assay."),
            argv,
            (
                planted.status_cases,
                planted.script_cases,
                planted.threshold_cases,
                planted.ledger_cases,
                planted.core_cases,
                launched_cases,
            ),
        )
