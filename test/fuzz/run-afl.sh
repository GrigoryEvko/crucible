#!/usr/bin/env bash
# Run one coverage-guided AFL++ campaign on one boundary harness.
#
#   test/fuzz/run-afl.sh BUILD_DIR OUT_DIR HARNESS CPU_LIST SECONDS
#
# BUILD_DIR  The instrumented build.  When BUILD_DIR has no CMakeCache.txt,
#            the script configures it with afl-g++-fast.  It also keeps a
#            second build, BUILD_DIR-cmplog, with comparison logging, for
#            the main instance.  It builds fuzz_HARNESS in both.
# OUT_DIR    The afl-fuzz output directory.  The seeds go to OUT_DIR/seeds,
#            and the harness scratch files go to OUT_DIR/tmp.  A harness that
#            afl-fuzz kills leaves its scratch directory there, so the script
#            removes OUT_DIR/tmp when it exits, however it exits.
# HARNESS    A name from test/fuzz/boundary/harnesses, such as region.
# CPU_LIST   The cores, such as 100-131 or 100-115,200-215.  One instance
#            runs on each core.  The first is the main instance.  A list
#            that names a reserved core is refused.
# SECONDS    The time limit of each instance.
#
# Environment:
#   CRUCIBLE_FUZZ_RESERVED_CPUS  cores that no instance may use (88-95)
#   CRUCIBLE_FUZZ_JOBS           build jobs (6)
#   AFL_BIN                      the directory of the AFL++ tools
#                                ($HOME/.local/bin)
set -euo pipefail

usage() {
    printf 'usage: %s BUILD_DIR OUT_DIR HARNESS CPU_LIST SECONDS\n' "$0" >&2
    exit 2
}
[[ $# -eq 5 ]] || usage

build_dir=$1
out_dir=$2
# The exit trap removes OUT_DIR/tmp, so an empty OUT_DIR would name /tmp.
[[ -n $out_dir ]] || { printf 'run-afl: OUT_DIR must not be empty\n' >&2; exit 2; }
harness=$3
cpu_list=$4
seconds=$5
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
afl_bin=${AFL_BIN:-$HOME/.local/bin}
jobs=${CRUCIBLE_FUZZ_JOBS:-6}
reserved=${CRUCIBLE_FUZZ_RESERVED_CPUS:-88-95}

[[ -f $root/test/fuzz/boundary/harnesses/$harness.h ]] || {
    printf 'run-afl: no harness named %s in test/fuzz/boundary/harnesses\n' "$harness" >&2
    exit 2
}
[[ $seconds =~ ^[0-9]+$ ]] || { printf 'run-afl: SECONDS must be a whole number\n' >&2; exit 2; }

# Prints one core number per line for a list such as 1-3,7.
expand_cpus() {
    local part first last cpu
    local -a parts
    IFS=, read -ra parts <<<"$1"
    for part in "${parts[@]}"; do
        if [[ $part =~ ^([0-9]+)-([0-9]+)$ ]]; then
            first=${BASH_REMATCH[1]}
            last=${BASH_REMATCH[2]}
        elif [[ $part =~ ^[0-9]+$ ]]; then
            first=$part
            last=$part
        else
            printf 'run-afl: %s is not a core or a range of cores\n' "$part" >&2
            exit 2
        fi
        for ((cpu = first; cpu <= last; ++cpu)); do printf '%s\n' "$cpu"; done
    done
}

mapfile -t cpus < <(expand_cpus "$cpu_list")
mapfile -t reserved_cpus < <(expand_cpus "$reserved")
(( ${#cpus[@]} > 0 )) || { printf 'run-afl: CPU_LIST names no core\n' >&2; exit 2; }
for cpu in "${cpus[@]}"; do
    for taken in "${reserved_cpus[@]}"; do
        if [[ $cpu == "$taken" ]]; then
            printf 'run-afl: core %s is reserved (%s)\n' "$cpu" "$reserved" >&2
            exit 2
        fi
    done
done

# The instrumented builds.  The compiler wrappers call the patched GCC 16.
gcc_bin=${CRUCIBLE_GCC16_PREFIX:-$HOME/.local/gcc16-patched}/usr/bin
export AFL_CC=$gcc_bin/gcc-16p
export AFL_CXX=$gcc_bin/g++-16p
configure_and_build() {
    local dir=$1
    if [[ ! -f $dir/CMakeCache.txt ]]; then
        cmake -S "$root" -B "$dir" -G Ninja \
            -DCMAKE_TOOLCHAIN_FILE="$root/cmake/Toolchain-gcc16.cmake" \
            -DCMAKE_C_COMPILER="$afl_bin/afl-gcc-fast" \
            -DCMAKE_CXX_COMPILER="$afl_bin/afl-g++-fast" \
            -DCMAKE_BUILD_TYPE=Debug -DCRUCIBLE_BENCH=OFF -DCRUCIBLE_WERROR=ON -DCRUCIBLE_FUZZ=ON
    fi
    # The cmake that configured the directory builds it (utils/scripts/cmake_pin.py).
    local build_cmake
    build_cmake=$(python3 "$root/utils/scripts/cmake_pin.py" --print-program "$dir" cmake)
    nice -n 10 "$build_cmake" --build "$dir" -j "$jobs" --target "fuzz_$harness"
}
configure_and_build "$build_dir"
AFL_GCC_CMPLOG=1 configure_and_build "$build_dir-cmplog"
binary=$build_dir/test/fuzz/fuzz_$harness
cmplog_binary=$build_dir-cmplog/test/fuzz/fuzz_$harness

mkdir -p "$out_dir/tmp"
trap 'rm -rf -- "$out_dir/tmp"' EXIT
"$binary" --write-seeds="$out_dir/seeds"

# ASan must abort and must not symbolize, so that afl-fuzz sees each crash.
# Core dumps go to the system handler on this host, so afl-fuzz is told not
# to wait for them.
export ASAN_OPTIONS=abort_on_error=1:symbolize=0:detect_leaks=0:allocator_may_return_null=1
export UBSAN_OPTIONS=halt_on_error=1:abort_on_error=1:symbolize=0
export AFL_SKIP_CPUFREQ=1
export AFL_NO_AFFINITY=1
export AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1
export AFL_NO_UI=1
export TMPDIR=$out_dir/tmp

pids=()
for index in "${!cpus[@]}"; do
    cpu=${cpus[index]}
    if ((index == 0)); then
        role=(-M main -c "$cmplog_binary")
    else
        role=(-S "secondary$index")
    fi
    taskset -c "$cpu" timeout --signal=INT $((seconds + 60)) \
        "$afl_bin/afl-fuzz" -i "$out_dir/seeds" -o "$out_dir/afl" -m none -t 2000 -V "$seconds" \
        "${role[@]}" -- "$binary" >"$out_dir/instance-$index.log" 2>&1 &
    pids+=($!)
done
printf 'run-afl: %s instances of fuzz_%s for %s seconds, output in %s/afl\n' \
    "${#pids[@]}" "$harness" "$seconds" "$out_dir"
status=0
for pid in "${pids[@]}"; do wait "$pid" || status=1; done
exit "$status"
