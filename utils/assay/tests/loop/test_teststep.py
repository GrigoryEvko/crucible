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
from collections.abc import Callable
from pathlib import Path

from assay.core import cache_root, proc, testing
from assay.core import meter as cost_meter
from assay.core import record as cost_record
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.loop import readset
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
        self.hard_killer = self.plant_test("test_hard_killed", "#!/bin/sh\nkill -KILL $$\n")
        # A planted emulator that is not in the build directory, as qemu-aarch64-static is.  It drops its two options
        # and runs the program (THE EMULATOR of the launcher).
        self.emulator = root / "emulator"
        self.emulator.write_text('#!/bin/sh\nshift 2\nexec "$@"\n')
        self.emulator.chmod(0o755)
        # The launcher as ctest starts it: the master loop with the subcommand test-step.
        self.launcher = [sys.executable, "-S", cost_meter.LOOP_MASTER, "test-step"]

    def plant_test(self, name: str, text: str) -> Path:
        """Write one planted test program in the directory test of the build directory, and return its path."""
        path = self.build / "test" / name
        path.write_text(text)
        path.chmod(0o755)
        return path

    def budget(self, memory_limit: float, instruction_limit: float | None = None) -> None:
        """Write the planted registry with the limit of each rule.

        With no `instruction_limit`, the registry has no rule test-instructions, and no rule judges a count.
        """
        rules = [rule_registry.measured_rule("test-memory", memory_limit, "GB", "the peak memory")]
        if instruction_limit is not None:
            rules.append(rule_registry.measured_rule("test-instructions", instruction_limit, "G", "the instructions"))
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
        self.ledger("test-memory", "")
        self.budget(2000)
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
        status, said = self.launch(self.hard_killer)
        hard_record = self._record_of("test_hard_killed")
        expect(
            "a test that SIGKILL stops ends the launcher on SIGKILL with no traceback, and the record gives the signal",
            status == -signal.SIGKILL
            and "Traceback" not in said
            and hard_record.get("exit") == -signal.SIGKILL
            and hard_record.get("result") == "failed",
        )

    def script_cases(self, expect: testing.Expect) -> None:
        """Check a script test, the usage errors, and the characters of a test name."""
        self.budget(2000)
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
        traced = test_launcher.TRACED_ENV
        status, _ = self.launch_script("planted_traced", "sh", "-c", f'test "${traced}" = 1')
        expect("a script test that the launcher plans to trace gets the mark of a traced run", status == 0)
        status, _ = self.launch_script("planted_untraced", "--trace", "never", "sh", "-c", f'test -z "${traced}"')
        expect(
            "a script test with --trace never gets no mark of a traced run, and the launcher writes no read set of it",
            status == 0 and not Path(readset.read_set_file(self.build, "planted_untraced")).exists(),
        )
        usage_errors = (
            ("an option with no value", ["--name"]),
            ("an option two times", ["--name", "a", "--name", "b", "sh"]),
            ("no program", ["--build-dir", str(self.build)]),
            ("the option of a self-test, which the launcher does not have", ["--self-test"]),
            ("a value of --trace that is not never", ["--trace", "sometimes", "sh"]),
        )
        for label, words in usage_errors:
            done = proc.run(proc.Command([*self.launcher, *words], timeout_s=proc.TOOL_S, env=self._environment_of({})))
            expect(f"a usage error gives status 2: {label}", done.exit == 2 and "usage:" in done.err)
        status, said = self.launch_script("planted/script", "sh", "-c", "exit 0")
        expect("a test name that is not a writer key gives status 2", status == 2 and "is not a word" in said)
        dotted = self.plant_test("test_planted.sh", "#!/bin/sh\nexit 0\n")
        status, said = self.launch(dotted)
        record = self._record_of("test_planted.sh")
        expect(
            "a program whose file name holds a dot runs as a test of that name, with its record",
            status == 0 and record.get("test") == "test_planted.sh" and record.get("result") == "passed",
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

    def launch_emulated(self, words: str, target: Path, *args: str) -> tuple[int, str]:
        """Run the launcher on one planted test behind the planted emulator, as CMake starts a test of a cross build.

        CMake puts the emulator and its two options after the launcher and before the program (THE EMULATOR).
        """
        done = proc.run(
            proc.Command(
                [
                    *self.launcher,
                    "--warnings-dir",
                    str(self.warnings),
                    "--build-dir",
                    str(self.build),
                    "--emulator-words",
                    words,
                    str(self.emulator),
                    "-L",
                    "/planted/sysroot",
                    str(target),
                    *args,
                ],
                timeout_s=proc.TOOL_S,
                env=self._environment_of({}),
            )
        )
        return done.exit, done.err

    def emulator_cases(self, expect: testing.Expect) -> None:
        """Check that two tests behind one emulator get two records, each named after its program (THE EMULATOR)."""
        self.budget(2000)
        first = self.plant_test("test_emulated_first", '#!/bin/sh\nexit "${1:-0}"\n')
        second = self.plant_test("test_emulated_second", '#!/bin/sh\nexit "${1:-0}"\n')
        first_status, _ = self.launch_emulated("3", first)
        second_status, _ = self.launch_emulated("3", second, "3")
        first_record, second_record = self._record_of("test_emulated_first"), self._record_of("test_emulated_second")
        expect(
            "two tests behind one emulator run through it, and each gets a record of its own, with the name and the "
            "path of its program",
            (first_status, second_status) == (0, 3)
            and (first_record.get("result"), first_record.get("command")) == ("passed", str(first))
            and (second_record.get("exit"), second_record.get("command")) == (3, str(second))
            and self._record_of(self.emulator.name) == {},
        )
        for words, label in (("4", "a count that leaves no program"), ("x", "a word that is not a count")):
            status, said = self.launch_emulated(words, first)
            expect(
                f"--emulator-words with {label} is a usage error",
                status == 2 and f"--emulator-words {words!r} is not a count" in said,
            )

    def environment_cases(self, expect: testing.Expect) -> None:
        """Check that a test runs in the clean environment of the launcher (THE ENVIRONMENT)."""
        self.budget(2000)
        search = test_launcher.search_path(os.environ.get("PATH", ""))
        check = (
            'test -z "${LD_LIBRARY_PATH+set}" && test -z "${PLANTED_HOST+set}" && test "$CRUCIBLE_PLANTED" = kept '
            f'&& test "$LANG" = C.UTF-8 && test "$TZ" = UTC && test "$PATH" = "{search}"'
        )
        status, _ = self.launch_script(
            "planted_environment",
            "sh",
            "-c",
            check,
            LD_LIBRARY_PATH="/opt/lib:",
            PLANTED_HOST="1",
            CRUCIBLE_PLANTED="kept",
            LANG="ru_RU.UTF-8",
            TZ="Europe/Moscow",
            PATH=f"{search}::relative/bin",
        )
        expect(
            "the test gets the kept variables, the locale and the time zone of the launcher, the clean PATH, and no "
            "other variable",
            status == 0,
        )
        makeflags = "-j4 --jobserver-auth=fifo:/tmp/planted.fifo"
        given = cost_meter.TEST_JOBSERVER_ENV
        status, _ = self.launch_script(
            "planted_jobserver",
            "sh",
            "-c",
            f'test -z "${{MAKEFLAGS+set}}" && test "${given}" = "{makeflags}"',
            MAKEFLAGS=makeflags,
        )
        expect(
            "the test gets no MAKEFLAGS, and it gets the value of MAKEFLAGS of the launcher as the jobserver of a test",
            status == 0,
        )

    def limit_cases(self, expect: testing.Expect) -> None:
        """Check the limit of test-memory, also on a CI runner."""
        program = self.program
        self.budget(2000)
        status, said = self.launch(program)
        expect("a test at or below the limit passes with no line", status == 0 and "[test-" not in said)

        self.budget(0)
        status, said = self.launch(program)
        expect(
            "a passed test above the memory limit fails, with an error line that names its program",
            status == 1 and f"{program}:0: error: [test-memory] " in said and "no row of" in said,
        )
        record = self._record_of("test_planted")
        expect("the record of the failed test says rejected", record["result"] == "rejected")
        status, said = self.launch_script("planted_script", "sh", "-c", "exit 0")
        expect(
            "a passed script test above the memory limit fails, with an error line that names its record",
            status == 1
            and f"{cost_record.test_record_file(self.build, 'planted_script')}:0: error: [test-memory] " in said
            and self._record_of("planted_script")["result"] == "rejected",
        )
        status, said = self.launch(program, "3")
        expect(
            "a test that failed keeps its status above the limit",
            status == 3 and ": error: [test-memory] " in said,
        )
        status, said = self.launch(program, GITHUB_ACTIONS="true")
        expect(
            "on a CI runner, a passed test above the memory limit still fails",
            status == 1 and ": error: [test-memory] " in said,
        )

    def _count_of(self, name: str) -> object:
        """Return the instruction count of the record of one test, or None when the record holds no count."""
        cost = self._record_of(name).get("cost")
        return cost.get(cost_record.TEST_COUNT_KEY) if isinstance(cost, dict) else None

    def count_cases(self, expect: testing.Expect) -> None:
        """Check test-instructions (THE MEASURED RULES).

        A planted test that does more work than the limit, or than its row keeps, fails.  A run with no count, as on a
        host with no accurate counter, has no judge of its instructions.
        """
        self.ledger("test-memory", "")
        self.ledger("test-instructions", "")
        host_counts = cost_meter.host_gives_counter()
        on_host = f"(this host: {'a counter' if host_counts else 'no counter'})"
        worker = self.plant_test(
            "test_working", f"#!{sys.executable} -S\ntotal = 0\nfor index in range(2_000_000):\n    total += index\n"
        )
        self.budget(2000, 0.0002)
        status, said = self.launch(worker, **{cost_meter.INSTRUCTIONS_ENV: "0"})
        expect(
            f"a run with {cost_meter.INSTRUCTIONS_ENV}=0 has no count and no judge of its instructions, and it passes",
            status == 0 and "[test-instructions]" not in said and self._count_of("test_working") is None,
        )
        self.budget(2000)
        status, said = self.launch(worker)
        count = self._count_of("test_working")
        expect(
            f"a registry with no rule test-instructions judges no count, and the record keeps the count {on_host}",
            status == 0
            and "[test-instructions]" not in said
            and ((isinstance(count, int) and count > 0) if host_counts else count is None),
        )
        if not host_counts:
            expect(f"skip the cases of test-instructions {on_host}", True)
            return
        self.budget(2000, 0.0002)
        status, said = self.launch(worker)
        measured = self._count_of("test_working")
        expect(
            "a passed test above the limit of test-instructions fails, with an error line that gives its count",
            status == 1 and ": error: [test-instructions] " in said and " G user instructions" in said,
        )
        if not isinstance(measured, int):
            expect("the record of a counted test keeps its count", False)
            return
        giga = measured / test_launcher.GIGA
        self.ledger("test-instructions", f"- | - | {giga * 2:.6f} | test_working | a planted reason\n")
        status, said = self.launch(worker)
        expect("a ledger row keeps a test up to its kept value, with no line", status == 0 and "[test-" not in said)
        self.ledger("test-instructions", f"- | - | {giga / 2:.6f} | test_working | a planted reason\n")
        status, said = self.launch(worker)
        expect(
            "a test that does more work than its row keeps fails again",
            status == 1 and ": error: [test-instructions] " in said and "more than the row" in said,
        )
        self.ledger("test-instructions", "")

    def ledger_cases(self, expect: testing.Expect) -> None:
        """Check the rows of a ledger, and a test that cannot start."""
        program = self.program
        self.budget(0)
        self.ledger("test-memory", "- | - | 0.05 | test_planted | a planted reason\n")
        status, said = self.launch(program)
        expect("a ledger row admits the test, with no line", status == 0 and "[test-" not in said)
        self.ledger("test-memory", "- | x86_64-release | 0.05 | test_planted | a planted reason\n")
        status, said = self.launch(program)
        expect("a ledger row with a KIND admits nothing", status == 1)
        self.ledger("test-memory", "@build/test/test_planted | - | 0.05 | - | a planted reason\n")
        status, said = self.launch(program)
        expect("a ledger row that names a file and not a test admits nothing", status == 1)
        self.ledger("test-memory", "- | - | 0.000001 | test_planted | a planted reason\n")
        status, said = self.launch(program)
        expect(
            "a test over the kept value of its row with the noise of the unit fails again",
            status == 1 and ": error: [test-memory] " in said and "more than the row" in said,
        )
        (self.build / cost_meter.CONFIGURATION_FILE).write_text("x86_64-debug-asan-bench\n")
        self.ledger("test-memory", "- | - | 0.05 | test_planted | a row of each build\n")
        status, said = self.launch(program)
        expect("a ledger row keeps the test of a build with options too", status == 0 and "[test-" not in said)
        (self.build / cost_meter.CONFIGURATION_FILE).unlink()
        self.budget(2000)
        status, said = self.launch(program)
        expect(
            "a ledger row whose test stays at or under the limit passes the test with no finding, because a ledger "
            "fails only on a growth",
            status == 0 and "test-memory" not in said,
        )
        status, said = self.launch(self.root / "no-such-test")
        expect("a test that cannot start gives status 127", status == 127 and "cannot start" in said)

    def core_cases(self, expect: testing.Expect) -> None:
        """Check the core limit of a test, of a child of the test and of the launcher (CORE DUMPS of the launcher)."""
        # This process sets its own soft core limit to 0.  The limit that a test sees then tells the launcher apart
        # from the environment.  A core dump that the launcher does not stop goes to the handler of
        # kernel.core_pattern, which ignores a limit of 0.  At a hard limit of 0, no process can set 1 byte, and the
        # cases then check only the limit that the environment gives.
        self.budget(2000)
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


def fallen_row_cases(expect: testing.Expect) -> None:
    """Check that a run below the limit gives no finding for the row that keeps the test."""
    budget = {"test-memory": 1.5}
    run = cost_meter.Measurement(0, 0.1, 0.0, 0.2, 50 * 1024, 1.0, 0.0, None)
    with tempfile.TemporaryDirectory(prefix="test-step-rows-") as directory:
        Path(cost_meter.ledger_file("test-memory", directory)).write_text(
            "format 1\nrule test-memory\n- | - | 0.6 | test_planted | a planted row\n", encoding="utf-8"
        )
        measured = test_launcher.judge("test_planted", "test/test_planted", run, budget, directory)
    expect(
        "a run under the limit gives no finding for the row that keeps the test, because a ledger fails only on a "
        "growth",
        measured == [],
    )


def bench_scope_cases(expect: testing.Expect) -> None:
    """Check that a test of the directory bench of the build has no judge, and that each other test has one."""
    budget = {"test-memory": 1.5}
    run = cost_meter.Measurement(0, 0.1, 0.0, 0.2, 2 * 1024 * 1024, 1.0, 0.0, None)
    with tempfile.TemporaryDirectory(prefix="test-step-scope-") as directory:
        build = str(Path(directory) / "build")
        bench = test_launcher.program_item(f"{build}/bench/bench_heavy", build)
        expect(
            "program_item() gives the program relative to the build directory, and a program of PATH as it is",
            bench == "bench/bench_heavy" and test_launcher.program_item("python3", build) == "python3",
        )
        expect(
            "a test of the directory bench above the limit has no finding",
            test_launcher.judge("bench_heavy", bench, run, budget, directory) == [],
        )
        judged = test_launcher.judge("test_heavy", "test/test_heavy", run, budget, directory)
        expect(
            "a test of a different directory above the limit has an error",
            [row for _, row, _ in judged] == ["test-memory"],
        )
        outside = test_launcher.judge("test_away", "/elsewhere/bench/x", run, budget, directory)
        expect(
            "a program outside the build directory is judged, also in a directory with the name bench",
            [row for _, row, _ in outside] == ["test-memory"],
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
    emulated = ["/usr/bin/qemu-aarch64-static", "-L", "/sysroot", *executable]
    expect(
        "a test of a cross build runs its emulator behind the launcher, and its program is the word after the emulator",
        test_launcher.launched([*launcher, "--emulator-words", "3", *emulated]) == emulated
        and test_launcher.launched_program([*launcher, "--emulator-words", "3", *emulated]) == executable[0],
    )
    expect(
        "the program of a test with no emulator is the word after the launcher, and of a command with no launcher its "
        "first word",
        test_launcher.launched_program([*launcher, "--build-dir", "/build", *executable]) == executable[0]
        and test_launcher.launched_program(executable) == executable[0],
    )
    expect(
        "a count of the emulator that leaves no program gives no program",
        test_launcher.launched_program([*launcher, "--emulator-words", str(len(emulated)), *emulated]) is None,
    )


def environment_unit_cases(expect: testing.Expect) -> None:
    """Check the clean environment that the launcher makes from its own environment (THE ENVIRONMENT)."""
    host = {
        "PATH": "/usr/bin::relative/bin:/bin:/usr/bin",
        "HOME": "/home/planted",
        "LD_LIBRARY_PATH": "/opt/lib:",
        "GIT_DIR": "/repo/.git",
        "LANG": "ru_RU.UTF-8",
        "TZ": "Europe/Moscow",
        "CRUCIBLE_PLANTED": "kept",
        "ASSAY_TEST_CXX": "/opt/g++",
        "SESSION_ORACLE_CACHE": "/cache",
        "ASAN_OPTIONS": "detect_leaks=0",
        test_launcher.LAUNCHED_ENV: "an_earlier_test",
        cost_meter.MAKEFLAGS_ENV: "-j4 --jobserver-auth=fifo:/tmp/planted.fifo",
        cost_meter.TEST_JOBSERVER_ENV: "-j4 --jobserver-auth=fifo:/tmp/earlier.fifo",
    }
    fixed = dict(test_launcher.canonical_environment(host))
    expect(
        "the launcher keeps HOME, the sanitizer options and the variables of its prefixes",
        fixed["HOME"] == "/home/planted"
        and fixed["ASAN_OPTIONS"] == "detect_leaks=0"
        and fixed["CRUCIBLE_PLANTED"] == "kept"
        and fixed["ASSAY_TEST_CXX"] == "/opt/g++"
        and fixed["SESSION_ORACLE_CACHE"] == "/cache",
    )
    expect(
        "the launcher drops each other variable, the name of the test that it sets itself, and the two jobserver "
        "variables, so the clean environment holds no FIFO of a test run",
        not {
            "LD_LIBRARY_PATH",
            "GIT_DIR",
            test_launcher.LAUNCHED_ENV,
            cost_meter.MAKEFLAGS_ENV,
            cost_meter.TEST_JOBSERVER_ENV,
        }
        & set(fixed),
    )
    expect(
        "the launcher sets the locale and the time zone, also over the values of the shell",
        fixed["LANG"] == "C.UTF-8" and fixed["LC_ALL"] == "C.UTF-8" and fixed["TZ"] == "UTC",
    )
    expect(
        "PATH keeps each absolute directory one time, and drops an empty item and a relative item",
        fixed["PATH"] == "/usr/bin:/bin",
    )
    expect(
        "a PATH with no absolute directory goes, and the result is sorted by name",
        "PATH" not in dict(test_launcher.canonical_environment({"PATH": ":relative"}))
        and list(test_launcher.canonical_environment(host)) == sorted(test_launcher.canonical_environment(host)),
    )
    expect(
        "a variable of FIXED_VALUES is not a kept variable, so a property cannot set it",
        not any(test_launcher.is_kept(name) for name, _ in test_launcher.FIXED_VALUES),
    )
    planted_root = {**host, cache_root.ROOT_VARIABLE: "/planted/caches"}
    rooted = dict(test_launcher.canonical_environment(planted_root))
    stopped = dict(test_launcher.canonical_environment({**host, cache_root.ROOT_VARIABLE: cache_root.OFF}))
    expect(
        "the launcher keeps the working directory and the script directory off sys.path, and it gives the "
        "directory pycache of the cache root of the environment to each Python process of the test",
        rooted[test_launcher.SAFE_PATH_ENV] == "1"
        and rooted[test_launcher.PYCACHE_PREFIX_ENV] == "/planted/caches/pycache"
        and test_launcher.NO_BYTECODE_ENV not in rooted,
    )
    with tempfile.TemporaryDirectory() as scratch:
        (Path(scratch) / test_launcher.TEST_GROUPS / "sample").mkdir(parents=True)
        master = [sys.executable, "-S", str(Path(scratch) / test_launcher.GUARD_MASTER)]
        derived = test_launcher.has_derived_inputs
        expect(
            "a rule test and a group test of the master guard get no trace, because loop test gives their inputs",
            derived([*master, "check", "--rule", "x"])
            and derived([*master, "witness", "--engine", "plugin"])
            and derived([*master, "selftest", "sample", "--part", "0/2"]),
        )
        expect(
            "the test of one module, a name that is no group, a different master and an empty command get the trace",
            not derived([*master, "selftest", "tests.loop.test_x"])
            and not derived([*master, "selftest", "core.cache"])
            and not derived([*master, "selftest", "absent"])
            and not derived([*master, "warnings", scratch])
            and not derived([sys.executable, "-S", str(Path(scratch) / test_launcher.MASTER), "fixture"])
            and not derived(["/tree/build/bin/test_x", str(Path(scratch) / test_launcher.GUARD_MASTER)])
            and not derived([]),
        )
    expect(
        "the clean environment holds no mark of a traced run, so a launcher in the test drops it",
        test_launcher.TRACED_ENV
        not in dict(test_launcher.canonical_environment({**host, test_launcher.TRACED_ENV: "1"})),
    )
    expect(
        "with the caches off, the launcher stops the writes of compiled modules and names no prefix, as the masters do",
        stopped[test_launcher.NO_BYTECODE_ENV] == "1" and test_launcher.PYCACHE_PREFIX_ENV not in stopped,
    )


def planted_group(cases: Callable[[Planted, testing.Expect], None]) -> testing.Group:
    """Return a group that runs one method of Planted with a fixture of its own in its own scratch directory.

    The groups can run at the same time (THE GROUPS AT THE SAME TIME of assay.core.testing).  Each one writes the
    planted registry and ledgers of its fixture.
    """

    def group(expect: testing.Expect) -> None:
        """Run the cases in a new scratch directory."""
        with tempfile.TemporaryDirectory(prefix="test-step-") as work:
            cases(Planted(Path(work)), expect)

    group.__name__ = cases.__name__
    return group


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each.  Each group of the launcher has a scratch directory of its own."""
    return testing.run(
        __name__.removeprefix("assay."),
        argv,
        (
            planted_group(Planted.status_cases),
            planted_group(Planted.script_cases),
            planted_group(Planted.limit_cases),
            planted_group(Planted.count_cases),
            planted_group(Planted.ledger_cases),
            planted_group(Planted.core_cases),
            planted_group(Planted.emulator_cases),
            planted_group(Planted.environment_cases),
            fallen_row_cases,
            bench_scope_cases,
            version_unit_cases,
            launched_cases,
            environment_unit_cases,
        ),
    )
