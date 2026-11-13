"""The test of the launcher crucible-caps (assay.loop.caps).

`guard selftest tests.loop.test_caps` runs the test.  Each case writes one line.  Exit 0 when each case holds, else 1.

The cases read the texts of getcap.  They plant a launcher file and a getcap program in a scratch directory, and they
hold a launcher that does not exist, a launcher that the user cannot run, a launcher with no capabilities, a launcher
with a part of them, and a ready launcher.  They also hold a getcap that fails or that does not exist, the words of a
command through the launcher, and the line of the fix.
"""

import grp
import os
import tempfile
from pathlib import Path

from assay.core import testing
from assay.loop import caps

# The text of getcap for the launcher of the build host, in the sequence that getcap gives.
READY = "cap_dac_read_search,cap_perfmon,cap_bpf=ep"
ALL = frozenset(caps.CAPABILITIES)


def planted_getcap(directory: Path, text: str, status: int = 0) -> str:
    """Write a planted getcap that prints its argument and a text, or nothing for no text, and exits with a status."""
    script = f"import sys\nif {text!r}:\n    print(sys.argv[1], {text!r})\nsys.exit({status})\n"
    return testing.planted_program(directory, f"getcap-{status}-{len(text)}", script)


def planted_launcher(directory: Path, mode: int) -> Path:
    """Write a planted launcher file with a mode, and return its path."""
    path = directory / f"crucible-caps-{mode:o}"
    path.write_text("a planted launcher\n", encoding="ascii")
    path.chmod(mode)
    return path


def text_cases(expect: testing.Expect) -> None:
    """Hold the names that a text of getcap puts in the effective and the permitted sets (THE CHECK)."""
    expect("the text of the launcher of the build host gives the three names", caps.held_capabilities(READY) == ALL)
    expect("a name in the permitted set only is not held", caps.held_capabilities("cap_bpf=p") == frozenset())
    expect(
        "a second clause removes a flag of the first clause",
        caps.held_capabilities("cap_bpf,cap_perfmon+ep cap_perfmon-e") == frozenset({"bpf"}),
    )
    expect(
        "two clauses with the sets eip and ep give their names",
        caps.held_capabilities("cap_bpf=eip cap_perfmon=ep") == frozenset({"bpf", "perfmon"}),
    )
    expect("a clause with no name gives each name", caps.held_capabilities("=ep") == ALL)
    expect(
        "the older text with a lone = before the clauses gives the names of the clauses",
        caps.held_capabilities("= cap_bpf+ep") == frozenset({"bpf"}),
    )
    expect("an empty text gives no name", caps.held_capabilities("") == frozenset())


def launcher_cases(expect: testing.Expect) -> None:
    """Hold the problem of a planted launcher with a planted getcap (THE CHECK)."""
    with tempfile.TemporaryDirectory(prefix="loop-caps-") as work:
        directory = Path(work)
        ready = planted_getcap(directory, READY)
        launcher = planted_launcher(directory, 0o750)
        absent = caps.launcher_problem(directory / "absent", ready)
        expect(f"a launcher that does not exist is not ready: {absent}", "does not exist" in absent)
        closed = caps.launcher_problem(planted_launcher(directory, 0o640), ready)
        expect(f"a launcher that the user cannot run is not ready: {closed}", "cannot run it" in closed)
        bare = caps.launcher_problem(launcher, planted_getcap(directory, ""))
        expect(
            f"a launcher with no capabilities is not ready, and the problem names each one: {bare}",
            all(f"cap_{name}" in bare for name in caps.CAPABILITIES),
        )
        part = caps.launcher_problem(launcher, planted_getcap(directory, "cap_bpf=ep"))
        expect(
            f"a launcher with a part of the capabilities names only the missing ones: {part}",
            "cap_perfmon" in part and "cap_dac_read_search" in part and "cap_bpf" not in part,
        )
        permitted = caps.launcher_problem(launcher, planted_getcap(directory, READY.replace("=ep", "=p")))
        expect(f"a launcher with no effective set is not ready: {permitted}", "cap_bpf" in permitted)
        expect("a launcher with the three capabilities is ready", caps.launcher_problem(launcher, ready) == "")
        failed = caps.launcher_problem(launcher, planted_getcap(directory, READY, 1))
        expect(f"a getcap that fails gives a problem: {failed}", "exits with 1" in failed)
        missing = caps.launcher_problem(launcher, str(directory / "no-getcap"))
        expect(f"a getcap that does not exist gives a problem: {missing}", "getcap cannot read" in missing)


def words_cases(expect: testing.Expect) -> None:
    """Hold the words of a command through the launcher, and the line of the fix (THE LAUNCHER, THE FIX)."""
    path = Path("/planted/crucible-caps")
    raised = "+bpf,+perfmon,+dac_read_search"
    expect(
        "the launcher puts the capabilities in the inheritable and the ambient sets before the command",
        caps.wrapped(("ctest", "-L", "x"), path)
        == [str(path), "--inh-caps", raised, "--ambient-caps", raised, "--", "ctest", "-L", "x"],
    )
    group = grp.getgrgid(os.getgid()).gr_name
    line = caps.fix_line("the launcher is absent", path)
    expect(f"the group of the fix is the group of the user: {group}", caps.user_group() == group)
    expect(
        f"the fix is one line with the problem and the two commands: {line}",
        "\n" not in line
        and line.startswith("the launcher is absent.  ")
        and f"sudo install -o root -g {group} -m 0750 {caps.SETPRIV} {path}" in line
        and line.endswith(f" cap_bpf,cap_perfmon,cap_dac_read_search+ep {path}"),
    )


def main(argv: list[str]) -> int:
    """Run each case, and return 0 when each case holds."""
    return testing.run(__name__.removeprefix("assay."), argv, (text_cases, launcher_cases, words_cases))
