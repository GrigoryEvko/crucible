#!/usr/bin/env bash
# prof.sh — VTune profiling automation for Crucible build_trace optimization.
#
# Subcommands:
#   prof.sh bench    [tag] [func]   Run benchmark only (no VTune), print timing
#   prof.sh baseline [tag] [func]   Save current timing + VTune profile as baseline
#   prof.sh compare  [tag] [func]   Run again, compare against saved baseline
#   prof.sh profile  [tag] [func]   Just profile current state (no baseline save)
#   prof.sh tma      [tag] [func]   Full analysis: bench + hotspots + uarch + TMA
#   prof.sh tma-show [tag] [func]   Show TMA report from existing results
#   prof.sh show     [tag] [func]   Show source-line hotspots from existing result
#   prof.sh asm      [tag] [func]   Show assembly hotspots from existing result
#   prof.sh hotlines [tag]          Show top-N hottest source lines across ALL funcs
#   prof.sh list                    List all saved profiling runs
#   prof.sh clean                   Remove all profiling data
#
# Environment:
#   VTUNE        VTune binary path   (default: /opt/intel/oneapi/vtune/2025.9/bin64/vtune)
#   TARGET       Benchmark target    (default: bench_merkle_dag)
#   TRACE        Trace file          (default: bench/vit_b.crtrace)
#   FUNC         Function to focus   (default: crucible::BackgroundThread::build_trace)
#   RUNS         Benchmark runs      (default: 5)
#   TAG          Result tag          (default: git short hash)
#   VTUNE_ITERS  VTune iterations    (default: 10)
#   PCPU         P-core CPU to pin   (default: 8, i.e. CPU 8 = P-core 4)
#   BENCH_PATTERN  Grep pattern for benchmark output line (auto-detected from TARGET)
#
# Results stored in .prof/<tag>/ with:
#   timing.txt        — raw benchmark output from all runs
#   stats.txt         — parsed min/med/max statistics
#   vtune/            — VTune hotspots result directory
#   uarch/            — VTune uarch-exploration result directory
#   meta.txt          — git hash, date, compiler, build flags
#
set -euo pipefail
trap '' PIPE  # Ignore SIGPIPE — VTune report pipelines send through head/sort

# ── Configuration ─────────────────────────────────────────────────────

ROOT="$(cd "$(dirname "$0")" && pwd)"
VTUNE="${VTUNE:-/opt/intel/oneapi/vtune/2025.9/bin64/vtune}"
TRACE="${TRACE:-$ROOT/bench/vit_b.crtrace}"
FUNC="${FUNC:-crucible::BackgroundThread::build_trace}"
RUNS="${RUNS:-5}"
VTUNE_ITERS="${VTUNE_ITERS:-10}"
PCPU="${PCPU:-8}"   # P-core CPU id for taskset pinning (i7-14700HX: CPUs 0-15 are P-cores)
TARGET="${TARGET:-bench_merkle_dag}"
BINARY="$ROOT/build-bench/bench/$TARGET"
PROF_DIR="$ROOT/.prof"

# The benchmark line we parse.  When TARGET is bench_merkle_dag, anchor
# on "from file" to distinguish from the synthetic 481-op build_trace.
# For other targets, match the first timing line.  Override with
# BENCH_PATTERN env var for full control.
if [ -z "${BENCH_PATTERN:-}" ]; then
    case "$TARGET" in
        bench_merkle_dag) BENCH_PATTERN="build_trace.*from file" ;;
        *)                BENCH_PATTERN="ns/op" ;;
    esac
fi

# ── Colors (disabled if not a terminal) ───────────────────────────────

if [ -t 1 ]; then
    RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[0;33m'
    CYAN='\033[0;36m'; BOLD='\033[1m'; DIM='\033[2m'; RESET='\033[0m'
else
    RED=''; GREEN=''; YELLOW=''; CYAN=''; BOLD=''; DIM=''; RESET=''
fi

# ── Phase annotations for build_trace() source lines ────────────────
# Derived from BackgroundThread.h comments.

phase_for_line() {
    local line="${1:-0}"
    [ "$line" -gt 0 ] 2>/dev/null || { echo "???"; return; }
    if   [ "$line" -ge 414 ] && [ "$line" -le 439 ]; then echo "P0-scan"
    elif [ "$line" -ge 440 ] && [ "$line" -le 511 ]; then echo "P1-alloc"
    elif [ "$line" -ge 512 ] && [ "$line" -le 677 ]; then echo "P2-copy+hash"
    elif [ "$line" -ge 678 ] && [ "$line" -le 900 ]; then echo "P3-outputs"
    else echo "other"
    fi
}

# ── Utility functions ─────────────────────────────────────────────────

die()  { printf "${RED}ERROR: %s${RESET}\n" "$*" >&2; exit 1; }
info() { printf "${CYAN}>>> %s${RESET}\n" "$*"; }
warn() { printf "${YELLOW}WARNING: %s${RESET}\n" "$*"; }

# Resolve the tag for a profiling run.
resolve_tag() {
    local tag="${1:-${TAG:-}}"
    if [ -z "$tag" ]; then
        tag="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo 'unknown')"
    fi
    echo "$tag"
}

# Find exact column index (1-based) for a given exact column name in a
# tab-delimited header line.  Returns empty string if not found.
# Usage: col_idx "$(header)" "Performance-core (P-core):Retiring(%)"
col_idx() {
    local header="$1" name="$2"
    echo "$header" | tr '\t' '\n' | awk -v n="$name" '{
        if ($0 == n) { print NR; exit }
    }'
}

# Create a temp file that auto-deletes on script exit.
# Returns the path; caller should use it immediately.
_tmpfiles=()
make_tmp() {
    local f
    f="$(mktemp /tmp/crucible_prof_XXXXXX)"
    _tmpfiles+=("$f")
    echo "$f"
}
cleanup_tmp() {
    for f in "${_tmpfiles[@]+"${_tmpfiles[@]}"}"; do
        rm -f "$f" 2>/dev/null || true
    done
}
trap 'cleanup_tmp' EXIT

# Run a VTune report and write output to a temp file.
# Avoids storing 500KB+ CSV in a bash variable (which causes SIGPIPE on echo).
# Usage: vtune_to_file <tmpfile> <vtune args...>
vtune_to_file() {
    local outfile="$1"; shift
    "$VTUNE" "$@" 2>/dev/null > "$outfile" || true
}

# ── Build ─────────────────────────────────────────────────────────────

do_build() {
    info "Building $TARGET (bench preset)"
    cmake --preset bench -S "$ROOT" >/dev/null 2>&1
    cmake --build --preset bench -j"$(nproc)" --target "$TARGET" 2>&1 \
        | grep -v '^\[.*\] Built target\|^$\|^ninja' || true
    [ -x "$BINARY" ] || die "Build failed -- $BINARY not found"
    info "Binary: $BINARY ($(stat -c %s "$BINARY") bytes)"
}

# ── Function address extraction ───────────────────────────────────────

extract_func_addr() {
    local binary="$1" fn="$2"
    # Pipeline may fail if function is not present; || true for pipefail.
    nm -SC "$binary" 2>/dev/null \
        | grep -F "$fn" \
        | grep ' [tT] ' \
        | head -1 \
        | awk '{printf "0x%s 0x%s\n", $1, $2}' \
        || true
}

print_func_info() {
    local addr_info
    addr_info="$(extract_func_addr "$BINARY" "$FUNC")"
    if [ -n "$addr_info" ]; then
        local start size
        start="$(echo "$addr_info" | cut -d' ' -f1)"
        size="$(echo "$addr_info" | cut -d' ' -f2)"
        local end
        end="$(printf '0x%x' "$(( start + size ))")"
        printf "  Function: %s\n" "$FUNC"
        printf "  Address:  %s  size=%s  end=%s  (%d bytes)\n" \
            "$start" "$size" "$end" "$((size))"
    else
        warn "Could not extract function address for: $FUNC"
    fi
}

# ── Benchmark execution and parsing ───────────────────────────────────

run_bench_once() {
    # Only bench_merkle_dag takes a trace file argument.
    case "$TARGET" in
        bench_merkle_dag)
            taskset -c "$PCPU" "$BINARY" "$TRACE" 2>&1 ;;
        *)
            taskset -c "$PCPU" "$BINARY" 2>&1 ;;
    esac
}

parse_bench_line() {
    local output="$1"
    # Take only the FIRST matching line to avoid multi-line output from
    # benchmarks that print multiple timing sections.
    echo "$output" \
        | grep -E "$BENCH_PATTERN" \
        | head -1 \
        | awk 'match($0, /[[:space:]]([0-9.]+) ns\/op[[:space:]]+\(min=[[:space:]]*([0-9.]+)[[:space:]]+med=[[:space:]]*([0-9.]+)[[:space:]]+max=[[:space:]]*([0-9.]+)\)/, a) {
            printf "%s %s %s %s\n", a[1], a[2], a[3], a[4]
        }' || true
}

# Run benchmark N times, collect statistics, save to directory.
run_benchmark_suite() {
    local outdir="$1"
    local runs="${RUNS}"
    mkdir -p "$outdir"

    info "Running benchmark $runs times (pinned to CPU $PCPU, no profiler overhead)"

    local all_min="" all_med="" all_max=""
    local i=1

    while [ "$i" -le "$runs" ]; do
        printf "  Run %d/%d ... " "$i" "$runs"
        local output
        output="$(run_bench_once)"
        echo "$output" >> "$outdir/timing.txt"
        echo "---RUN-SEPARATOR---" >> "$outdir/timing.txt"

        local parsed
        parsed="$(parse_bench_line "$output")"
        if [ -z "$parsed" ]; then
            warn "Could not parse benchmark output for run $i"
            printf "(parse failed)\n"
            i=$((i + 1))
            continue
        fi

        local ns_op min_ns med_ns max_ns
        ns_op="$(echo "$parsed" | cut -d' ' -f1)"
        min_ns="$(echo "$parsed" | cut -d' ' -f2)"
        med_ns="$(echo "$parsed" | cut -d' ' -f3)"
        max_ns="$(echo "$parsed" | cut -d' ' -f4)"

        printf "min=%s  med=%s  max=%s ns/op\n" "$min_ns" "$med_ns" "$max_ns"

        all_min="$all_min $min_ns"
        all_med="$all_med $med_ns"
        all_max="$all_max $max_ns"

        i=$((i + 1))
    done

    # Compute statistics.
    local stats
    stats="$(echo "$all_min" | tr ' ' '\n' | grep -v '^$' | sort -g | awk '
        BEGIN { n=0; sum=0 }
        { vals[n++] = $1; sum += $1 }
        END {
            if (n == 0) { print "NO_DATA"; exit }
            printf "best_min=%.1f\navg_min=%.1f\n", vals[0], sum/n
        }
    ')"
    local med_stats
    med_stats="$(echo "$all_med" | tr ' ' '\n' | grep -v '^$' | sort -g | awk '
        BEGIN { n=0; sum=0 }
        { vals[n++] = $1; sum += $1 }
        END {
            if (n == 0) { print "NO_DATA"; exit }
            mid = int(n/2)
            printf "best_med=%.1f\navg_med=%.1f\nmedian_med=%.1f\n", vals[0], sum/n, vals[mid]
        }
    ')"

    {
        echo "runs=$runs"
        echo "$stats"
        echo "$med_stats"
        echo "raw_mins=($all_min )"
        echo "raw_meds=($all_med )"
    } > "$outdir/stats.txt"

    printf "\n${BOLD}Statistics ($runs runs):${RESET}\n"
    while IFS= read -r line; do
        printf "  %s\n" "$line"
    done < "$outdir/stats.txt"
}

# ── Save metadata ─────────────────────────────────────────────────────

save_meta() {
    local outdir="$1"
    local cc flags
    cc="$(grep 'CMAKE_CXX_COMPILER:FILEPATH' "$ROOT/build-bench/CMakeCache.txt" 2>/dev/null \
        | cut -d= -f2- || echo 'unknown')"
    flags="$(grep '^CMAKE_CXX_FLAGS:' "$ROOT/build-bench/CMakeCache.txt" 2>/dev/null \
        | head -1 | cut -d= -f2- || echo 'unknown')"
    {
        echo "date=$(date -Iseconds)"
        echo "git_hash=$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo 'unknown')"
        echo "git_dirty=$(git -C "$ROOT" status --porcelain 2>/dev/null | wc -l | tr -d ' ') files"
        echo "target=$TARGET"
        echo "binary=$BINARY"
        echo "trace=$TRACE"
        echo "function=$FUNC"
        echo "cxx_compiler=$cc"
        echo "cxx_flags=$flags"
        echo "pinned_cpu=$PCPU"
    } > "$outdir/meta.txt"
}

# ── VTune collection ──────────────────────────────────────────────────

# Create the VTune wrapper script that suppresses benchmark stdout.
make_vtune_wrapper() {
    local script
    script="$(mktemp /tmp/crucible_vtune_XXXXXX.sh)"
    # Only bench_merkle_dag takes a trace file argument.
    case "$TARGET" in
        bench_merkle_dag)
            printf '#!/bin/bash\nfor i in $(seq 1 %d); do\n  taskset -c %s "%s" "%s" >/dev/null\ndone\n' \
                "$VTUNE_ITERS" "$PCPU" "$BINARY" "$TRACE" > "$script" ;;
        *)
            printf '#!/bin/bash\nfor i in $(seq 1 %d); do\n  taskset -c %s "%s" >/dev/null\ndone\n' \
                "$VTUNE_ITERS" "$PCPU" "$BINARY" > "$script" ;;
    esac
    chmod +x "$script"
    echo "$script"
}

do_vtune_collect() {
    local outdir="$1"
    local vtune_dir="$outdir/vtune"

    [ -x "$VTUNE" ] || die "VTune not found at $VTUNE"

    info "Collecting VTune hotspots (HW sampling, ${VTUNE_ITERS} iterations, CPU $PCPU)"

    rm -rf "$vtune_dir"

    local vtune_script
    vtune_script="$(make_vtune_wrapper)"

    "$VTUNE" \
        -collect hotspots \
        -knob sampling-mode=hw \
        -knob sampling-interval=0.1 \
        -quiet \
        -result-dir "$vtune_dir" \
        -- "$vtune_script" \
        >/dev/null 2>&1 || true

    rm -f "$vtune_script"
    info "VTune hotspots result: $vtune_dir"
}

do_vtune_uarch_collect() {
    local outdir="$1"
    local uarch_dir="$outdir/uarch"

    [ -x "$VTUNE" ] || die "VTune not found at $VTUNE"

    info "Collecting VTune uarch-exploration (TMA breakdown, ${VTUNE_ITERS} iterations, CPU $PCPU)"

    rm -rf "$uarch_dir"

    local vtune_script
    vtune_script="$(make_vtune_wrapper)"

    "$VTUNE" \
        -collect uarch-exploration \
        -knob pmu-collection-mode=detailed \
        -quiet \
        -result-dir "$uarch_dir" \
        -source-search-dir "$ROOT/include" \
        -search-dir "$(dirname "$BINARY")" \
        -- "$vtune_script" \
        >/dev/null 2>&1 || true

    rm -f "$vtune_script"
    info "uarch result: $uarch_dir"
}

# ── TMA bar-chart report from uarch-exploration ──────────────────────

# Print a horizontal bar with color.
# Args: $1=value(%), $2=color, $3=label, $4=bar_width(default 40)
print_bar() {
    local val="${1:-0}" color="$2" label="$3" width="${4:-40}"
    # Clamp val to [0, 100] for display
    local filled empty
    filled="$(awk "BEGIN { v=${val}; if(v>100)v=100; if(v<0)v=0; f=int(v * ${width} / 100 + 0.5); if(f>${width}) f=${width}; print f }")"
    empty=$((width - filled))
    printf "  ${color}%-15s %5.1f%% ${RESET}" "$label" "$val"
    printf "${color}"
    printf '%*s' "$filled" '' | tr ' ' '#'
    printf "${DIM}"
    printf '%*s' "$empty" '' | tr ' ' '.'
    printf "${RESET}\n"
}

# Extract P-core section from uarch summary CSV.
# The CSV has two hierarchy trees: P-core and E-core.  P-core starts at
# the line with hierarchy level 1 and metric name "Performance-core (P-core)"
# (with empty value), E-core starts at "Efficient-core (E-core)".
# We extract only the P-core section so no metric lookup can accidentally
# return E-core data.
# Args: $1=full_csv
pcore_section() {
    echo "$1" | awk -F'\t' '
        $1==1 && $2=="Performance-core (P-core)" { p=1; next }
        $1==1 && $2=="Efficient-core (E-core)"   { p=0 }
        p { print }
    '
}

# Extract a metric from the P-core section of the uarch summary CSV.
# Args: $1=pcore_csv (output of pcore_section), $2=hierarchy_level, $3=metric_name
uarch_metric() {
    echo "$1" | awk -F'\t' -v lvl="$2" -v m="$3" '$1==lvl && $2==m { print $3; exit }'
}

# Extract a metric from the FULL summary CSV (for non-core-specific metrics like
# Elapsed Time at level 0, or CPI Rate / Average CPU Frequency at level 1).
# Args: $1=full_csv, $2=hierarchy_level, $3=metric_name
summary_metric() {
    echo "$1" | awk -F'\t' -v lvl="$2" -v m="$3" '$1==lvl && $2==m { print $3; exit }'
}

do_tma_report() {
    local uarch_dir="$1"
    [ -d "$uarch_dir" ] || { warn "No uarch result at $uarch_dir"; return; }

    local csv
    csv="$("$VTUNE" -report summary -r "$uarch_dir" -format csv 2>/dev/null \
        | grep -v '^vtune:')" || true

    if [ -z "$csv" ]; then
        warn "Empty uarch summary CSV"
        return
    fi

    # Extract P-core section only (ignore E-core hierarchy on hybrid CPUs).
    local pcore
    pcore="$(pcore_section "$csv")"

    if [ -z "$pcore" ]; then
        warn "No P-core data in uarch summary (non-hybrid CPU?)"
        # Fall back to full CSV for non-hybrid CPUs
        pcore="$csv"
    fi

    # TMA L1 (P-core hierarchy level 2 in summary CSV)
    local retiring frontend badspec backend
    retiring="$(uarch_metric "$pcore" 2 "Retiring")"
    frontend="$(uarch_metric "$pcore" 2 "Front-End Bound")"
    badspec="$(uarch_metric "$pcore" 2 "Bad Speculation")"
    backend="$(uarch_metric "$pcore" 2 "Back-End Bound")"

    # TMA L2 (hierarchy level 3)
    local mem_bound core_bound
    mem_bound="$(uarch_metric "$pcore" 3 "Memory Bound")"
    core_bound="$(uarch_metric "$pcore" 3 "Core Bound")"

    # TMA L3 memory (hierarchy level 4)
    # IMPORTANT: These are "% of parent category clockticks", NOT % of total.
    # On modern Intel CPUs, sub-categories OVERLAP (a stall can be L1+L2+L3
    # simultaneously as the miss propagates), so they can each be ~100%.
    # Absolute conversion: backend% * mem_bound% / 100 * child% / 100.
    local l1_raw l2_raw l3_raw dram_raw store_raw
    l1_raw="$(uarch_metric "$pcore" 4 "L1 Bound")"
    l2_raw="$(uarch_metric "$pcore" 4 "L2 Bound")"
    l3_raw="$(uarch_metric "$pcore" 4 "L3 Bound")"
    dram_raw="$(uarch_metric "$pcore" 4 "DRAM Bound")"
    store_raw="$(uarch_metric "$pcore" 4 "Store Bound")"

    # Detect if all L4 values are at 100% (common on hybrid CPUs where the
    # hierarchical breakdown loses resolution due to low sample counts).
    local all_saturated
    all_saturated="$(awk "BEGIN {
        l1=${l1_raw:-0}; l2=${l2_raw:-0}; l3=${l3_raw:-0}; st=${store_raw:-0}
        if (l1 >= 99.5 && l2 >= 99.5 && l3 >= 99.5 && st >= 99.5) print 1; else print 0
    }")"

    # Convert to absolute % of pipeline slots
    local mem_abs l1_abs l2_abs l3_abs dram_abs store_abs
    mem_abs="$(awk "BEGIN { printf \"%.1f\", ${backend:-0} * ${mem_bound:-0} / 100 }")"
    l1_abs="$(awk "BEGIN { printf \"%.1f\", ${mem_abs:-0} * ${l1_raw:-0} / 100 }")"
    l2_abs="$(awk "BEGIN { printf \"%.1f\", ${mem_abs:-0} * ${l2_raw:-0} / 100 }")"
    l3_abs="$(awk "BEGIN { printf \"%.1f\", ${mem_abs:-0} * ${l3_raw:-0} / 100 }")"
    dram_abs="$(awk "BEGIN { printf \"%.1f\", ${mem_abs:-0} * ${dram_raw:-0} / 100 }")"
    store_abs="$(awk "BEGIN { printf \"%.1f\", ${mem_abs:-0} * ${store_raw:-0} / 100 }")"

    # Core bound absolute
    local core_abs
    core_abs="$(awk "BEGIN { printf \"%.1f\", ${backend:-0} * ${core_bound:-0} / 100 }")"

    # Scalar metrics (from full CSV, not P-core section)
    local cpi elapsed freq
    cpi="$(summary_metric "$csv" 1 "CPI Rate")"
    elapsed="$(summary_metric "$csv" 0 "Elapsed Time")"
    elapsed="$(awk "BEGIN { printf \"%.3f\", ${elapsed:-0} }")"
    freq="$(summary_metric "$csv" 1 "Average CPU Frequency")"
    freq="$(awk "BEGIN { printf \"%.2f\", ${freq:-0}/1e9 }")"

    printf "\n${BOLD}=== TMA Summary (Top-Down Microarchitecture Analysis) ===${RESET}\n\n"
    printf "  ${DIM}Elapsed: ${elapsed}s  |  CPI: ${cpi}  |  Freq: ${freq} GHz${RESET}\n\n"

    # L1 pipeline slots (these sum to ~100%)
    printf "  ${BOLD}Pipeline Slots:${RESET}\n"
    print_bar "${retiring:-0}" "$GREEN"  "Retiring"
    print_bar "${frontend:-0}" "$CYAN"   "Front-End"
    print_bar "${badspec:-0}"  "$YELLOW" "Bad Spec"
    print_bar "${backend:-0}"  "$RED"    "Back-End"
    printf "\n"

    # L2 back-end breakdown (absolute % of pipeline slots)
    if awk "BEGIN { exit !(${backend:-0} > 10) }" 2>/dev/null; then
        printf "  ${BOLD}Back-End Breakdown (absolute %%):${RESET}\n"
        print_bar "${mem_abs}"  "$YELLOW" "  Memory"
        print_bar "${core_abs}" "$RED"    "  Core"
        printf "\n"
    fi

    # L3 memory breakdown
    if awk "BEGIN { exit !(${mem_abs:-0} > 2) }" 2>/dev/null; then
        if [ "$all_saturated" = "1" ]; then
            # When all sub-categories saturate at 100%, the hierarchical
            # breakdown is uninformative.  Show Memory/Core absolute split only
            # and note the saturation.
            printf "  ${BOLD}Memory Breakdown:${RESET}\n"
            printf "  ${DIM}  L4 hierarchy saturated (L1/L2/L3/Store all ~100%% of parent).${RESET}\n"
            printf "  ${DIM}  Sub-categories overlap -- a miss traverses L1->L2->L3->DRAM.${RESET}\n"
            printf "  ${DIM}  DRAM Bound: %s%% of Memory Bound clocks.${RESET}\n" "${dram_raw:-0}"
            printf "\n"
        else
            printf "  ${BOLD}Memory Breakdown (%% of Memory Bound clockticks):${RESET}\n"
            print_bar "${l1_raw:-0}"    "$YELLOW" "    L1 Bound"
            print_bar "${l2_raw:-0}"    "$YELLOW" "    L2 Bound"
            print_bar "${l3_raw:-0}"    "$YELLOW" "    L3 Bound"
            print_bar "${dram_raw:-0}"  "$YELLOW" "    DRAM"
            print_bar "${store_raw:-0}" "$YELLOW" "    Store"
            printf "  ${DIM}  (absolute: L1=%.1f%% L2=%.1f%% L3=%.1f%% DRAM=%.1f%% Store=%.1f%% of pipeline)${RESET}\n" \
                "$l1_abs" "$l2_abs" "$l3_abs" "$dram_abs" "$store_abs"
            printf "  ${DIM}  Note: sub-categories overlap (miss traverses L1->L2->L3->DRAM).${RESET}\n"
            printf "\n"
        fi
    fi
}

# ── Per-function TMA table from uarch-exploration ────────────────────

do_tma_function_table() {
    local uarch_dir="$1"
    [ -d "$uarch_dir" ] || { warn "No uarch result at $uarch_dir"; return 0; }

    printf "${BOLD}=== Function Hotspots (TMA per function) ===${RESET}\n\n"

    # Write CSV to temp file to avoid SIGPIPE on large echo pipelines.
    # Use a high limit to ensure hot-by-instructions functions (like
    # build_trace) are included even if VTune sorts by CPU Time.
    local tmpf
    tmpf="$(make_tmp)"
    vtune_to_file "$tmpf" -report hotspots -r "$uarch_dir" -format csv \
        -csv-delimiter tab -group-by function -limit 200

    if [ ! -s "$tmpf" ]; then
        warn "No function hotspot data"
        return 0
    fi

    local header
    header="$(head -1 "$tmpf")"

    # Find column indices by exact name match.
    local ct cc ci cr cf cb cbe cm ccore
    ct="$(col_idx "$header" "CPU Time")"
    cc="$(col_idx "$header" "CPI Rate")"
    ci="$(col_idx "$header" "Instructions Retired")"
    cr="$(col_idx "$header" "Performance-core (P-core):Retiring(%)")"
    cf="$(col_idx "$header" "Performance-core (P-core):Front-End Bound(%)")"
    cb="$(col_idx "$header" "Performance-core (P-core):Bad Speculation(%)")"
    cbe="$(col_idx "$header" "Performance-core (P-core):Back-End Bound(%)")"
    cm="$(col_idx "$header" "Performance-core (P-core):Back-End Bound:Memory Bound(%)")"
    ccore="$(col_idx "$header" "Performance-core (P-core):Back-End Bound:Core Bound(%)")"

    if [ -z "$ct" ] || [ -z "$cr" ]; then
        warn "Could not parse uarch column headers for function table"
        return 0
    fi

    # Compute total CPU time and total instructions for percentage columns.
    local total_time total_instr
    total_time=$(tail -n +2 "$tmpf" | awk -F'\t' -v c="$ct" '{ s += $c } END { printf "%.6f", s }') || true
    total_instr=$(tail -n +2 "$tmpf" | awk -F'\t' -v c="${ci:-5}" '{ s += ($c + 0) } END { printf "%.0f", s }') || true

    # Sort by CPU time, then by instructions retired for functions with
    # zero CPU time (common with HW sampling on pinned single-core workloads
    # where samples land on the outer frame).
    local tmps
    tmps="$(make_tmp)"
    tail -n +2 "$tmpf" | sort -t$'\t' -k"$ct" -rg > "$tmps" 2>/dev/null || true

    # Take top 15 by CPU time, then append any functions with significant
    # instruction counts (>1% of total) that were not in the top 15.
    local tmps2
    tmps2="$(make_tmp)"
    {
        head -15 "$tmps"
        tail -n +16 "$tmps" | awk -F'\t' \
            -v ci_="${ci:-5}" -v ti="$total_instr" \
            '{ instr = $(ci_) + 0; if (ti > 0 && instr / ti > 0.01) print }'
    } > "$tmps2"

    printf "  ${BOLD}%-48s %8s %12s %6s %6s %6s %6s %6s %6s %6s${RESET}\n" \
        "Function" "Time(s)" "Instructions" "CPI" "Ret%" "FrE%" "BkE%" "Mem%" "Cor%" "Tot%"
    printf "  %-48s %8s %12s %6s %6s %6s %6s %6s %6s %6s\n" \
        "$(printf -- '-%.0s' {1..48})" "--------" "------------" "------" "------" "------" "------" "------" "------" "------"

    awk -F'\t' \
        -v ct_="$ct" -v cc_="${cc:-0}" -v ci_="${ci:-5}" -v cr_="${cr:-0}" \
        -v cf_="${cf:-0}" -v cb_="${cb:-0}" -v cbe_="${cbe:-0}" \
        -v cm_="${cm:-0}" -v ccore_="${ccore:-0}" -v tot="$total_time" -v ti="$total_instr" \
        -v C_RED="$RED" -v C_YEL="$YELLOW" -v C_GRN="$GREEN" -v C_DIM="$DIM" -v C_RST="$RESET" '
    !seen[$1]++ {
        fn = $1; tm = $(ct_) + 0; cpi = (cc_ > 0) ? $(cc_) + 0 : 0
        instr = (ci_ > 0) ? $(ci_) + 0 : 0
        ret = (cr_ > 0) ? $(cr_) + 0 : 0
        fe  = (cf_ > 0) ? $(cf_) + 0 : 0
        bs  = (cb_ > 0) ? $(cb_) + 0 : 0
        be  = (cbe_ > 0) ? $(cbe_) + 0 : 0
        mem = (cm_ > 0) ? $(cm_) + 0 : 0
        cor = (ccore_ > 0) ? $(ccore_) + 0 : 0
        if (tm < 0.0001 && instr < 1000000) next
        pct = (tot > 0) ? tm / tot * 100 : 0
        instr_pct = (ti > 0) ? instr / ti * 100 : 0
        if (length(fn) > 48) fn = substr(fn, 1, 45) "..."

        # Format instruction count with B/M suffix
        instr_s = ""
        if (instr >= 1e9) instr_s = sprintf("%.1fB", instr / 1e9)
        else if (instr >= 1e6) instr_s = sprintf("%.1fM", instr / 1e6)
        else if (instr >= 1e3) instr_s = sprintf("%.1fK", instr / 1e3)
        else instr_s = sprintf("%.0f", instr)

        clr = ""
        if (be > 50) clr = C_RED; else if (mem > 30) clr = C_YEL; else if (ret > 60) clr = C_GRN
        # Dim rows with zero CPU time (instruction-only attribution)
        if (tm < 0.0001) clr = C_DIM

        printf "  %s%-48s %7.3fs %12s %6.3f %5.1f%% %5.1f%% %5.1f%% %5.1f%% %5.1f%% %4.1f%%%s\n", \
            clr, fn, tm, instr_s, cpi, ret, fe, be, mem, cor, pct, C_RST
    }' "$tmps2"

    printf "\n"
}

# ── Actionable diagnostics from uarch data ───────────────────────────

do_diagnostics() {
    local uarch_dir="$1"
    [ -d "$uarch_dir" ] || return 0

    local csv
    csv="$("$VTUNE" -report summary -r "$uarch_dir" -format csv 2>/dev/null \
        | grep -v '^vtune:')" || true

    if [ -z "$csv" ]; then return 0; fi

    # Extract P-core section only.
    local pcore
    pcore="$(pcore_section "$csv")"
    [ -n "$pcore" ] || pcore="$csv"

    printf "${BOLD}=== Actionable Diagnostics ===${RESET}\n\n"

    # Compute parent chain for absolute % conversion.
    # Level-5 metrics under L1 Bound are "% of L1 Bound clockticks".
    # Absolute = BackEnd * MemBound * L1Bound / 10000 * metric / 100
    # Level-5 metrics under Store Bound = BackEnd * MemBound * StoreBound / 10000 * metric / 100
    # Level-4 metrics under Core Bound = BackEnd * CoreBound / 100 * metric / 100
    local backend mem_bound core_bound l1_raw store_raw
    backend="$(uarch_metric "$pcore" 2 "Back-End Bound")"
    mem_bound="$(uarch_metric "$pcore" 3 "Memory Bound")"
    core_bound="$(uarch_metric "$pcore" 3 "Core Bound")"
    l1_raw="$(uarch_metric "$pcore" 4 "L1 Bound")"
    store_raw="$(uarch_metric "$pcore" 4 "Store Bound")"

    # Absolute base for L1 sub-metrics
    local l1_abs_base store_abs_base core_abs_base
    l1_abs_base="$(awk "BEGIN { printf \"%.4f\", ${backend:-0} * ${mem_bound:-0} * ${l1_raw:-0} / 1000000 }")"
    store_abs_base="$(awk "BEGIN { printf \"%.4f\", ${backend:-0} * ${mem_bound:-0} * ${store_raw:-0} / 1000000 }")"
    core_abs_base="$(awk "BEGIN { printf \"%.4f\", ${backend:-0} * ${core_bound:-0} / 10000 }")"

    local count=0

    # Store Forwarding Blocked (level 5, parent = L1 Bound)
    local stfwd stfwd_abs
    stfwd="$(uarch_metric "$pcore" 5 "Loads Blocked by Store Forwarding")"
    stfwd_abs="$(awk "BEGIN { printf \"%.1f\", ${l1_abs_base:-0} * ${stfwd:-0} }")"
    if awk "BEGIN { exit !(${stfwd_abs:-0} > 1) }" 2>/dev/null; then
        printf "  ${RED}[HIGH]${RESET} Store Forwarding Blocked: ~%s%% of pipeline  (raw: %s%% of L1 Bound)\n" \
            "$stfwd_abs" "${stfwd:-0}"
        printf "         ${DIM}Write-then-read with size mismatch. Fix: align store/load widths.${RESET}\n"
        count=$((count + 1))
    fi

    # L1 Latency Dependency (level 5, parent = L1 Bound)
    local l1_dep l1_dep_abs
    l1_dep="$(uarch_metric "$pcore" 5 "L1 Latency Dependency")"
    l1_dep_abs="$(awk "BEGIN { printf \"%.1f\", ${l1_abs_base:-0} * ${l1_dep:-0} }")"
    if awk "BEGIN { exit !(${l1_dep_abs:-0} > 2) }" 2>/dev/null; then
        printf "  ${RED}[HIGH]${RESET} L1 Latency Dependency: ~%s%% of pipeline  (raw: %s%% of L1 Bound)\n" \
            "$l1_dep_abs" "${l1_dep:-0}"
        printf "         ${DIM}Long dependency chains. Fix: break chains, use independent accumulators.${RESET}\n"
        count=$((count + 1))
    fi

    # Store Latency (level 5, parent = Store Bound)
    local store_lat store_lat_abs
    store_lat="$(uarch_metric "$pcore" 5 "Store Latency")"
    store_lat_abs="$(awk "BEGIN { printf \"%.1f\", ${store_abs_base:-0} * ${store_lat:-0} }")"
    if awk "BEGIN { exit !(${store_lat_abs:-0} > 2) }" 2>/dev/null; then
        printf "  ${YELLOW}[MED]${RESET}  Store Latency: ~%s%% of pipeline  (raw: %s%% of Store Bound)\n" \
            "$store_lat_abs" "${store_lat:-0}"
        printf "         ${DIM}Stores still in store buffer. Fix: reduce store pressure, batch writes.${RESET}\n"
        count=$((count + 1))
    fi

    # Branch Misprediction (level 3 under Bad Speculation -- already % of pipeline)
    local badspec br_misp br_misp_abs
    badspec="$(uarch_metric "$pcore" 2 "Bad Speculation")"
    br_misp="$(uarch_metric "$pcore" 3 "Branch Mispredict")"
    br_misp_abs="$(awk "BEGIN { printf \"%.1f\", ${badspec:-0} * ${br_misp:-0} / 100 }")"
    if awk "BEGIN { exit !(${br_misp_abs:-0} > 2) }" 2>/dev/null; then
        printf "  ${YELLOW}[MED]${RESET}  Branch Misprediction: ~%s%% of pipeline  (raw: %s%% of Bad Spec)\n" \
            "$br_misp_abs" "${br_misp:-0}"
        printf "         ${DIM}Fix: branchless patterns (cmov, bitwise), sort input data.${RESET}\n"
        count=$((count + 1))
    fi

    # Port Utilization (level 4 under Core Bound)
    local port_util port_util_abs
    port_util="$(uarch_metric "$pcore" 4 "Port Utilization")"
    port_util_abs="$(awk "BEGIN { printf \"%.1f\", ${core_abs_base:-0} * ${port_util:-0} }")"
    if awk "BEGIN { exit !(${port_util_abs:-0} > 5) }" 2>/dev/null; then
        printf "  ${CYAN}[INFO]${RESET} Port Utilization: ~%s%% of pipeline  (raw: %s%% of Core Bound)\n" \
            "$port_util_abs" "${port_util:-0}"
        printf "         ${DIM}Potential: vectorize loops, reduce dependency chains.${RESET}\n"
        count=$((count + 1))
    fi

    # DSB Coverage (info metric, not hierarchical -- use from P-core section)
    local dsb_cov
    dsb_cov="$(uarch_metric "$pcore" 4 "(Info) DSB Coverage")"
    if awk "BEGIN { exit !(${dsb_cov:-100} < 70) }" 2>/dev/null; then
        printf "  ${CYAN}[INFO]${RESET} DSB Coverage: %s%% (low = large code footprint)\n" "${dsb_cov:-?}"
        printf "         ${DIM}Potential: reduce template instantiations, use __attribute__((cold)).${RESET}\n"
        count=$((count + 1))
    fi

    # Split Stores (level 5, parent = Store Bound)
    local split_st split_st_abs
    split_st="$(uarch_metric "$pcore" 5 "Split Stores")"
    split_st_abs="$(awk "BEGIN { printf \"%.2f\", ${store_abs_base:-0} * ${split_st:-0} }")"
    if awk "BEGIN { exit !(${split_st_abs:-0} > 0.1) }" 2>/dev/null; then
        printf "  ${YELLOW}[MED]${RESET}  Split Stores: ~%s%% of pipeline  (raw: %s%% of Store Bound)\n" \
            "$split_st_abs" "${split_st:-0}"
        printf "         ${DIM}Stores crossing cacheline boundary. Fix: align to 64B.${RESET}\n"
        count=$((count + 1))
    fi

    if [ "$count" -eq 0 ]; then
        printf "  ${GREEN}No significant microarchitectural issues detected.${RESET}\n"
    fi
    printf "\n"
}

# ── VTune reports ─────────────────────────────────────────────────────

# Source-line hotspots for the target function (with phase annotations).
do_vtune_source_report() {
    local vtune_dir="$1"
    [ -d "$vtune_dir" ] || { warn "VTune result not found: $vtune_dir"; return 0; }

    info "Source-line hotspots for $FUNC"
    printf "\n"

    local tmpf
    tmpf="$(make_tmp)"
    vtune_to_file "$tmpf" -report hotspots -r "$vtune_dir" -format csv \
        -csv-delimiter tab -group-by source-line -filter "function=$FUNC"

    if [ ! -s "$tmpf" ]; then
        printf "  ${YELLOW}No source-line data available.${RESET}\n\n"
        return 0
    fi

    local header
    header="$(head -1 "$tmpf")"

    # Find columns by exact name
    local ct ci cu
    ct="$(col_idx "$header" "CPU Time")"
    ci="$(col_idx "$header" "Instructions Retired")"
    cu="$(col_idx "$header" "Microarchitecture Usage(%)")"

    local data_lines
    data_lines="$(tail -n +2 "$tmpf" | awk -F'\t' -v c="${ct:-3}" '$(c)+0 > 0' | wc -l)"
    if [ "$data_lines" -eq 0 ]; then
        printf "  ${YELLOW}No source-line data. Rebuild with -g for debug info.${RESET}\n\n"
        return 0
    fi

    printf "  ${BOLD}%-26s %6s  %8s  %12s  %6s  %-14s${RESET}\n" \
        "Source" "Line" "CPU(ms)" "Instructions" "uArch" "Phase"

    # Sort by CPU time descending, take top 30
    local tmps
    tmps="$(make_tmp)"
    tail -n +2 "$tmpf" | sort -t$'\t' -k"${ct:-3}" -rg > "$tmps" 2>/dev/null || true

    head -30 "$tmps" | awk -F'\t' \
        -v ct_="${ct:-3}" -v ci_="${ci:-7}" -v cu_="${cu:-8}" \
        -v C_RED="$RED" -v C_YEL="$YELLOW" -v C_RST="$RESET" '{
        f = $1; ln = $2 + 0; tm = $(ct_) + 0
        if (tm <= 0) next
        instr = (ci_ > 0) ? $(ci_) : ""
        uarch = (cu_ > 0) ? $(cu_) : ""

        # Strip path
        gsub(/.*\//, "", f)

        # Phase annotation
        phase = "other"
        if (ln >= 414 && ln <= 439) phase = "P0-scan"
        else if (ln >= 440 && ln <= 511) phase = "P1-alloc"
        else if (ln >= 512 && ln <= 677) phase = "P2-copy+hash"
        else if (ln >= 678 && ln <= 900) phase = "P3-outputs"

        # Color hot lines
        clr = ""
        if (tm > 0.01) clr = C_RED
        else if (tm > 0.005) clr = C_YEL

        printf "  %s%-26s %6d  %8.2f  %12s  %5s%%  %-14s%s\n", \
            clr, f, ln, tm*1000, instr, (uarch == "" ? "--" : uarch), phase, C_RST
    }'

    printf "\n"
}

# Function-level hotspots for the entire module.
do_vtune_func_report() {
    local vtune_dir="$1"
    [ -d "$vtune_dir" ] || { warn "VTune result not found: $vtune_dir"; return 0; }

    info "Function hotspots (top 15)"
    printf "\n"

    local tmpf
    tmpf="$(make_tmp)"
    vtune_to_file "$tmpf" -report hotspots -r "$vtune_dir" -format csv \
        -csv-delimiter tab -group-by function -filter "module=$TARGET"

    if [ ! -s "$tmpf" ]; then
        printf "  ${YELLOW}No function data available.${RESET}\n\n"
        return 0
    fi

    local tmps
    tmps="$(make_tmp)"
    tail -n +2 "$tmpf" | sort -t$'\t' -k2 -rg > "$tmps" 2>/dev/null || true

    head -15 "$tmps" \
    | awk -F'\t' 'BEGIN { printf "  %-55s %8s  %12s  %6s\n", "Function", "CPU(ms)", "Instructions", "CPI" }
{
    fn = $1; cpu_s = $2 + 0; instr = $5; cpi = $9
    if (cpu_s > 0) printf "  %-55s %8.2f  %12s  %s\n", fn, cpu_s*1000, instr, cpi
}'
}

# ── TMA per source-line (from uarch-exploration) ─────────────────────

do_tma_source_report() {
    local uarch_dir="$1"
    [ -d "$uarch_dir" ] || { warn "No uarch result for per-line TMA"; return 0; }

    info "TMA per source-line for $FUNC (uarch-exploration)"
    printf "\n"

    # Write to temp file -- the uarch CSV is ~500KB with 160+ columns.
    local tmpf
    tmpf="$(make_tmp)"
    vtune_to_file "$tmpf" -report hotspots -r "$uarch_dir" -format csv \
        -csv-delimiter tab -group-by source-line -filter "function=$FUNC" -q

    if [ ! -s "$tmpf" ]; then
        printf "  ${YELLOW}No TMA source-line data available.${RESET}\n\n"
        return 0
    fi

    local header
    header="$(head -1 "$tmpf")"

    # Find column indices using EXACT full column names (no substring ambiguity).
    local ct cc cr cf cb cbe cm ccore
    ct="$(col_idx "$header" "CPU Time")"
    cc="$(col_idx "$header" "CPI Rate")"
    cr="$(col_idx "$header" "Performance-core (P-core):Retiring(%)")"
    cf="$(col_idx "$header" "Performance-core (P-core):Front-End Bound(%)")"
    cb="$(col_idx "$header" "Performance-core (P-core):Bad Speculation(%)")"
    cbe="$(col_idx "$header" "Performance-core (P-core):Back-End Bound(%)")"
    cm="$(col_idx "$header" "Performance-core (P-core):Back-End Bound:Memory Bound(%)")"
    ccore="$(col_idx "$header" "Performance-core (P-core):Back-End Bound:Core Bound(%)")"

    if [ -z "$ct" ]; then
        printf "  ${YELLOW}No TMA source-line data available (missing CPU Time column).${RESET}\n\n"
        return 0
    fi

    # Check for data rows with non-zero CPU time
    local data_lines
    data_lines="$(tail -n +2 "$tmpf" | awk -F'\t' -v c="${ct}" '$(c)+0 > 0' | wc -l)"
    if [ "$data_lines" -eq 0 ]; then
        printf "  ${YELLOW}No source lines with CPU time > 0.${RESET}\n\n"
        return 0
    fi

    printf "  ${BOLD}%-22s %5s %7s  %5s %5s %5s %5s %5s %5s  %-14s${RESET}\n" \
        "Source" "Line" "CPU(ms)" "CPI" "Ret%" "FrE%" "BkE%" "Mem%" "Cor%" "Phase"

    # Sort by CPU time descending, take top 25
    local tmps
    tmps="$(make_tmp)"
    tail -n +2 "$tmpf" | sort -t$'\t' -k"$ct" -rg > "$tmps" 2>/dev/null || true

    head -25 "$tmps" | awk -F'\t' \
        -v ct_="$ct" -v cc_="${cc:-0}" -v cr_="${cr:-0}" -v cf_="${cf:-0}" \
        -v cbe_="${cbe:-0}" -v cm_="${cm:-0}" -v ccore_="${ccore:-0}" \
        -v C_RED="$RED" -v C_YEL="$YELLOW" -v C_GRN="$GREEN" -v C_RST="$RESET" \
    '{
        f = $1; ln = $2; tm = $(ct_) + 0
        if (tm <= 0) next
        cpi = (cc_ > 0) ? $(cc_) + 0 : 0
        ret = (cr_ > 0) ? $(cr_) + 0 : 0
        fe  = (cf_ > 0) ? $(cf_) + 0 : 0
        be  = (cbe_ > 0) ? $(cbe_) + 0 : 0
        mem = (cm_ > 0) ? $(cm_) + 0 : 0
        cor = (ccore_ > 0) ? $(ccore_) + 0 : 0

        # Strip path
        gsub(/.*\//, "", f)

        # Phase annotation
        phase = "other"
        if (ln >= 414 && ln <= 439) phase = "P0-scan"
        else if (ln >= 440 && ln <= 511) phase = "P1-alloc"
        else if (ln >= 512 && ln <= 677) phase = "P2-copy+hash"
        else if (ln >= 678 && ln <= 900) phase = "P3-outputs"

        # Color by back-end bound severity
        clr = ""
        if (be > 50) clr = C_RED
        else if (be > 30) clr = C_YEL
        else if (ret > 60) clr = C_GRN

        printf "  %s%-22s %5d %6.2fms %5.3f %4.1f%% %4.1f%% %4.1f%% %4.1f%% %4.1f%%  %-14s%s\n", \
            clr, f, ln, tm*1000, cpi, ret, fe, be, mem, cor, phase, C_RST
    }'

    printf "\n"
}

# ── Hot source lines across ALL functions ────────────────────────────

do_hotlines() {
    local uarch_dir="$1"
    local nlines="${2:-30}"
    [ -d "$uarch_dir" ] || { warn "No uarch result at $uarch_dir"; return 0; }

    info "Top $nlines hottest source lines (all functions)"
    printf "\n"

    local tmpf
    tmpf="$(make_tmp)"
    vtune_to_file "$tmpf" -report hotspots -r "$uarch_dir" -format csv \
        -csv-delimiter tab -group-by source-line -filter "module=$TARGET" -q

    if [ ! -s "$tmpf" ]; then
        printf "  ${YELLOW}No source-line data available.${RESET}\n\n"
        return 0
    fi

    local header
    header="$(head -1 "$tmpf")"

    local ct cr cbe cm
    ct="$(col_idx "$header" "CPU Time")"
    cr="$(col_idx "$header" "Performance-core (P-core):Retiring(%)")"
    cbe="$(col_idx "$header" "Performance-core (P-core):Back-End Bound(%)")"
    cm="$(col_idx "$header" "Performance-core (P-core):Back-End Bound:Memory Bound(%)")"

    if [ -z "$ct" ]; then
        printf "  ${YELLOW}No CPU Time column found.${RESET}\n\n"
        return 0
    fi

    # Compute total CPU time
    local total_time
    total_time="$(tail -n +2 "$tmpf" | awk -F'\t' -v c="$ct" '{ s += $(c) } END { printf "%.6f", s }')" || true

    printf "  ${BOLD}%-26s %5s %8s %6s %5s %5s %5s${RESET}\n" \
        "Source" "Line" "CPU(ms)" "Tot%" "Ret%" "BkE%" "Mem%"
    printf "  %-26s %5s %8s %6s %5s %5s %5s\n" \
        "--------------------------" "-----" "--------" "------" "-----" "-----" "-----"

    local tmps
    tmps="$(make_tmp)"
    tail -n +2 "$tmpf" | sort -t$'\t' -k"$ct" -rg > "$tmps" 2>/dev/null || true

    head -"$nlines" "$tmps" | awk -F'\t' \
        -v ct_="$ct" -v cr_="${cr:-0}" -v cbe_="${cbe:-0}" -v cm_="${cm:-0}" \
        -v tot="$total_time" \
        -v C_RED="$RED" -v C_YEL="$YELLOW" -v C_GRN="$GREEN" -v C_RST="$RESET" '{
        f = $1; ln = $2; tm = $(ct_) + 0
        if (tm <= 0) next
        ret = (cr_ > 0) ? $(cr_) + 0 : 0
        be  = (cbe_ > 0) ? $(cbe_) + 0 : 0
        mem = (cm_ > 0) ? $(cm_) + 0 : 0
        pct = (tot > 0) ? tm / tot * 100 : 0

        gsub(/.*\//, "", f)

        clr = ""
        if (be > 50) clr = C_RED
        else if (be > 30) clr = C_YEL
        else if (ret > 60) clr = C_GRN

        printf "  %s%-26s %5d %7.2fms %5.1f%% %4.1f%% %4.1f%% %4.1f%%%s\n", \
            clr, f, ln, tm*1000, pct, ret, be, mem, C_RST
    }'

    printf "\n"
}

# ── A/B Comparison ────────────────────────────────────────────────────

do_compare() {
    local baseline_tag="$1"
    local current_tag="$2"

    local baseline_dir="$PROF_DIR/$baseline_tag"
    local current_dir="$PROF_DIR/$current_tag"

    [ -f "$baseline_dir/stats.txt" ] || die "No baseline found at $baseline_dir/stats.txt"
    [ -f "$current_dir/stats.txt" ] || die "No current results at $current_dir/stats.txt"

    # Source both stats files (variables: best_min, avg_min, avg_med, median_med).
    local b_best_min b_avg_min b_avg_med b_median_med
    local best_min avg_min avg_med median_med  # temps for eval
    eval "$(grep -E '^(best_min|avg_min)=' "$baseline_dir/stats.txt")"
    b_best_min="$best_min"; b_avg_min="$avg_min"
    eval "$(grep -E '^(best_med|avg_med|median_med)=' "$baseline_dir/stats.txt")"
    b_avg_med="$avg_med"; b_median_med="$median_med"

    eval "$(grep -E '^(best_min|avg_min)=' "$current_dir/stats.txt")"
    local c_best_min="$best_min" c_avg_min="$avg_min"
    eval "$(grep -E '^(best_med|avg_med|median_med)=' "$current_dir/stats.txt")"
    local c_avg_med="$avg_med" c_median_med="$median_med"

    printf "\n${BOLD}=== A/B Comparison ===${RESET}\n"
    printf "  Baseline: %-12s  Current: %-12s\n\n" "$baseline_tag" "$current_tag"

    compare_metric() {
        local name="$1" base="$2" curr="$3"
        local delta
        delta="$(awk "BEGIN { d = ($curr - $base) / $base * 100; printf \"%.2f\", d }")"
        local color="$GREEN"
        local sign=""
        if awk "BEGIN { exit ($delta >= 0) ? 0 : 1 }"; then
            color="$RED"
            sign="+"
        fi
        printf "  %-20s  %10.1f  %10.1f  ${color}%s%.2f%%${RESET}\n" \
            "$name" "$base" "$curr" "$sign" "$delta"
    }

    printf "  %-20s  %10s  %10s  %s\n" "Metric" "Baseline" "Current" "Delta"
    printf "  %-20s  %10s  %10s  %s\n" "--------------------" "----------" "----------" "------"
    compare_metric "best_min (ns/op)" "$b_best_min" "$c_best_min"
    compare_metric "avg_min (ns/op)" "$b_avg_min" "$c_avg_min"
    compare_metric "avg_med (ns/op)" "$b_avg_med" "$c_avg_med"
    compare_metric "median_med (ns/op)" "$b_median_med" "$c_median_med"

    printf "\n"

    # If both have VTune results, compare source-line hotspots.
    if [ -d "$baseline_dir/vtune" ] && [ -d "$current_dir/vtune" ]; then
        info "VTune source-line comparison (top changes)"
        printf "\n"

        local b_tmp c_tmp
        b_tmp="$(make_tmp)"
        c_tmp="$(make_tmp)"
        vtune_to_file "$b_tmp" -report hotspots -r "$baseline_dir/vtune" \
            -format csv -csv-delimiter tab -group-by source-line \
            -filter "function=$FUNC"
        vtune_to_file "$c_tmp" -report hotspots -r "$current_dir/vtune" \
            -format csv -csv-delimiter tab -group-by source-line \
            -filter "function=$FUNC"

        {
            tail -n +2 "$b_tmp" | awk -F'\t' '{ if ($3+0 > 0) print "B", $1, $2, $3 }'
            tail -n +2 "$c_tmp" | awk -F'\t' '{ if ($3+0 > 0) print "C", $1, $2, $3 }'
        } | awk '
{
    key = $2 ":" $3
    if ($1 == "B") base[key] = $4 + 0
    if ($1 == "C") curr[key] = $4 + 0
}
END {
    for (k in base) {
        if (k in curr) {
            d = (curr[k] - base[k]) * 1000
            gsub(/.*\//, "", k)
            printf "  %-30s  %+8.2f ms  (%.2f -> %.2f)\n", k, d, base[k]*1000, curr[k]*1000
        }
    }
}' | sort -k2 -rg | head -15 || true
    fi
}

# ── Find latest baseline ─────────────────────────────────────────────

find_baseline() {
    local baseline_file="$PROF_DIR/.baseline"
    if [ -f "$baseline_file" ]; then
        cat "$baseline_file"
    else
        ls -1t "$PROF_DIR"/*/stats.txt 2>/dev/null \
            | head -1 \
            | awk -F/ '{ print $(NF-1) }' || true
    fi
}

# ── Subcommands ───────────────────────────────────────────────────────

cmd_bench() {
    do_build
    print_func_info
    printf "\n"

    local tag
    tag="$(resolve_tag "${1:-}")"
    local outdir="$PROF_DIR/$tag"
    mkdir -p "$outdir"

    run_benchmark_suite "$outdir"
    save_meta "$outdir"
}

cmd_profile() {
    do_build
    print_func_info
    printf "\n"

    local tag
    tag="$(resolve_tag "${1:-}")"
    local outdir="$PROF_DIR/$tag"
    mkdir -p "$outdir"

    run_benchmark_suite "$outdir"
    do_vtune_collect "$outdir"
    do_vtune_func_report "$outdir/vtune"
    do_vtune_source_report "$outdir/vtune"
    save_meta "$outdir"

    info "Results saved to $outdir"
}

cmd_baseline() {
    do_build
    print_func_info
    printf "\n"

    local tag
    tag="$(resolve_tag "${1:-}")"
    local outdir="$PROF_DIR/$tag"
    mkdir -p "$outdir"

    run_benchmark_suite "$outdir"
    do_vtune_collect "$outdir"
    do_vtune_func_report "$outdir/vtune"
    do_vtune_source_report "$outdir/vtune"
    save_meta "$outdir"

    echo "$tag" > "$PROF_DIR/.baseline"

    printf "\n${GREEN}${BOLD}Baseline saved: %s${RESET}\n" "$tag"
    info "Results: $outdir"
}

cmd_compare() {
    local baseline_tag
    baseline_tag="$(find_baseline)"
    [ -n "$baseline_tag" ] || die "No baseline found. Run: prof.sh baseline"

    do_build
    print_func_info
    printf "\n"

    local tag
    tag="$(resolve_tag "${1:-}")"
    if [ "$tag" = "$baseline_tag" ]; then
        tag="${tag}-$(date +%H%M%S)"
        warn "Same tag as baseline, using: $tag"
    fi
    local outdir="$PROF_DIR/$tag"
    mkdir -p "$outdir"

    run_benchmark_suite "$outdir"
    do_vtune_collect "$outdir"
    save_meta "$outdir"

    do_compare "$baseline_tag" "$tag"
    do_vtune_source_report "$outdir/vtune"
}

cmd_show() {
    local tag
    tag="$(resolve_tag "${1:-}")"
    local outdir="$PROF_DIR/$tag"

    if [ -d "$outdir/vtune" ]; then
        do_vtune_func_report "$outdir/vtune"
        do_vtune_source_report "$outdir/vtune"
    elif [ -d "$outdir/uarch" ]; then
        do_tma_function_table "$outdir/uarch"
        do_tma_source_report "$outdir/uarch"
    else
        die "No VTune result found at $outdir"
    fi
}

cmd_asm() {
    local tag
    tag="$(resolve_tag "${1:-}")"
    local vtune_dir="$PROF_DIR/$tag/vtune"
    [ -d "$vtune_dir" ] || die "VTune result not found: $vtune_dir"

    info "Assembly hotspots for $FUNC"
    printf "\n"

    "$VTUNE" \
        -report hotspots \
        -r "$vtune_dir" \
        -format text \
        -group-by address \
        -filter "function=$FUNC" \
        2>/dev/null \
    | head -80 || true
}

cmd_tma() {
    do_build
    print_func_info
    printf "\n"

    local tag
    tag="$(resolve_tag "${1:-}")"
    local outdir="$PROF_DIR/$tag"
    mkdir -p "$outdir"

    # Timing runs first (no profiler overhead)
    run_benchmark_suite "$outdir"

    # Both collections: hotspots (source-line resolution) + uarch (TMA)
    do_vtune_collect "$outdir"
    do_vtune_uarch_collect "$outdir"
    save_meta "$outdir"

    # Full report
    printf "\n"
    do_tma_report "$outdir/uarch"
    do_tma_function_table "$outdir/uarch"
    do_tma_source_report "$outdir/uarch"
    do_vtune_source_report "$outdir/vtune"
    do_diagnostics "$outdir/uarch"

    info "Full results: $outdir"
    printf "  ${DIM}VTune GUI:  $VTUNE -gui $outdir/uarch${RESET}\n"
    printf "  ${DIM}Show later: ./prof.sh tma-show $tag${RESET}\n"
}

cmd_tma_show() {
    local tag
    tag="$(resolve_tag "${1:-}")"
    local outdir="$PROF_DIR/$tag"

    if [ -d "$outdir/uarch" ]; then
        do_tma_report "$outdir/uarch"
        do_tma_function_table "$outdir/uarch"
        do_tma_source_report "$outdir/uarch"
    fi
    if [ -d "$outdir/vtune" ]; then
        do_vtune_source_report "$outdir/vtune"
    fi
    if [ -d "$outdir/uarch" ]; then
        do_diagnostics "$outdir/uarch"
    fi

    if [ ! -d "$outdir/uarch" ] && [ ! -d "$outdir/vtune" ]; then
        die "No profiling data found at $outdir"
    fi
}

cmd_hotlines() {
    local tag
    tag="$(resolve_tag "${1:-}")"
    local outdir="$PROF_DIR/$tag"

    if [ -d "$outdir/uarch" ]; then
        do_hotlines "$outdir/uarch" 40
    elif [ -d "$outdir/vtune" ]; then
        warn "Only hotspots data available (no uarch); use 'tma' for full TMA columns"
        do_hotlines "$outdir/vtune" 40
    else
        die "No profiling data found at $outdir"
    fi
}

cmd_clean() {
    if [ -d "$PROF_DIR" ]; then
        local size
        size="$(du -sh "$PROF_DIR" 2>/dev/null | cut -f1)"
        info "Removing $PROF_DIR ($size)"
        rm -rf "$PROF_DIR"
    else
        info "Nothing to clean"
    fi
}

cmd_list() {
    if [ ! -d "$PROF_DIR" ]; then
        info "No profiling data found"
        return
    fi

    local baseline_tag
    baseline_tag="$(find_baseline)"

    printf "${BOLD}%-16s  %-20s  %-10s  %12s  %12s  %6s  %6s${RESET}\n" \
        "Tag" "Date" "Git" "Best Min" "Avg Med" "VTune" "uArch"

    for d in "$PROF_DIR"/*/; do
        [ -f "$d/stats.txt" ] || continue
        local tag
        tag="$(basename "$d")"
        local date_str git_hash has_vtune has_uarch marker
        local best_min="?" avg_med="?"

        date_str="$(grep '^date=' "$d/meta.txt" 2>/dev/null | cut -d= -f2 | cut -dT -f1,2 | tr T ' ' || echo '?')"
        git_hash="$(grep '^git_hash=' "$d/meta.txt" 2>/dev/null | cut -d= -f2 || echo '?')"

        eval "$(grep -E '^best_min=' "$d/stats.txt" 2>/dev/null)" 2>/dev/null || true
        eval "$(grep -E '^avg_med=' "$d/stats.txt" 2>/dev/null)" 2>/dev/null || true

        has_vtune="no"
        [ -d "$d/vtune" ] && has_vtune="yes"
        has_uarch="no"
        [ -d "$d/uarch" ] && has_uarch="yes"

        marker=""
        [ "$tag" = "$baseline_tag" ] && marker=" *"

        printf "%-16s  %-20s  %-10s  %12s  %12s  %6s  %6s%s\n" \
            "$tag" "$date_str" "$git_hash" "$best_min" "$avg_med" "$has_vtune" "$has_uarch" "$marker"
    done

    printf "\n  * = baseline\n"
}

# ── Main ──────────────────────────────────────────────────────────────

usage() {
    cat <<'EOF'
Usage: prof.sh <command> [tag] [func]

Commands:
  bench    [tag] [func]   Build and benchmark only (no VTune)
  baseline [tag] [func]   Build, benchmark (5 runs), VTune profile, save as baseline
  compare  [tag] [func]   Build, benchmark, VTune profile, compare against baseline
  profile  [tag] [func]   Build, benchmark, VTune profile (no baseline save)
  tma      [tag] [func]   Full analysis: bench + hotspots + uarch + TMA report
  tma-show [tag] [func]   Show TMA report from existing results (no collection)
  show     [tag] [func]   Show source-line hotspots from existing VTune result
  asm      [tag] [func]   Show assembly hotspots from existing VTune result
  hotlines [tag]          Show top-N hottest source lines across ALL functions
  list                    List all saved profiling runs
  clean                   Remove all profiling data

Arguments:
  tag    Result tag (defaults to git short hash). Also set via TAG env var.
  func   Function to focus on. Shorthand names are expanded:
           build_trace       -> crucible::BackgroundThread::build_trace
           compute_memory    -> crucible::BackgroundThread::compute_memory_plan
           build_csr         -> crucible::build_csr
           make_region       -> crucible::make_region
           content_hash      -> crucible::compute_content_hash
         Also settable via FUNC env var.

Environment:
  TARGET=name     Benchmark target (default: bench_merkle_dag)
  RUNS=N          Number of benchmark runs (default: 5)
  VTUNE_ITERS=N   VTune collection iterations (default: 10)
  PCPU=N          P-core CPU to pin to (default: 8)
  TRACE=path      Trace file (default: bench/vit_b.crtrace)
  BENCH_PATTERN=  Grep pattern for timing line (auto-detected from TARGET)

Available targets:
  bench_merkle_dag  bench_arena  bench_pool_allocator  bench_region_cache
  bench_replay_engine  bench_trace_ring  bench_meta_log  bench_smoke
  bench_dispatch  bench_expr_pool  bench_iteration_detector  bench_swiss_table

Examples:
  ./prof.sh bench                                # Quick timing, pinned to P-core
  ./prof.sh tma                                  # Full TMA analysis
  ./prof.sh tma-show                             # Re-display TMA from last collection
  ./prof.sh tma "" build_csr                     # TMA focused on build_csr function
  ./prof.sh show "" compute_memory               # Source hotspots for compute_memory_plan
  ./prof.sh hotlines                             # Top-40 hottest lines across all functions
  ./prof.sh baseline                             # Save baseline at current commit
  ./prof.sh compare                              # Compare new commit against baseline
  RUNS=10 ./prof.sh baseline v2                  # 10 runs, tagged "v2"
  FUNC=crucible::build_csr ./prof.sh show        # Explicit function via env
  TARGET=bench_trace_ring ./prof.sh bench        # Bench a different target
  TARGET=bench_arena ./prof.sh tma               # Full TMA for arena benchmark
EOF
}

# Expand function shorthand names to full qualified names.
expand_func() {
    local fn="$1"
    case "$fn" in
        build_trace)       echo "crucible::BackgroundThread::build_trace" ;;
        compute_memory*|memory_plan)  echo "crucible::BackgroundThread::compute_memory_plan" ;;
        build_csr|csr)     echo "crucible::build_csr" ;;
        make_region|region) echo "crucible::make_region" ;;
        content_hash|hash) echo "crucible::compute_content_hash" ;;
        merkle_hash)       echo "crucible::compute_merkle_hash" ;;
        *)                 echo "$fn" ;;  # pass through fully-qualified names
    esac
}

# Parse positional arguments: command [tag] [func]
# For commands that take both tag and func, the second positional arg
# is treated as func if it doesn't look like a tag (contains :: or
# matches a known shorthand).
parse_args() {
    local cmd="$1"
    shift

    local arg1="${1:-}"
    local arg2="${2:-}"

    # If arg1 looks like a function name, swap: treat as func with empty tag.
    local tag_arg=""
    case "$arg1" in
        *::*|build_trace|compute_memory*|memory_plan|build_csr|csr|make_region|region|content_hash|hash|merkle_hash)
            # arg1 is a func, no tag
            FUNC="$(expand_func "$arg1")"
            tag_arg=""  # no tag specified
            ;;
        *)
            # arg1 is a tag (or empty)
            tag_arg="$arg1"
            if [ -n "$arg2" ]; then
                FUNC="$(expand_func "$arg2")"
            fi
            ;;
    esac

    # Dispatch
    case "$cmd" in
        bench)    cmd_bench "$tag_arg" ;;
        baseline) cmd_baseline "$tag_arg" ;;
        compare)  cmd_compare "$tag_arg" ;;
        profile)  cmd_profile "$tag_arg" ;;
        tma)      cmd_tma "$tag_arg" ;;
        tma-show) cmd_tma_show "$tag_arg" ;;
        show)     cmd_show "$tag_arg" ;;
        asm)      cmd_asm "$tag_arg" ;;
        hotlines) cmd_hotlines "$tag_arg" ;;
        list)     cmd_list ;;
        clean)    cmd_clean ;;
    esac
}

case "${1:-}" in
    bench|baseline|compare|profile|tma|tma-show|show|asm|hotlines)
        CMD="$1"; shift; parse_args "$CMD" "$@" ;;
    list)     cmd_list ;;
    clean)    cmd_clean ;;
    -h|--help|help) usage ;;
    "") usage; exit 1 ;;
    *) die "Unknown command: $1. Run: prof.sh --help" ;;
esac
