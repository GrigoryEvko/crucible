"""x86level — the x86-64 level of the host, and the check of the level that a test or a bench needs.

THE NEED
    The configure of a build gives each executable target the x86-64 level
    that its units need (THE NEED OF A TEST of cmake/TestLevel.cmake).  The
    launcher of a test gets the need as the option --x86-level of `loop
    test-step`.  The file NEEDS_FILE of the build directory gives the need of
    each target that needs more than v1, one row "TARGET vN" for each, for a
    start of a bench with no test launcher.

THE LEVEL OF A HOST
    Python cannot read CPUID.  The level probe of the build,
    PROBE_DIRECTORY/PROBE_LIBRARY, compiles at x86-64 (v1), and it reads the
    level of the host through the level door of foundation/os/X86Level.h, the
    one reader of the host level (vessel/torch/level_probe.cpp).  The text of
    REQUEST_VARIABLE, such as CRUCIBLE_ISA_LEVEL=v2, asks for a level at or
    below the level of the host.  The check then uses that level, so a run on
    this host plants a host of a lower level.  The probe ends the process with
    fatal for a request above the host and for a text that is no level.

    One load of the probe costs approximately 10 ms, and each test of the
    build of the preset default needs the level v3.  So the first check of a
    build writes the level into CACHE_FILE of the build directory, with the
    boot id of the kernel and the request.  A check with the same boot id and
    the same request reads the file and loads no probe.  A different boot or a
    different request loads the probe again.

THE CHECK
    refusal() gives the text of a skip when the host is below the need, else
    None.  A launcher prints the text and ends with SKIP_CODE before the
    program starts.

`guard selftest tests.loop.test_teststep` does a test of the module through the test launcher.
"""

import os

# The type of a path that each function takes: a text, or an object of os.PathLike such as a Path.  The module
# imports no pathlib, because the test launcher imports this module, and each test pays its imports.
type PathText = str | os.PathLike[str]

# The variable of a request of a level (THE LEVEL OF A HOST), which the loader of the vessel also reads.
REQUEST_VARIABLE = "CRUCIBLE_ISA_LEVEL"
# The level probe of a build, below its build directory (vessel/torch/CMakeLists.txt).
PROBE_DIRECTORY = "lib"
PROBE_LIBRARY = "libcrucible_level.so"
# Mirrors kLevelProbeAbiVersion of vessel/torch/level_probe.cpp.
PROBE_ABI_VERSION = 1
# The file of the needs of the targets, and the file of the level of the host, in a build directory.
NEEDS_FILE = "x86-level-needs.txt"
CACHE_FILE = "x86-host-level.txt"
# The boot id of the kernel, which changes at each boot (THE LEVEL OF A HOST).
BOOT_ID_FILE = "/proc/sys/kernel/random/boot_id"
# The exit of a launcher that skips a test, which each test of the tree takes as its SKIP_RETURN_CODE.
SKIP_CODE = 77
# The words of the levels, and the processor whose programs have a level.
LEVEL_WORDS = {"v1": 1, "v2": 2, "v3": 3, "v4": 4}
PROCESSOR = "x86_64"


class LevelError(Exception):
    """A need that is no level, or a probe or a file that the check cannot read."""


def is_x86_64() -> bool:
    """Tell whether this host is x86_64, the one processor whose programs have an x86-64 level."""
    return os.uname().machine == PROCESSOR


def parse_need(text: str) -> int:
    """Return the highest level of a list of levels with commas, such as "v1,v3", as a number from 1 to 4.

    An empty text and an empty item add no level, and a text with no level gives 1, because each x86_64 host runs v1.

    Raises:
        LevelError: If an item is not v1, v2, v3 or v4
    """
    need = 1
    for item in text.split(","):
        word = item.strip()
        if not word:
            continue
        if word not in LEVEL_WORDS:
            raise LevelError(f"the need {text!r} holds {word!r}, and the levels are {', '.join(LEVEL_WORDS)}")
        need = max(need, LEVEL_WORDS[word])
    return need


def read_text(path: PathText) -> str:
    """Return the text of a file in ASCII.

    Raises:
        OSError: If the file cannot be read
        UnicodeDecodeError: If the file is not ASCII
    """
    with open(path, encoding="ascii") as handle:
        return handle.read()


def boot_id(path: PathText = BOOT_ID_FILE) -> str:
    """Return the boot id of the kernel, or an empty text when it cannot be read."""
    try:
        return read_text(path).strip()
    except OSError, UnicodeDecodeError:
        return ""


def probe_level(probe: PathText, request: str) -> int:
    """Load the level probe at `probe`, and return the level that it picks for the request (THE LEVEL OF A HOST).

    Raises:
        LevelError: If the probe is not there, cannot load, or has a different ABI stamp
    """
    import ctypes

    if not os.path.isfile(probe):
        raise LevelError(
            f"{probe} does not exist, and the check of a level reads the level of the host through it.  Build the "
            f"target crucible_level of the build"
        )
    try:
        library = ctypes.CDLL(os.fspath(probe))
    except OSError as problem:
        raise LevelError(f"{probe} does not load: {problem}") from None
    stamp = library.crucible_level_abi_version
    stamp.restype = ctypes.c_uint64
    stamp.argtypes = []
    if stamp() != PROBE_ABI_VERSION:
        raise LevelError(f"{probe} has the ABI stamp {stamp()}, and this module reads the stamp {PROBE_ABI_VERSION}")
    pick = library.crucible_level_pick
    pick.restype = ctypes.c_uint32
    pick.argtypes = [ctypes.c_char_p, ctypes.c_uint64]
    encoded = request.encode()
    return int(pick(encoded, len(encoded)))


def host_level(build_dir: PathText, request: str | None = None, boot: str | None = None) -> int:
    """Return the level of the host for the request, from the cache of the build or from its probe (THE LEVEL OF A HOST).

    Args:
        build_dir: The build directory, which holds the probe and the cache
        request: The text of the request, or None for the value of REQUEST_VARIABLE
        boot: The boot id, or None for the boot id of the kernel

    Raises:
        LevelError: If the probe is not there, cannot load, or has a different ABI stamp
    """
    asked = os.environ.get(REQUEST_VARIABLE, "") if request is None else request
    current = boot_id() if boot is None else boot
    is_cached = bool(current) and asked == "".join(asked.split())
    key = f"{current} {asked or '-'}"
    cache = os.path.join(build_dir, CACHE_FILE)
    if is_cached:
        try:
            words = read_text(cache).split()
        except OSError, UnicodeDecodeError:
            words = []
        if len(words) == 3 and " ".join(words[:2]) == key and words[2] in ("1", "2", "3", "4"):
            return int(words[2])
    level = probe_level(os.path.join(build_dir, PROBE_DIRECTORY, PROBE_LIBRARY), asked)
    if is_cached:
        written = f"{cache}.{os.getpid()}"
        try:
            with open(written, "w", encoding="ascii") as handle:
                handle.write(f"{key} {level}\n")
            os.replace(written, cache)
        except OSError:
            try:
                os.unlink(written)
            except OSError:
                pass
    return level


def refusal(need: int, host: int, request: str, subject: str) -> str | None:
    """Return the text of a skip when the host is below the need, else None (THE CHECK).

    Args:
        need: The level that the program needs, from 1 to 4
        host: The level of the host, from 1 to 4
        request: The text of REQUEST_VARIABLE, or an empty text
        subject: The words that name the program, such as "the test test_x"
    """
    if host >= need:
        return None
    asked = f", which the request {REQUEST_VARIABLE}={request} gives" if request else ""
    return (
        f"skip: the host runs the x86-64 level v{host}{asked}, and {subject} needs the level v{need}.  The program "
        f"does not start"
    )


def needs_of(build_dir: PathText) -> dict[str, int]:
    """Return the need of each target of NEEDS_FILE of a build, or no need when the file is not there (THE NEED).

    Complexity: linear in the rows.

    Raises:
        LevelError: If a row is not "TARGET vN"
    """
    path = os.path.join(build_dir, NEEDS_FILE)
    try:
        lines = read_text(path).splitlines()
    except FileNotFoundError:
        return {}
    except (OSError, UnicodeDecodeError) as problem:
        raise LevelError(f"{path} cannot be read: {problem}") from None
    needs: dict[str, int] = {}
    for number, line in enumerate(lines, 1):
        words = line.split()
        if not words or line.startswith("#"):
            continue
        if len(words) != 2 or words[1] not in LEVEL_WORDS:
            raise LevelError(f"{path}:{number}: a row is 'TARGET vN', and the line is {line!r}")
        needs[words[0]] = LEVEL_WORDS[words[1]]
    return needs


def bench_refusal(build_dir: PathText, target: str) -> str | None:
    """Return the text of a skip of a bench target whose need is above the host, else None (THE CHECK).

    A start of a bench with no test launcher calls it: `loop bench`, `loop pgo` and the measurement of `loop evidence`.
    A host other than x86_64 and a target with no row need nothing.

    Raises:
        LevelError: If the needs or the probe cannot be read
    """
    if not is_x86_64():
        return None
    need = needs_of(build_dir).get(target, 1)
    if need <= 1:
        return None
    return refusal(
        need,
        host_level(build_dir),
        os.environ.get(REQUEST_VARIABLE, ""),
        f"the bench {target}",
    )
