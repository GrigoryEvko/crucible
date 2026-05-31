#!/usr/bin/env bash
#
# run-bench.sh — build + cap + pinned-run Crucible benches on the isolated partition.
#
# Pipeline per target:
#   1. build via the `bench` CMake preset (incremental — no-op if up to date)
#   2. re-apply CAP_BPF+CAP_PERFMON FRESH (a rebuild relinks the binary and strips
#      file caps, so this MUST run after every build) — lets bench_harness.h's eBPF
#      SenseHub + SchedSwitch sensory line attach (paranoid=2 + unprivileged_bpf
#      _disabled=2 on this box mean caps are required; without them the harness
#      degrades gracefully — ns/percentiles unchanged, sensory line omitted)
#   3. pin the run to an isolated core (reads /sys/.../cpu/isolated; falls back to
#      $CRUCIBLE_BENCH_CORE with a warning if the bench partition isn't active yet)
#
# This script deliberately does NOT touch GPU clocks or the global cpufreq `boost`
# knob. Per-core governor / SMT-sibling offlining / isolcpus are owned by
# crucible-benchtune.service (applied at boot). This script only consumes them.
#
# Usage:
#   bench/run-bench.sh [opts] [target ...] [-- <args forwarded to each bench>]
#   bench/run-bench.sh                      # build+run the default hot-path set
#   bench/run-bench.sh bench_trace_ring     # one target
#   bench/run-bench.sh --no-build bench_arena
#   bench/run-bench.sh --list               # list buildable bench targets
#   CRUCIBLE_BENCH_CORE=92 bench/run-bench.sh bench_philox
#
# Env overrides:
#   CRUCIBLE_BENCH_BUILD  (default build-bench)   build dir for the `bench` preset
#   CRUCIBLE_BENCH_PRESET (default bench)         CMake build preset
#   CRUCIBLE_BENCH_CORE   (default 88)            fallback pin core if none isolated
#   CRUCIBLE_BENCH_JOBS   (default 8)             -j for incremental builds

set -euo pipefail

BUILD_DIR="${CRUCIBLE_BENCH_BUILD:-build-bench}"
PRESET="${CRUCIBLE_BENCH_PRESET:-bench}"
FALLBACK_CORE="${CRUCIBLE_BENCH_CORE:-88}"
JOBS="${CRUCIBLE_BENCH_JOBS:-8}"
# Matches the project's `bench-caps` CMake target exactly (bench/CMakeLists.txt):
#   cap_bpf             — bpf_prog_load (tracepoint)
#   cap_perfmon         — perf_event_open on tracepoint IDs
#   cap_dac_read_search — read root-only /sys/kernel/tracing/events/*/id + BTF
CAPS="cap_bpf,cap_perfmon,cap_dac_read_search=eip"

# Curated hot-path latency set (the clean-building ones; bench_dispatch /
# bench_meta_log are excluded — known bench-tree drift).
DEFAULT_TARGETS=(bench_trace_ring bench_arena bench_swiss_table bench_philox bench_pool_allocator)

# Repo root = parent of this script's dir, so the script works from any CWD.
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

log()  { printf '\033[1;36m» %s\033[0m\n' "$*"; }
warn() { printf '\033[1;33m! %s\033[0m\n' "$*" >&2; }
die()  { printf '\033[1;31m✗ %s\033[0m\n' "$*" >&2; exit 1; }

# ── arg parse ──────────────────────────────────────────────────────
do_build=1
list_only=0
targets=()
bench_args=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-build) do_build=0; shift ;;
        --list)     list_only=1; shift ;;
        -h|--help)  grep '^#' "${BASH_SOURCE[0]}" | sed 's/^# \?//'; exit 0 ;;
        --)         shift; bench_args=("$@"); break ;;
        -*)         die "unknown option: $1" ;;
        *)          targets+=("$1"); shift ;;
    esac
done
[[ ${#targets[@]} -eq 0 ]] && targets=("${DEFAULT_TARGETS[@]}")

[[ -f "$BUILD_DIR/CMakeCache.txt" ]] || \
    die "$BUILD_DIR not configured — run: cmake --preset $PRESET"

if [[ "$list_only" == 1 ]]; then
    log "buildable bench targets in $BUILD_DIR:"
    cmake --build "$BUILD_DIR" --target help 2>/dev/null | grep -oE '\bbench_[a-z_]+' | sort -u
    exit 0
fi

# ── pick the pin core ──────────────────────────────────────────────
# Prefer a kernel-advertised isolated CPU (set by isolcpus= via the boot
# service). Fall back to the configured core with a loud warning if the
# bench partition isn't active yet (e.g. before the first reboot).
isolated="$(cat /sys/devices/system/cpu/isolated 2>/dev/null || true)"
if [[ -n "$isolated" ]]; then
    PIN_CORE="${isolated%%[,-]*}"   # first core in the isolated list
    log "isolated partition active: [$isolated] → pinning to cpu$PIN_CORE"
    sib="/sys/devices/system/cpu/cpu$((PIN_CORE + 192))/online"
    [[ -r "$sib" && "$(cat "$sib")" == 0 ]] && log "  (SMT sibling offline — cpu$PIN_CORE runs single-threaded)"
else
    PIN_CORE="$FALLBACK_CORE"
    warn "no isolated CPUs advertised (bench partition not active — reboot pending?)."
    warn "falling back to cpu$PIN_CORE; results will be NOISY (shared, tick-active core)."
fi

# ── per-target: build → cap → run ──────────────────────────────────
apply_caps() {  # $1 = binary path
    local bin="$1"
    if [[ "$(getcap "$bin" 2>/dev/null)" == *"$CAPS"* ]]; then return 0; fi
    if sudo -n setcap "$CAPS" "$bin" 2>/dev/null; then
        log "  caps: $CAPS applied (eBPF sensory line enabled)"
    else
        warn "  caps: could not setcap (no passwordless sudo?) — eBPF sensory line"
        warn "        will be omitted; ns/percentiles are unaffected."
    fi
}

for t in "${targets[@]}"; do
    bin="$BUILD_DIR/bench/$t"
    if [[ "$do_build" == 1 ]]; then
        log "build $t"
        cmake --build "$BUILD_DIR" --target "$t" -j "$JOBS" >/dev/null \
            || die "build failed: $t (known-drifted bench? try another target)"
    fi
    [[ -x "$bin" ]] || die "missing binary: $bin (build it first, drop --no-build)"
    apply_caps "$bin"
    log "run  $t   (taskset -c $PIN_CORE)"
    taskset -c "$PIN_CORE" "$bin" "${bench_args[@]}"
    echo
done
