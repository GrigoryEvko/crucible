#!/usr/bin/env bash
# Collect a PGO profile for the hardware tier of this host.
#
# The script configures the pgo-generate preset, builds every bench that
# bench/CMakeLists.txt registers, runs each one, and writes the
# COLLECTION record next to the counters.  CMake chooses the profile
# directory from the name the compiler gives this silicon, so the same
# command collects a znver5 profile on the EPYC host and a
# sapphirerapids profile on the Xeon host.  When the script completes:
#
#     cmake --preset pgo && cmake --build --preset pgo
#
# Environment:
#   CRUCIBLE_PGO_DIR            The profile root.  The preset value is <tree>/pgo
#   CRUCIBLE_PGO_JOBS           Build parallelism.  The preset value is nproc
#   CRUCIBLE_PGO_BENCH_TIMEOUT  Seconds for each bench.  The preset value is 900
#   CRUCIBLE_PGO_SKIP           An extended regex of bench names to skip
#
# The benches inherit the CPU affinity of this shell.  Run the script
# under taskset to keep them off reserved cores.
#
# A bench that fails or exceeds its time contributes no counters, and
# COLLECTION names it.  The script fails only when no counters exist at
# the end.
set -euo pipefail

. "$(dirname -- "${BASH_SOURCE[0]}")/repo_root.sh"
cd "$REPO_ROOT"

jobs="${CRUCIBLE_PGO_JOBS:-$(nproc)}"
bench_timeout="${CRUCIBLE_PGO_BENCH_TIMEOUT:-900}"
skip_regex="${CRUCIBLE_PGO_SKIP:-}"
build_dir="$REPO_ROOT/build-pgo-generate"

# 1. Configure.  CMake resolves the tier and the profile directory.  The
#    query files ask CMake to write its cache and its code model through the
#    file API, so step 2 reads what CMake decided, not the text of a list file.
configure_args=()
if [[ -n "${CRUCIBLE_PGO_DIR:-}" ]]; then
    configure_args+=("-DCRUCIBLE_PGO_DIR=$CRUCIBLE_PGO_DIR")
fi
query_dir="$build_dir/.cmake/api/v1/query"
mkdir -p "$query_dir"
: >"$query_dir/cache-v2"
: >"$query_dir/codemodel-v2"
cmake --preset pgo-generate "${configure_args[@]}"

# 2. The input set, enumerated before anything runs.  The first line is the
#    profile directory, the second the tier, and each other line a bench: an
#    executable whose FOLDER property is crucible_bench, which the
#    crucible_bench() function in bench/CMakeLists.txt sets.
if ! facts_text="$(python3 - "$build_dir" <<'PY'
import json, sys
from pathlib import Path
reply = Path(sys.argv[1]) / ".cmake" / "api" / "v1" / "reply"
# The index with the lexically greatest name is the newest one.
index = json.loads(max(reply.glob("index-*.json")).read_text())
objects = {entry["kind"]: entry for entry in index["objects"]}
cache = json.loads((reply / objects["cache"]["jsonFile"]).read_text())
values = {entry["name"]: entry["value"] for entry in cache["entries"]}
print(values.get("CRUCIBLE_PGO_PROFILE_DIR", ""))
print(values.get("CRUCIBLE_PGO_TIER", ""))
model = json.loads((reply / objects["codemodel"]["jsonFile"]).read_text())
benches = []
for target in model["configurations"][0]["targets"]:
    detail = json.loads((reply / target["jsonFile"]).read_text())
    if detail.get("type") == "EXECUTABLE" and detail.get("folder", {}).get("name") == "crucible_bench":
        benches.append(detail["name"])
print("\n".join(sorted(benches)))
PY
)"; then
    echo "pgo-bootstrap: the CMake file API reply in $build_dir could not be read." >&2
    exit 1
fi
mapfile -t facts <<<"$facts_text"
profile_dir="${facts[0]:-}"
tier="${facts[1]:-}"
if [[ -z "$profile_dir" || -z "$tier" ]]; then
    echo "pgo-bootstrap: the CMake cache of $build_dir has no CRUCIBLE_PGO_PROFILE_DIR or CRUCIBLE_PGO_TIER." >&2
    exit 1
fi
registered=()
for name in "${facts[@]:2}"; do
    [[ -n "$name" ]] && registered+=("$name")
done
benches=()
skipped=()
for name in "${registered[@]}"; do
    if [[ -n "$skip_regex" && "$name" =~ $skip_regex ]]; then
        skipped+=("$name")
    else
        benches+=("$name")
    fi
done
printf 'pgo-bootstrap: tier %s, profile directory %s\n' "$tier" "$profile_dir"
printf 'pgo-bootstrap: %d benches registered in bench/CMakeLists.txt, %d to run, %d skipped\n' \
    "${#registered[@]}" "${#benches[@]}" "${#skipped[@]}"
printf '  %s\n' "${benches[@]}"
if [[ ${#benches[@]} -eq 0 ]]; then
    echo "pgo-bootstrap: nothing to run." >&2
    exit 1
fi

# 3. Build the instrumented benches.
cmake --build "$build_dir" -j "$jobs" --target "${benches[@]}"

# 4. Start from no counters.  A rebuilt object replaces its counter
#    file on its own, and a bench run again adds to it, so this only
#    removes what an older collection left for objects that no longer
#    exist.
mkdir -p "$profile_dir"
find "$profile_dir" -name '*.gcda' -delete

# 5. Run.
run_log_dir="$build_dir/pgo-run"
mkdir -p "$run_log_dir"
ok=()
failed=()
timed_out=()
for name in "${benches[@]}"; do
    printf 'pgo-bootstrap: %s ... ' "$name"
    set +e
    timeout --signal=TERM "$bench_timeout" "$build_dir/bench/$name" >"$run_log_dir/$name.log" 2>&1
    status=$?
    set -e
    case $status in
        0)
            ok+=("$name")
            echo ok
            ;;
        124)
            timed_out+=("$name")
            echo "stopped after ${bench_timeout}s"
            ;;
        *)
            failed+=("$name")
            echo "exit $status, log in $run_log_dir/$name.log"
            ;;
    esac
done

# 6. Record what was collected.
counter_count="$(find "$profile_dir" -name '*.gcda' | wc -l)"
commit="$(git rev-parse HEAD)"
if git diff --quiet && git diff --cached --quiet; then
    tree_state=clean
else
    tree_state=dirty
fi
{
    echo "commit=$commit"
    echo "tree=$tree_state"
    echo "collected=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "host=$(hostname)"
    echo "tier=$tier"
    echo "benches_ok=${ok[*]:-}"
    echo "benches_failed=${failed[*]:-}"
    echo "benches_timed_out=${timed_out[*]:-}"
    echo "benches_skipped=${skipped[*]:-}"
    echo "gcda_files=$counter_count"
} >"$profile_dir/COLLECTION"

printf 'pgo-bootstrap: %s counter files in %s (%d ok, %d failed, %d stopped)\n' \
    "$counter_count" "$profile_dir" "${#ok[@]}" "${#failed[@]}" "${#timed_out[@]}"
if [[ "$counter_count" -eq 0 ]]; then
    echo "pgo-bootstrap: no counters were written. The logs are in $run_log_dir." >&2
    exit 1
fi
echo "pgo-bootstrap: next, cmake --preset pgo && cmake --build --preset pgo"
