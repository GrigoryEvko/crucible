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
from assay.core import record as cost_record
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

    def budget(
        self,
        time_rule: tuple[float, float],
        memory_rule: tuple[float, float],
        instruction_rule: tuple[float, float] | None = None,
    ) -> None:
        """Write the planted registry with the warning and the error thresholds of the rules.

        With no `instruction_rule`, the registry has no rule test-instructions, and a count holds no error level.
        """
        rules = [
            rule_registry.measured_rule("test-time", *time_rule, "s", "the wall time"),
            rule_registry.measured_rule("test-memory", *memory_rule, "GB", "the peak memory"),
        ]
        if instruction_rule is not None:
            rules.append(rule_registry.measured_rule("test-instructions", *instruction_rule, "G", "the instructions"))
        self.table.write_text(rule_registry.planted(rules))

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
        """Return the record of one test of the build directory, or no field when the test has no record."""
        try:
            found: object = json.loads(Path(cost_record.test_record_file(self.build, name)).read_text())
        except FileNotFoundError:
            return {}
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
        self.budget((0, 2000), (1000, 2000))
        dotted = self.plant_test("test_planted.sh", "#!/bin/sh\nexit 0\n")
        status, said = self.launch(dotted)
        record = self._record_of("test_planted.sh")
        expect(
            "a program whose file name holds a dot runs as a test of that name, with its record and its warnings file",
            status == 0
            and record.get("test") == "test_planted.sh"
            and record.get("result") == "passed"
            and (self.warnings / "test-time.test_planted.sh.txt").is_file(),
        )
        writer_keys = ("a", "Z_9", "neg_x-y", "_a", "a.b", "run.sh")
        refused_keys = ("", "-a", ".a", "a/b", "a b")
        expect(
            "is_test_name agrees with WRITER_KEY of utils/assay/core/report.py",
            all(
                test_launcher.is_test_name(key) == bool(check_report.WRITER_KEY.fullmatch(key))
                for key in (*writer_keys, *refused_keys)
            )
            and frozenset(string.ascii_letters + string.digits + "_.-") == test_launcher.NAME_CHARACTERS,
        )

    def _walls_of(self, name: str) -> list[object]:
        """Return the window of the record of one test, or no wall time when the record has no window."""
        walls = self._record_of(name).get(cost_record.WALLS_FIELD)
        return walls if isinstance(walls, list) else []

    def version_cases(self, expect: testing.Expect) -> None:
        """Check that a change of a test drops its window.

        A test that becomes slower then gives a warning on its first run after the change.
        """
        self.ledger("test-time", "")
        self.ledger("test-memory", "")
        self.budget((0.5, 2000), (1000, 2000))
        warned = ": warning: [test-time] "
        changed = self.plant_test("test_changed", "#!/bin/sh\nexit 0\n")
        said = [self.launch(changed)[1] for _ in range(test_launcher.RECENT_RUNS - 1)]
        expect(
            "the runs of one version of a test go into one window",
            not any(warned in text for text in said)
            and len(self._walls_of("test_changed")) == test_launcher.RECENT_RUNS - 1,
        )
        self.plant_test("test_changed", "#!/bin/sh\nsleep 0.6\n")
        status, text = self.launch(changed)
        expect(
            "a change of the program of a test drops its window, so the test that becomes slower warns on its first "
            "run after the change",
            status == 0 and warned in text and len(self._walls_of("test_changed")) == 1,
        )
        script = self.root / "planted_changed.sh"
        script.write_text("exit 0\n")
        for _ in range(2):
            self.launch_script("planted_changed_script", "sh", str(script))
        script.write_text("sleep 0.6\n")
        status, text = self.launch_script("planted_changed_script", "sh", str(script))
        expect(
            "a change of a file that a word of a script test names drops its window",
            status == 0 and warned in text and len(self._walls_of("planted_changed_script")) == 1,
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
            and f"{cost_record.test_record_file(self.build, 'planted_script')}:0: error: [test-time] " in said
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

    def _count_of(self, name: str) -> object:
        """Return the instruction count of the record of one test, or None when the record holds no count."""
        cost = self._record_of(name).get("cost")
        return cost.get(cost_record.TEST_COUNT_KEY) if isinstance(cost, dict) else None

    def count_cases(self, expect: testing.Expect) -> None:
        """Check test-instructions, and the warning of test-time for a run with an accurate count (THE MEASURED RULES).

        A planted test that sleeps over the time error threshold gives a warning, because its count holds the error
        level.  A planted test that does more work than its row keeps fails.  A host with no accurate counter gives no
        count, and test-time then keeps its error.
        """
        self.ledger("test-time", "")
        self.ledger("test-memory", "")
        self.ledger("test-instructions", "")
        host_counts = cost_meter.host_gives_counter()
        on_host = f"(this host: {'a counter' if host_counts else 'no counter'})"
        sleeper = self.plant_test("test_sleeping", "#!/bin/sh\nsleep 0.3\n")
        self.budget((0.1, 0.2), (1000, 2000), (1000, 2000))
        status, said = self.launch(sleeper)
        count = self._count_of("test_sleeping")
        if host_counts:
            expect(
                f"a test that sleeps over the time error threshold with a small count passes, with a warning of "
                f"test-time that names test-instructions, and its record keeps the count {on_host}",
                status == 0
                and ": warning: [test-time] " in said
                and "test-instructions holds the error level" in said
                and ": error: " not in said
                and isinstance(count, int)
                and count > 0,
            )
        else:
            expect(
                f"with no count, a test that sleeps over the time error threshold fails {on_host}",
                status == 1 and ": error: [test-time] " in said and count is None,
            )
        status, said = self.launch(sleeper, **{cost_meter.INSTRUCTIONS_ENV: "0"})
        expect(
            f"a run with {cost_meter.INSTRUCTIONS_ENV}=0 has no count, and test-time keeps its error",
            status == 1 and ": error: [test-time] " in said and self._count_of("test_sleeping") is None,
        )
        self.budget((0.1, 0.2), (1000, 2000))
        status, said = self.launch(sleeper)
        expect(
            "a registry with no rule test-instructions gives a count no error level, and test-time keeps its error",
            status == 1 and ": error: [test-time] " in said,
        )
        if not host_counts:
            expect(f"skip the cases of test-instructions {on_host}", True)
            return
        worker = self.plant_test(
            "test_working", f"#!{sys.executable} -S\ntotal = 0\nfor index in range(2_000_000):\n    total += index\n"
        )
        self.budget((1000, 2000), (1000, 2000), (0.0001, 0.0002))
        status, said = self.launch(worker)
        measured = self._count_of("test_working")
        expect(
            "a passed test over the error threshold of test-instructions fails, with an error line that gives its count",
            status == 1 and ": error: [test-instructions] " in said and " G user instructions" in said,
        )
        if not isinstance(measured, int):
            expect("the record of a counted test keeps its count", False)
            return
        giga = measured / test_launcher.GIGA
        self.ledger("test-instructions", f"- | x86_64-debug-asan | {giga * 2:.6f} | test_working | a planted reason\n")
        status, said = self.launch(worker)
        expect(
            "a ledger row keeps a test up to its kept value, with a warning",
            status == 0 and ": warning: [test-instructions] " in said and "a planted reason" in said,
        )
        self.ledger("test-instructions", f"- | x86_64-debug-asan | {giga / 2:.6f} | test_working | a planted reason\n")
        status, said = self.launch(worker)
        expect(
            "a test that does more work than its row keeps fails again",
            status == 1 and ": error: [test-instructions] " in said and "more than the row" in said,
        )
        self.ledger("test-instructions", "")

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
        self.ledger("test-memory", "- | x86_64-debug-asan | 0.000001 | test_planted | a planted reason\n")
        status, said = self.launch(program)
        expect(
            "a test over the kept value of its row with the noise of the unit fails again",
            status == 1 and ": error: [test-memory] " in said and "more than the row" in said,
        )
        self.budget((1000, 2000), (0, 1000))
        self.ledger("test-memory", "- | x86_64-debug-asan | 0.01 | test_planted | a planted reason\n")
        status, said = self.launch(program)
        expect(
            "a ledger row keeps a test over the warning threshold only, with a warning that names its kept value",
            status == 0 and ": warning: [test-memory] " in said and "keeps it up to 0.01 GB" in said,
        )
        self.budget((1000, 2000), (0, 0))
        (self.build / cost_meter.CONFIGURATION_FILE).write_text("x86_64-debug-asan-bench\n")
        self.ledger("test-memory", "- | x86_64-debug-asan-bench | 0.01 | test_planted | a row of the bench build\n")
        status, said = self.launch(program)
        expect(
            "a ledger row of the name of the configuration keeps the test",
            status == 0 and "a row of the bench build" in said,
        )
        self.ledger("test-memory", "- | x86_64-debug-asan | 0.01 | test_planted | a row of the build kind\n")
        status, said = self.launch(program)
        expect(
            "a ledger row of the build kind keeps the test of a build with options",
            status == 0 and "a row of the build kind" in said,
        )
        (self.build / cost_meter.CONFIGURATION_FILE).unlink()
        self.budget((1000, 2000), (1000, 2000))
        status, said = self.launch(program)
        expect(
            "a ledger row whose test stays at or under the warning threshold fails the test",
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


def unmeasured_cases(expect: testing.Expect) -> None:
    """Check that a run below the warning threshold refuses its row only when the run does the work that the row keeps."""
    budget = {"test-memory": (0.5, 1.5)}
    run = cost_meter.Measurement(0, 0.1, 0.0, 0.2, 50 * 1024, 1.0, 0.0, None)
    kind = "x86_64-debug-asan"
    with tempfile.TemporaryDirectory(prefix="test-step-rows-") as directory:
        Path(cost_meter.ledger_file("test-memory", directory)).write_text(
            f"format 1\nrule test-memory\n- | {kind} | 0.6 | test_planted | a planted row\n", encoding="utf-8"
        )
        measured = test_launcher.judge("test_planted", run, budget, kind, directory)
        stored = test_launcher.judge("test_planted", run, budget, kind, directory, is_measured=False)
    expect(
        "a run under the warning threshold refuses the row that keeps the test",
        [(level, row) for level, row, _ in measured] == [("error", "test-memory")],
    )
    expect("a run that does not do the work of the row, such as a stored fixture, keeps the row", stored == [])


def window_cases(expect: testing.Expect) -> None:
    """Check that the warning of test-time reads the smallest wall time of the window, and its error each run."""
    budget = {"test-time": (5.0, 30.0)}
    counted_budget = {"test-time": (5.0, 30.0), "test-instructions": (1000.0, 2000.0)}
    with tempfile.TemporaryDirectory(prefix="test-step-window-") as directory:
        build = Path(directory) / "build"
        build.mkdir()
        (build / cost_meter.KIND_FILE).write_text("x86_64-debug-asan\n")
        warnings = build / "check-warnings"

        def run(
            name: str,
            wall: float,
            exit_code: int = 0,
            is_measured: bool = True,
            version: str = "0123456789abcdef",
            instructions: int | None = None,
        ) -> tuple[int, bool]:
            """Judge and record one planted run of a test.  Return its status, and True when it gives a warning."""
            measured = cost_meter.Measurement(exit_code, 0.1, 0.0, wall, 1024, 1.0, 0.0, None, instructions)
            status = test_launcher.judge_and_record(
                name,
                "/b/test",
                "/b/test",
                str(build),
                measured,
                str(warnings),
                budget if instructions is None else counted_budget,
                is_measured,
                version,
            )
            return status, (warnings / f"test-time.{name}.txt").is_file()

        def window(name: str) -> tuple[float, ...]:
            """Return the window of the record of a test."""
            return cost_record.read_test(Path(cost_record.test_record_file(build, name))).walls

        run("test_loaded", 4.2)
        run("test_loaded", 4.4)
        status, warns = run("test_loaded", 6.0)
        expect(
            "a run over the warning threshold of test-time gives no warning when a faster run of its window is under "
            "the threshold, because the load of the host only makes a run slower",
            status == 0 and not warns and window("test_loaded") == (6.0, 4.4, 4.2),
        )
        verdicts = [run("test_blocked", 6.0) for _ in range(test_launcher.RECENT_RUNS + 1)]
        expect(
            "a test that waits for 6 s on each run warns on each run",
            verdicts == [(0, True)] * (test_launcher.RECENT_RUNS + 1)
            and window("test_blocked") == (6.0,) * test_launcher.RECENT_RUNS,
        )
        run("test_slower", 4.0)
        verdicts = [run("test_slower", 6.0) for _ in range(test_launcher.RECENT_RUNS)]
        expect(
            "a test that becomes slower warns when each run of its window is slower",
            verdicts == [(0, False)] * (test_launcher.RECENT_RUNS - 1) + [(0, True)],
        )
        run("test_stalled", 4.0)
        status, _ = run("test_stalled", 31.0)
        expect("one run over the error threshold of test-time fails at once, also after a faster run", status == 1)
        run("test_reclaimed", 4.0, instructions=10**9)
        status, warns = run("test_reclaimed", 91.0, instructions=10**9)
        expect(
            "a run with an instruction count over the error threshold of test-time, after a faster run, passes with no "
            "finding, because test-instructions holds the error level and the window reads the faster run",
            status == 0 and not warns,
        )
        status, warns = run("test_reclaimed_alone", 91.0, instructions=10**9)
        expect(
            "the first run with an instruction count over the error threshold of test-time passes with a warning",
            status == 0 and warns,
        )
        run("test_failing", 6.0)
        status, warns = run("test_failing", 1.0, exit_code=1)
        expect(
            "a run that failed is judged alone, and it stays out of the window",
            status == 1 and not warns and window("test_failing") == (6.0,),
        )
        status, warns = run("test_failing", 6.0)
        expect("the next passed run reads the window with no failed run", status == 0 and warns)
        run("neg_stored", 6.0)
        status, warns = run("neg_stored", 0.1, is_measured=False, version="fedcba9876543210")
        expect(
            "a run that does not do the measured work, such as a stored fixture, stays out of the window, and the "
            "record keeps the version of the window",
            status == 0
            and not warns
            and window("neg_stored") == (6.0,)
            and cost_record.read_test(Path(cost_record.test_record_file(build, "neg_stored"))).version
            == "0123456789abcdef",
        )
        run("test_versions", 4.0, version="")
        status, warns = run("test_versions", 6.0, version="")
        expect(
            "a run with no version is judged alone, and its window holds only that run",
            status == 0 and warns and window("test_versions") == (6.0,),
        )
        run("test_versions", 4.0, version="aaaaaaaaaaaaaaaa")
        status, warns = run("test_versions", 6.0, version="bbbbbbbbbbbbbbbb")
        expect(
            "a run of a different version drops the window, and it warns on its first run",
            status == 0 and warns and window("test_versions") == (6.0,),
        )


def version_unit_cases(expect: testing.Expect) -> None:
    """Check the files of the version of a command, and the change of the version with a change of one file."""
    with tempfile.TemporaryDirectory(prefix="test-step-version-") as directory:
        utils = Path(directory) / "utils"
        (utils / "bin").mkdir(parents=True)
        (utils / test_launcher.PACKAGE / "core").mkdir(parents=True)
        (utils / test_launcher.PACKAGE / "__pycache__").mkdir()
        master = utils / "bin" / "guard"
        master.write_text("# a planted master\n")
        module = utils / test_launcher.PACKAGE / "core" / "planted.py"
        module.write_text("SIZE = 1\n")
        cached = utils / test_launcher.PACKAGE / "__pycache__" / "planted.cpython-314.pyc"
        cached.write_bytes(b"planted")
        script = Path(directory) / "planted.cmake"
        script.write_text("# a planted script\n")
        command = [sys.executable, "-S", str(master), "selftest", f"-DSCRIPT={script}"]

        def version() -> str:
            """Return the version of the planted command."""
            return test_launcher.version_of(command, test_launcher.command_files(command))

        first = version()
        expect(
            "the version is 16 lowercase hexadecimal digits, and the same files give the same version",
            len(first) == 16 and set(first) <= set("0123456789abcdef") and version() == first,
        )
        expect(
            "the files of a master command are each module of the package, and not its byte code",
            str(module) in test_launcher.command_files(command)
            and str(cached) not in test_launcher.command_files(command),
        )
        cached.write_bytes(b"planted again")
        expect("a change of the byte code keeps the version", version() == first)
        module.write_text("SIZE = 22\n")
        second = version()
        expect("a change of a module of the package changes the version of a master command", second != first)
        script.write_text("# a planted script, changed\n")
        expect("a change of the VALUE of a word NAME=VALUE changes the version", version() != second)
        expect(
            "the words of the command go into the version",
            test_launcher.version_of(["a", "b"], []) != test_launcher.version_of(["a", "c"], []),
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
                planted.count_cases,
                planted.ledger_cases,
                planted.core_cases,
                planted.version_cases,
                unmeasured_cases,
                window_cases,
                version_unit_cases,
                launched_cases,
            ),
        )
