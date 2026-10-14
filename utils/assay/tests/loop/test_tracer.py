"""The test of the trace of the system calls of a test (assay.loop.tracer).

`guard selftest tests.loop.test_tracer` runs the test.  The cases of the filter
run its program on planted seccomp data with a small BPF interpreter.  The cases
of the trace run `loop test-step` on planted test programs in a scratch build
directory.  One program opens a file by a path that it makes at run time.  One
starts a child that opens a file, and one has no reads of its own.  The scratch
build directory is in the work root of assay.core.cache.  A path in /tmp is a
fact of a read set and not a key part.  On a host that refuses the filter, the
launcher writes no read set, and the cases of the trace then write a note and
hold.  Each case writes one line.  Exit 0 when each case holds, else 1.
"""

import os
import shutil
import signal
import struct
import sys
import tempfile
from pathlib import Path

from assay.core import cache, proc, testing
from assay.core import meter as cost_meter
from assay.core import report as check_report
from assay.loop import readset, tracer

# The seccomp data of one call: the number, the architecture, the instruction pointer and six arguments.
DATA = struct.Struct("=iIQ6Q")


def run_filter(program: bytes, number: int, architecture: int, arguments: tuple[int, ...] = (0,) * 6) -> int:
    """Run a filter program on the seccomp data of one call, and return its result.

    The interpreter knows the five codes of tracer.filter_program().  Complexity: linear in the length of the program.

    Raises:
        ValueError: If the program holds a code that the interpreter does not know, or ends with no return
    """
    data = DATA.pack(number, architecture, 0, *arguments)
    index, accumulator = 0, 0
    while index < len(program) // 8:
        code, true, false, constant = struct.unpack_from("=HBBI", program, index * 8)
        index += 1
        if code == tracer.BPF_LOAD_WORD:
            accumulator = int.from_bytes(data[constant : constant + 4], "little")
        elif code == tracer.BPF_JUMP_EQUAL:
            index += true if accumulator == constant else false
        elif code == tracer.BPF_JUMP_SET:
            index += true if accumulator & constant else false
        elif code == tracer.BPF_AND:
            accumulator &= constant
        elif code == tracer.BPF_RETURN:
            return int(constant)
        else:
            raise ValueError(f"the code {code:#x} is not a code of the filter")
    raise ValueError("the program ends with no return")


def filter_cases(expect: testing.Expect) -> None:
    """Check the program of the filter of each architecture on planted calls (THE CALLS)."""
    notify, allow = tracer.RETURN_NOTIFY, tracer.RETURN_ALLOW
    x86 = tracer.ARCHITECTURES["x86_64"]
    program = tracer.filter_program(x86)
    read_only, write_only, read_write = 0, tracer.WRITE_ONLY, 2
    cases = (
        ("an openat for read stops", 257, (0, 0, read_only, 0, 0, 0), notify),
        ("an openat for read and write stops", 257, (0, 0, read_write, 0, 0, 0), notify),
        ("a write-only openat goes through", 257, (0, 0, write_only, 0, 0, 0), allow),
        ("an openat with O_PATH goes through", 257, (0, 0, tracer.PATH_ONLY, 0, 0, 0), allow),
        ("an open for read stops", 2, (0, read_only, 0, 0, 0, 0), notify),
        ("a write-only open goes through", 2, (0, write_only, 0, 0, 0, 0), allow),
        ("an openat2 stops, because its flags are in memory", 437, (0,) * 6, notify),
        ("an execve stops", 59, (0,) * 6, notify),
        ("a getdents64 stops", 217, (0,) * 6, notify),
        ("a write goes through", 1, (0,) * 6, allow),
    )
    for label, number, arguments, result in cases:
        expect(f"x86_64: {label}", run_filter(program, number, x86.audit, arguments) == result)
    expect("x86_64: a call of a different architecture goes through", run_filter(program, 257, 0x40000003) == allow)
    arm = tracer.ARCHITECTURES["aarch64"]
    arm_program = tracer.filter_program(arm)
    expect(
        "aarch64: an openat for read stops, a write-only openat and an execve of x86_64 numbers go through",
        run_filter(arm_program, 56, arm.audit) == notify
        and run_filter(arm_program, 56, arm.audit, (0, 0, write_only, 0, 0, 0)) == allow
        and run_filter(arm_program, 59, arm.audit) == allow
        and run_filter(arm_program, 221, arm.audit) == notify,
    )
    expect(
        "signed() reads AT_FDCWD and a descriptor from the low 32 bits of a word",
        tracer.signed(0xFFFFFFFFFFFFFF9C) == tracer.AT_FDCWD and tracer.signed((7 << 32) | 5) == 5,
    )


class Planted:
    """A scratch build directory with planted test programs, which are copies of sh."""

    def __init__(self, root: Path) -> None:
        """Make the build directory and its data directory in `root`."""
        self.build = root / "build"
        (self.build / "test").mkdir(parents=True)
        self.data = self.build / "data"
        self.data.mkdir()
        (self.build / cost_meter.KIND_FILE).write_text("x86_64-debug-asan\n")
        self.shell = shutil.which("sh") or "/bin/sh"

    def program(self, name: str) -> Path:
        """Write one planted test program, a copy of sh, and return its path."""
        path = self.build / "test" / name
        shutil.copyfile(self.shell, path)
        path.chmod(0o755)
        return path

    def launch(self, program: Path, script: str) -> int:
        """Run the launcher on one planted program with a script for sh, and return the exit code of the launcher."""
        launcher = [sys.executable, "-S", cost_meter.LOOP_MASTER, "test-step", "--build-dir", str(self.build)]
        done = proc.run(proc.Command([*launcher, str(program), "-c", script], timeout_s=proc.TOOL_S))
        return done.exit

    def read_set(self, name: str) -> readset.ReadSet | None:
        """Return the read set of one planted test, or None."""
        return readset.read(readset.read_set_file(self.build, name))


def keys_of(found: readset.ReadSet | None) -> dict[tuple[str, str], readset.Entry]:
    """Return the key parts of a read set by kind and path."""
    return {} if found is None else {(entry.kind, entry.path): entry for entry in found.key_parts()}


def trace_cases(expect: testing.Expect) -> None:
    """Check the read sets of the planted programs (THE TRACE, THE CALLS)."""
    work = cache.work_root()
    work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="test-tracer-", dir=work) as scratch:
        planted = Planted(Path(scratch))
        target = planted.data / "target.txt"
        target.write_text("one\n")
        runtime = planted.program("test_runtime_path")
        status = planted.launch(runtime, 'read -r line < "${0%/*}/../data/target.txt" && test "$line" = one')
        found = planted.read_set("test_runtime_path")
        if found is None:
            check_report.write_line("  note: the host refused the filter, so the trace cases hold with no read set")
            expect("a host with no trace runs the test with its status, and writes no read set", status == 0)
            return
        keys = keys_of(found)
        real_target = os.path.realpath(target)
        expect(
            "a program that opens a file by a path that it makes at run time gives that file as a key part",
            status == 0 and found.status == 0 and found.is_settled and (readset.READ, real_target) in keys,
        )
        expect(
            "the read set holds the command of the run, and the exec of the program",
            found.command[0] == str(runtime) and (readset.EXEC, os.path.realpath(runtime)) in keys,
        )
        first_mtime = os.stat(readset.read_set_file(planted.build, "test_runtime_path")).st_mtime_ns
        planted.launch(runtime, 'read -r line < "${0%/*}/../data/target.txt" && test "$line" = one')
        again = os.stat(readset.read_set_file(planted.build, "test_runtime_path")).st_mtime_ns
        expect("a second run with a current read set has no trace and keeps the read set", again == first_mtime)
        target.write_text("two\n")
        status = planted.launch(runtime, 'read -r line < "${0%/*}/../data/target.txt" && test "$line" = two')
        changed = keys_of(planted.read_set("test_runtime_path")).get((readset.READ, real_target))
        expect(
            "a change of a key part makes the next run a traced run, with the new signature",
            status == 0 and changed is not None and changed.end == readset.signature(real_target),
        )

        (planted.data / "child.txt").write_text("child\n")
        parent = planted.program("test_child")
        status = planted.launch(parent, 'cat "${0%/*}/../data/child.txt" > /dev/null')
        keys = keys_of(planted.read_set("test_child"))
        cat = shutil.which("cat") or ""
        expect(
            "a program that starts a child gives the exec of the child and the file that the child opens",
            status == 0
            and (readset.EXEC, os.path.realpath(cat)) in keys
            and (readset.READ, os.path.realpath(planted.data / "child.txt")) in keys,
        )

        quiet = planted.program("test_no_reads")
        status = planted.launch(quiet, "exit 0")
        keys = keys_of(planted.read_set("test_no_reads"))
        expect(
            "a program with no reads of its own gives no file of the build directory but its program",
            status == 0 and all(not path.startswith(str(planted.data)) for _, path in keys),
        )

        lister = planted.program("test_listing")
        status = planted.launch(lister, 'ls "${0%/*}/../data" > /dev/null && read -r line < /proc/self/status')
        found = planted.read_set("test_listing")
        facts = {(entry.kind, entry.path) for entry in found.entries if entry.role == readset.FACT} if found else set()
        expect(
            "a listing of a directory is a key part, and a read of /proc is a fact",
            status == 0
            and (readset.LIST, os.path.realpath(planted.data)) in keys_of(found)
            and (readset.READ, "/proc/self/status") in facts,
        )

        writer = planted.program("test_rewrites")
        (planted.data / "counter.txt").write_text("0\n")
        counter = '"${0%/*}/../data/counter.txt"'
        status = planted.launch(writer, f"read -r n < {counter}; echo 1 > {counter}")
        found = planted.read_set("test_rewrites")
        expect(
            "a test that changes a file that it read gives a read set that is not settled",
            status == 0 and found is not None and not found.is_settled,
        )

        failing = planted.program("test_fails")
        status = planted.launch(failing, "exit 3")
        found = planted.read_set("test_fails")
        expect(
            "the status of a traced test goes through, and the read set holds it",
            status == 3 and found is not None and found.status == 3,
        )
        killed = planted.program("test_killed")
        status = planted.launch(killed, "kill -TERM $$")
        expect("a traced test that a signal stops ends on the same signal", status == -signal.SIGTERM)


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (filter_cases, trace_cases))
