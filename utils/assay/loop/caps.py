"""caps — the launcher crucible-caps, which runs a command with the capabilities of a BPF load and with no sudo.

THE LAUNCHER
    LAUNCHER is a copy of setpriv of util-linux that the owner installs one
    time (THE FIX).  root owns the file, and the group of the user can run
    it.  Its file capabilities put CAPABILITIES in the permitted and the
    effective sets.  wrapped() puts the launcher and its options before a
    command.  The launcher then adds CAPABILITIES to the inheritable and the
    ambient sets, and it runs the command.  The ambient set gives the
    capabilities to each child process of the command, also to each test
    that ctest starts.  These are the capabilities:
      bpf              bpf_prog_load, and the maps of a BPF object
      perfmon          perf_event_open on a tracepoint and on a counter of the PMU
      dac_read_search  a read of /sys/kernel/tracing/events/*/id and of the BTF, which only root can read
    A command that starts with setuid sudo loses the ambient set.  The
    launcher then comes after sudo in the words of a command.

THE CHECK
    launcher_problem() tells why the launcher cannot run a command, or it
    gives an empty text when the launcher is ready.  The launcher is ready
    when its file exists, the user can run it, and getcap shows each name of
    CAPABILITIES in the effective and the permitted sets
    (held_capabilities()).  The function starts getcap through
    assay.core.proc.  A getcap that cannot start or that fails gives a
    problem and no exception.

THE FIX
    fix_line() gives one line with the problem and the two commands that
    install the launcher.  The commands give the file to the group of the
    user that runs the check.  The owner runs them one time.  No command of
    the package runs sudo for the launcher.

`guard selftest tests.loop.test_caps` does a test of the module.
"""

import grp
import os
import re
from collections.abc import Sequence
from pathlib import Path

from assay.core import proc

LAUNCHER = Path("/usr/local/bin/crucible-caps")
# The capabilities of the launcher, by their names in setpriv (THE LAUNCHER).
CAPABILITIES = ("bpf", "perfmon", "dac_read_search")
# The program that reads the file capabilities, and the file that the fix copies (THE CHECK, THE FIX).
GETCAP = "getcap"
SETPRIV = Path("/usr/bin/setpriv")
# The prefix of a name in the text of getcap, and the sets that each capability needs: effective and permitted.
NAME_PREFIX = "cap_"
NEEDED_SETS = frozenset("ep")
# An operation of a clause of the text of getcap: "=" sets the flags, "+" adds them and "-" removes them.
OPERATION = re.compile(r"([=+-])([eip]*)")


def held_capabilities(text: str) -> frozenset[str]:
    """Return each name of a capability that a text of getcap puts in the effective and the permitted sets.

    The text holds the clauses after the path of the file, such as "cap_bpf,cap_perfmon=ep".  A clause gives a list of
    names and one or more operations of OPERATION.  A clause with no name applies to each name of CAPABILITIES.  The
    names come back with no prefix NAME_PREFIX.  Complexity: linear in the length of the text.
    """
    flags: dict[str, set[str]] = {}
    for clause in text.split():
        start = next((index for index, letter in enumerate(clause) if letter in "=+-"), len(clause))
        names = [name.removeprefix(NAME_PREFIX) for name in clause[:start].split(",") if name] or list(CAPABILITIES)
        for operator, letters in OPERATION.findall(clause[start:]):
            for name in names:
                held = flags.setdefault(name, set())
                if operator == "=":
                    held.clear()
                if operator == "-":
                    held.difference_update(letters)
                else:
                    held.update(letters)
    return frozenset(name for name, held in flags.items() if NEEDED_SETS <= held)


def launcher_problem(path: Path = LAUNCHER, getcap: str = GETCAP) -> str:
    """Return why the launcher cannot run a command, or an empty text when it is ready (THE CHECK).

    Args:
        path: The file of the launcher
        getcap: The program that reads the file capabilities
    """
    if not path.is_file():
        return f"the launcher {path} does not exist"
    if not os.access(path, os.X_OK):
        return f"the launcher {path} exists, and this user cannot run it"
    try:
        completed = proc.run(proc.Command([getcap, str(path)], timeout_s=proc.QUICK_S))
    except proc.ToolFailed as problem:
        return f"getcap cannot read the file capabilities of the launcher {path}: {problem}"
    if completed.exit != 0:
        return f"getcap exits with {completed.exit} on the launcher {path}: {completed.err.strip()}"
    held = held_capabilities(completed.out.strip().removeprefix(str(path)))
    missing = [NAME_PREFIX + name for name in CAPABILITIES if name not in held]
    if missing:
        return f"the launcher {path} does not have {', '.join(missing)} in the effective and the permitted sets"
    return ""


def wrapped(words: Sequence[str], path: Path = LAUNCHER) -> list[str]:
    """Return the words that run a command through the launcher, with CAPABILITIES in the ambient set (THE LAUNCHER)."""
    raised = ",".join(f"+{name}" for name in CAPABILITIES)
    return [str(path), "--inh-caps", raised, "--ambient-caps", raised, "--", *words]


def user_group() -> str:
    """Return the name of the group of this process, or its number when the group has no name (THE FIX)."""
    try:
        return grp.getgrgid(os.getgid()).gr_name
    except KeyError:
        return str(os.getgid())


def fix_line(problem: str, path: Path = LAUNCHER) -> str:
    """Return one line with a problem of the launcher and the two commands that install it (THE FIX)."""
    names = ",".join(NAME_PREFIX + name for name in CAPABILITIES)
    return (
        f"{problem}.  Install the launcher one time: sudo install -o root -g {user_group()} -m 0750 {SETPRIV} {path}, "
        f"then sudo setcap {names}+ep {path}"
    )
