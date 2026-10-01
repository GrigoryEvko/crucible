#!/usr/bin/env bash
# build-gauge.sh — the cost of one clean build of the tree and of one run of each test group.
#
# The script configures a fresh build directory with no compiler cache and
# builds `all`.  Then it runs the negative-compile fixtures, and then the
# other tests.  For each of the three steps it records the wall time, the
# user and system CPU time, and the largest resident set of one process,
# from /usr/bin/time.  At the end it prints the 20 compile steps that took
# the most time, from the ninja log of the build.
#
# usage: utils/scripts/build-gauge.sh [option]...
#   --jobs N          The build and test parallelism (48 without the option)
#   --preset NAME     The configure preset (default without the option)
#   --build-dir DIR   The build directory (<tree>/build-gauge without the option)
#   --target NAME     Build NAME and not `all`.  Give it again for more targets
#   --neg-regex RE    The ctest regex of the fixture run (^neg_ without the option)
#   --rest-regex RE   A ctest regex that limits the run of the other tests
#   --timeout S       The ctest limit for one test, in seconds (900 without the option)
#   --no-tests        Build, and run no test
#   -h, --help        Print this text
#
# A fresh directory is necessary for a clean build.  If DIR holds a
# CMakeCache.txt, the script removes DIR first.  The script refuses a DIR
# that exists and holds no CMakeCache.txt, and a DIR that contains the tree.
#
# The report goes to stdout and to DIR/gauge-report.txt.  The ninja log
# stays in DIR/.ninja_log, and the output of each step is in DIR/gauge-*.log.
#
# The host can be shared.  A number from a host with other load is not
# comparable with a number from a quiet host, so the report names the host,
# the commit and the job count.
#
# Exit codes:
#   0  The build passed, and each test that ran passed
#   1  The configure or the build failed, or a test failed.  The report
#      still holds each number that the script could measure
#   2  A usage error
#   3  A necessary tool is missing: cmake, ninja, ctest, python3 or /usr/bin/time
set -euo pipefail

. "$(dirname -- "${BASH_SOURCE[0]}")/repo_root.sh"

usage() {
    local line
    while IFS= read -r line; do
        case "$line" in
            '# usage:'*) printing=1 ;;
            '#   -h, --help'*)
                printf '%s\n' "${line#\# }"
                return 0
                ;;
        esac
        if [[ "${printing:-0}" == 1 ]]; then
            printf '%s\n' "${line#\# }"
        fi
    done <"${BASH_SOURCE[0]}"
}

jobs=48
preset=default
build_dir="$REPO_ROOT/build-gauge"
targets=()
neg_regex='^neg_'
rest_regex=''
test_timeout=900
run_tests=1

while [[ $# -gt 0 ]]; do
    case "$1" in
        --jobs | --preset | --build-dir | --target | --neg-regex | --rest-regex | --timeout)
            if [[ $# -lt 2 ]]; then
                echo "build-gauge: $1 takes a value." >&2
                exit 2
            fi
            case "$1" in
                --jobs) jobs="$2" ;;
                --preset) preset="$2" ;;
                --build-dir) build_dir="$2" ;;
                --target) targets+=("$2") ;;
                --neg-regex) neg_regex="$2" ;;
                --rest-regex) rest_regex="$2" ;;
                --timeout) test_timeout="$2" ;;
            esac
            shift 2
            ;;
        --no-tests)
            run_tests=0
            shift
            ;;
        -h | --help)
            usage
            exit 0
            ;;
        *)
            echo "build-gauge: unknown argument '$1'.  Run with --help for the options." >&2
            exit 2
            ;;
    esac
done

if ! [[ "$jobs" =~ ^[1-9][0-9]*$ && "$test_timeout" =~ ^[1-9][0-9]*$ ]]; then
    echo "build-gauge: --jobs and --timeout take a positive number." >&2
    exit 2
fi

for tool in cmake ninja ctest python3; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "build-gauge: $tool is not on PATH." >&2
        exit 3
    fi
done
if [[ ! -x /usr/bin/time ]]; then
    echo "build-gauge: /usr/bin/time is missing.  Install the time package." >&2
    exit 3
fi

# The directory must be a build directory that the script can remove, or a
# path that does not exist.
mkdir -p -- "$(dirname -- "$build_dir")"
build_dir="$(cd -- "$(dirname -- "$build_dir")" && pwd)/$(basename -- "$build_dir")"
case "$REPO_ROOT/" in
    "$build_dir"/*)
        echo "build-gauge: the build directory $build_dir contains the tree.  Give another --build-dir." >&2
        exit 2
        ;;
esac
if [[ -e "$build_dir" ]]; then
    if [[ ! -f "$build_dir/CMakeCache.txt" ]]; then
        echo "build-gauge: $build_dir exists and holds no CMakeCache.txt, so it is not a build directory." \
            "Give another --build-dir, or remove it yourself." >&2
        exit 2
    fi
    rm -rf -- "$build_dir"
fi
mkdir -p -- "$build_dir"

report="$build_dir/gauge-report.txt"
status=0

# The CPU time of one step comes from /usr/bin/time, which reads wait4.  The
# times include each child that the step waited for, so a build includes
# each compiler run and a ctest run includes each test.  The largest
# resident set is that of the largest single process.
timed_step() {
    # $1 = the step name, then the command.  Sets step_status.
    local name="$1"
    shift
    local log="$build_dir/gauge-$name.log"
    local timing="$build_dir/gauge-$name.time"
    set +e
    /usr/bin/time -f '%e %U %S %M' -o "$timing" "$@" >"$log" 2>&1
    step_status=$?
    set -e
    # /usr/bin/time writes a "Command exited with non-zero status" line
    # before the numbers when the step fails, so the numbers are on the
    # last line.
    local wall=0 user=0 system=0 peak_kb=0 line
    while IFS= read -r line; do
        if [[ "$line" =~ ^[0-9.]+\ [0-9.]+\ [0-9.]+\ [0-9]+$ ]]; then
            read -r wall user system peak_kb <<<"$line"
        fi
    done <"$timing"
    rm -f -- "$timing"
    printf '%-10s wall %8.1f s   cpu %9.1f s (user %9.1f, system %7.1f)   peak %6d MB   exit %d\n' \
        "$name" "${wall:-0}" "$(python3 -c "print(${user:-0} + ${system:-0})")" "${user:-0}" "${system:-0}" \
        "$(( ${peak_kb:-0} / 1024 ))" "$step_status" | tee -a "$report"
}

# The summary line of one ctest log, or a note that the run found no test.
test_summary() {
    local line found=''
    while IFS= read -r line; do
        # ctest prints "100% tests passed out of N", or with a failure
        # "99% tests passed, F tests failed out of N".
        if [[ "$line" =~ tests\ passed(,\ [0-9]+\ tests?\ failed)?\ out\ of\ [0-9]+ ]]; then
            found="$line"
        elif [[ "$line" == 'No tests were found!!!' ]]; then
            found='no test matched the regex'
        fi
    done <"$1"
    printf '           %s\n' "${found:-ctest printed no summary; read $1}" | tee -a "$report"
}

commit="$(git -C "$REPO_ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
if git -C "$REPO_ROOT" diff --quiet 2>/dev/null && git -C "$REPO_ROOT" diff --cached --quiet 2>/dev/null; then
    tree_state=clean
else
    tree_state=dirty
fi
{
    echo "build-gauge: host $(hostname), commit $commit ($tree_state), preset $preset, $jobs jobs"
    echo "build-gauge: build directory $build_dir, no compiler cache"
} | tee "$report"

export CCACHE_DISABLE=1
timed_step configure cmake --preset "$preset" -B "$build_dir" -DCRUCIBLE_USE_CCACHE=OFF
if [[ "$step_status" -ne 0 ]]; then
    echo "build-gauge: the configure failed.  Read $build_dir/gauge-configure.log." >&2
    exit 1
fi

build_args=(--build "$build_dir" -j "$jobs")
if [[ ${#targets[@]} -gt 0 ]]; then
    build_args+=(--target "${targets[@]}")
fi
timed_step build cmake "${build_args[@]}"
if [[ "$step_status" -ne 0 ]]; then
    echo "build-gauge: the build failed.  Read $build_dir/gauge-build.log." >&2
    status=1
fi

if [[ "$run_tests" -eq 1 && "$status" -eq 0 ]]; then
    # A run that finds no test measures nothing, so it fails.
    timed_step fixtures ctest --test-dir "$build_dir" -j "$jobs" --timeout "$test_timeout" --no-tests=error \
        -R "$neg_regex"
    [[ "$step_status" -eq 0 ]] || status=1
    test_summary "$build_dir/gauge-fixtures.log"
    rest_args=(--test-dir "$build_dir" -j "$jobs" --timeout "$test_timeout" --no-tests=error -E "$neg_regex")
    if [[ -n "$rest_regex" ]]; then
        rest_args+=(-R "$rest_regex")
    fi
    timed_step tests ctest "${rest_args[@]}"
    [[ "$step_status" -eq 0 ]] || status=1
    test_summary "$build_dir/gauge-tests.log"
fi

# The ninja log has one row for each output: start and end in milliseconds,
# the modification time, the output path and a hash.  A step that ran two
# times keeps its last row.  The source of an object is its path without
# the CMakeFiles/<target>.dir/ part and without the .o suffix.
python3 - "$build_dir/.ninja_log" <<'PY' | tee -a "$report"
import sys
from pathlib import Path

log = Path(sys.argv[1])
if not log.is_file():
    print("build-gauge: the build wrote no ninja log.")
    sys.exit(0)
steps = {}
for line in log.read_text(encoding="utf-8").splitlines():
    if line.startswith("#"):
        continue
    cells = line.split("\t")
    if len(cells) < 4 or not cells[3].endswith(".o"):
        continue
    steps[cells[3]] = (int(cells[1]) - int(cells[0])) / 1000.0


def source_of(output: str) -> str:
    """Return the source path of one object path of the build."""
    parts = output[:-2].split("/")
    if "CMakeFiles" in parts:
        start = parts.index("CMakeFiles")
        end = next((i for i in range(start, len(parts)) if parts[i].endswith(".dir")), start)
        parts = parts[:start] + parts[end + 1:]
    return "/".join(parts)


total = sum(steps.values())
print(f"compile steps: {len(steps)}, sum of their wall times {total:.0f} s")
print("the 20 slowest compile steps (wall seconds, from the ninja log):")
for output, seconds in sorted(steps.items(), key=lambda item: item[1], reverse=True)[:20]:
    print(f"  {seconds:8.1f}  {source_of(output)}")
PY

echo "build-gauge: the report is in $report"
exit "$status"
