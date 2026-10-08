"""no-shell — utils/ holds no shell script.

THE RULE
    Python can start each job that a shell script starts.  Each job of utils/
    is then a module of the package that a master starts.  The rule reads
    each file of git and each new file of utils/, but not the plants of
    utils/plants/, and it refuses:
      suffix   a file with the suffix .sh
      shebang  a shebang of a shell on the first line of a file, also after
               /usr/bin/env and its options.  The shells are sh, bash, dash,
               zsh, ksh, mksh, ash, fish, csh and tcsh
    A plant of a rule that reads shell scripts holds shell scripts.  The rule
    does not read utils/plants/ for that reason.

Complexity: one read of the first line of each file of utils/.
"""

from pathlib import PurePosixPath

from assay.core import report as check_report
from assay.guard.engines.meta.runner import PLANTS, MetaContext

RULE = "no-shell"
SCOPE = "utils/"
SUFFIX = ".sh"
SHEBANG = b"#!"
ENV = "env"
SHELLS = frozenset({"sh", "bash", "dash", "zsh", "ksh", "mksh", "ash", "fish", "csh", "tcsh"})
# The longest first line that the rule reads.
LINE_LIMIT = 512


def interpreter(first_line: bytes) -> str | None:
    """Return the name of the program of a shebang line, after /usr/bin/env and its options, or None."""
    if not first_line.startswith(SHEBANG):
        return None
    words = first_line[len(SHEBANG) :].decode("utf-8", "replace").split()
    if not words:
        return None
    name = PurePosixPath(words[0]).name
    if name == ENV:
        rest = [word for word in words[1:] if not word.startswith("-") and "=" not in word]
        return PurePosixPath(rest[0]).name if rest else None
    return name


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give each shell script of utils/ that is not part of a plant."""
    found: list[check_report.Finding] = []
    for path in context.tracked():
        if not path.startswith(SCOPE) or path.startswith(f"{PLANTS}/"):
            continue
        if path.endswith(SUFFIX):
            found.append(
                check_report.Finding(
                    "error",
                    path,
                    0,
                    RULE,
                    "the file has the suffix .sh, and utils/ holds no shell script.  Write its "
                    "job as a module of the package, which a master starts",
                    "suffix",
                    path,
                )
            )
            continue
        with open(context.root / path, "rb") as handle:
            program = interpreter(handle.readline(LINE_LIMIT))
        if program in SHELLS:
            found.append(
                check_report.Finding(
                    "error",
                    path,
                    1,
                    RULE,
                    f"the first line is a shebang of the shell {program}, and utils/ holds no "
                    f"shell script.  Write its job as a module of the package, which a master starts",
                    "shebang",
                    path,
                )
            )
    return found
