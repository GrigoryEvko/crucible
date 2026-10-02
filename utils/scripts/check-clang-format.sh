#!/usr/bin/env bash
# check-clang-format.sh — the tree is clang-format clean, and stays clean.
#
# The repo .clang-format is explicit rather than inherited, so a formatter
# upgrade cannot silently move the style.  This guard is the other half of
# that: it fails CI when a file drifts from what the config produces.
#
# ── Speed ─────────────────────────────────────────────────────────────
#
# The formatter takes about 270 s of CPU time for the tree, and the
# generated files of test/session_oracle take half of that time.  Two things
# keep a run short:
#
#   * An executable formatter runs in parallel, one process for each batch.
#     The guard puts the files in batches by size, the largest file first,
#     so that the batches take about the same time.
#     CRUCIBLE_CLANG_FORMAT_JOBS sets the number of processes.  The default
#     is the CPU count, with a maximum of 32.
#   * A cache keeps each file that the formatter found clean, keyed on the
#     SHA-256 of its content.  The name of the cache file is the SHA-256 of
#     the formatter, its version, .clang-format and .clang-format-ignore.
#     A file with a cached key needs no formatter run.  A change of the
#     file, the formatter or the config gives a different key, so a cached
#     result cannot hide a drift.  The guard does not cache a drifting file.
#     CRUCIBLE_CLANG_FORMAT_CACHE names the directory.  The default is
#     ~/.cache/crucible/clang-format, and an empty value disables the cache.
#
# ── A script formatter runs in sequence ───────────────────────────────
#
# clang-format 23 needs glibc 2.44, and this host has 2.43.  So
# ~/.local/bin/clang-format-23 is a script that runs the formatter in a
# container, which bind-mounts the scan root with an SELinux relabel (`:Z`).
# Parallel containers race on the relabel.  A container that loses the race
# cannot read .clang-format, uses the LLVM default style, and reports almost
# each file as drifting.  The guard cannot see what a script starts, so it
# runs a script formatter in sequence, in batches of BATCH files.  A batch
# keeps the count of container starts low.
#
# ── Every formatter call reads stdin from /dev/null ──────────────────
#
# The container wrapper runs `podman run -i`, which sends stdin into the
# container.  When the stdin of a formatter call was the file list, the
# first batch got the remaining paths, and the guard checked only that
# batch.  On this tree that scan reported 100 of the 749 drifting files, and
# it printed no error.  --self-test runs a formatter that reads its stdin
# and requires the full count.
#
# ── Which binary ─────────────────────────────────────────────────────
#
# $CLANG_FORMAT wins if set.  Otherwise clang-format when its major version
# is 22 or 23, then clang-format-23, then clang-format.  Version 22.1.8 and
# 23.1.0 were verified to produce byte-identical output across this tree
# with this config, so CI can use either.
#
# Exit status:
#   0  — every file matches the config
#   1  — at least one file drifts (the list is printed)
#   2  — bad invocation, no formatter found, or the formatter failed on a
#        batch (an unreadable config, a crash), so a file was not checked

set -euo pipefail

. "$(dirname -- "${BASH_SOURCE[0]}")/repo_root.sh"

usage() {
    cat >&2 <<'USAGE'
check-clang-format.sh — the tree is clang-format clean, and stays clean.

Usage:
  check-clang-format.sh              # check; exit 1 on drift
  check-clang-format.sh --fix        # reformat the drifting files in place
  check-clang-format.sh --self-test  # plant drift and verify it is caught
  check-clang-format.sh -h | --help  # usage

Environment:
  CLANG_FORMAT                 formatter to use (default: clang-format 22 or 23,
                               then clang-format-23, then clang-format)
  CRUCIBLE_CLANG_FORMAT_JOBS   parallel formatter processes (default: CPU count, at most 32)
  CRUCIBLE_CLANG_FORMAT_CACHE  cache directory (default: ~/.cache/crucible/clang-format;
                               empty disables the cache)

Excluded: build*/, papers/ (LaTeX sources), and the generated perf/bpf/vmlinux.h.
USAGE
}

mode="check"
case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --fix) mode="fix" ;;
    --self-test) mode="self-test" ;;
    "") ;;
    *) printf 'check-clang-format: unknown option: %s\n\n' "$1" >&2; usage; exit 2 ;;
esac

# The major versions whose output was verified against this tree.
is_verified_version_() {
    case "$1" in
        *" 22."*|*" 23."*) return 0 ;;
        *) return 1 ;;
    esac
}

formatter="${CLANG_FORMAT:-}"
if [[ -z "$formatter" ]]; then
    if command -v clang-format >/dev/null 2>&1 \
        && is_verified_version_ "$(clang-format --version 2>/dev/null </dev/null || true)"; then
        formatter="clang-format"
    elif command -v clang-format-23 >/dev/null 2>&1; then
        formatter="clang-format-23"
    elif command -v clang-format >/dev/null 2>&1; then
        formatter="clang-format"
    else
        printf 'check-clang-format: no clang-format found.  Set $CLANG_FORMAT.\n' >&2
        exit 2
    fi
fi

# Debian and Ubuntu ship fd as `fdfind` because the name `fd` was taken.
finder="${FD:-}"
if [[ -z "$finder" ]]; then
    if command -v fd >/dev/null 2>&1; then
        finder="fd"
    elif command -v fdfind >/dev/null 2>&1; then
        finder="fdfind"
    else
        printf 'check-clang-format: fd (or fdfind) is required.  Set $FD.\n' >&2
        exit 2
    fi
fi

# The config was verified byte-for-byte against 22.1.8 and 23.1.0.  Other
# majors may format this dialect differently — clang-format's handling of
# `template for`, `^^T` and contract clauses has moved between releases —
# and a version that disagrees would report drift that is not there.  Warn
# rather than fail: a false green is worse than a noisy one, but so is
# blocking a contributor whose distro ships a different major.
formatter_version="$("$formatter" --version 2>/dev/null </dev/null || echo 'unknown')"
if ! is_verified_version_ "$formatter_version"; then
    printf 'check-clang-format: NOTE — %s.  The repo .clang-format was verified against 22.1.8 and 23.1.0; another major may report drift that is not real.\n' \
        "$formatter_version" >&2
fi

# The largest batch of one formatter call.  250 keeps every argv well under
# ARG_MAX, and a script formatter then starts fourteen times for the tree.
readonly BATCH="${CRUCIBLE_CLANG_FORMAT_BATCH:-250}"

formatter_path="$(type -P -- "$formatter" || true)"
is_parallel=0
if [[ -n "$formatter_path" && "$(LC_ALL=C head -c 4 -- "$formatter_path" 2>/dev/null || true)" == $'\177ELF' ]]; then
    is_parallel=1
fi
cpu_count="$(nproc 2>/dev/null || echo 1)"
jobs="${CRUCIBLE_CLANG_FORMAT_JOBS:-$(( cpu_count < 32 ? cpu_count : 32 ))}"
if ! [[ "$jobs" =~ ^[1-9][0-9]*$ ]]; then
    printf 'check-clang-format: CRUCIBLE_CLANG_FORMAT_JOBS must be a positive integer, not "%s".\n' "$jobs" >&2
    exit 2
fi

cache_dir="${CRUCIBLE_CLANG_FORMAT_CACHE-${XDG_CACHE_HOME:-$HOME/.cache}/crucible/clang-format}"

scan_root="${CRUCIBLE_CLANG_FORMAT_TEST_ROOT:-$REPO_ROOT}"

# Paths stay RELATIVE and the working directory is the scan root, because
# the container wrapper bind-mounts $PWD.  An absolute host path does not
# exist inside the container, and clang-format reports it as missing rather
# than failing loudly, which reads as a clean scan.  Staying relative is
# also what lets --self-test point the whole guard at a planted tree: the
# wrapper mounts that tree instead.
cd "$scan_root"

list_files() {
    "$finder" -e h -e hpp -e cpp -e cc -e cxx \
       -E 'build*' -E 'papers' -E 'vmlinux.h' .
}

# The SHA-256 of each input that changes the verdict of the formatter on a
# file that does not change: the formatter, its version, and the two config
# files of the scan root.
cache_identity_() {
    {
        printf 'check-clang-format cache 1\n%s\n' "$formatter_version"
        if [[ -n "$formatter_path" ]]; then sha256sum <"$formatter_path"; fi
        printf -- '-- .clang-format\n'
        if [[ -f .clang-format ]]; then cat .clang-format; fi
        printf -- '-- .clang-format-ignore\n'
        if [[ -f .clang-format-ignore ]]; then cat .clang-format-ignore; fi
    } | sha256sum | cut -d' ' -f1
}

# The formatter prints one diagnostic per drifting construct, so a file can
# appear many times.  Reduce to the file set: keep the violation lines, cut
# the path, dedupe.
#
# The exit status of each batch is read, because it is the only sign of a
# formatter that failed.  `--dry-run -Werror` exits 1 on drift.  A batch that
# exits 1 with no violation line did not check its files: a config the
# formatter cannot read, or a crash.  A batch that exits 0 with a violation
# line, or with any other status, is also a failure.  Each failure goes to
# stderr and the function returns 2, so a formatter that checked nothing can
# never read as a clean tree.
readonly VIOLATION='^[^ ][^:]*\.(h|hpp|cpp|cc|cxx):[0-9]+:[0-9]+: error: code should be clang-formatted'

run_batch_() {
    local output status=0 drift
    output="$("$formatter" --dry-run -Werror "$@" </dev/null 2>&1)" || status=$?
    drift="$(printf '%s\n' "$output" | grep -oE "$VIOLATION" | cut -d: -f1 | sort -u || true)"
    if { [[ "$status" -eq 0 && -z "$drift" ]]; } || { [[ "$status" -eq 1 && -n "$drift" ]]; }; then
        [[ -n "$drift" ]] && printf '%s\n' "$drift"
        return 0
    fi
    printf 'check-clang-format: the formatter failed on a batch of %d files (exit %d, %d violation lines):\n' \
        "$#" "$status" "$(printf '%s' "$drift" | grep -c . || true)" >&2
    printf '%s\n' "$output" | head -20 >&2
    return 2
}

# Puts the files of $1 into batch lists in the directory $2, and prints the
# list names.  An executable formatter gets min(jobs, files) lists.  The
# files go to the lists in turn, largest first, so the lists take about the
# same time.  A script formatter gets lists of BATCH files, in order.
# Complexity: O(n log n) in the file count, for the sort by size.
make_batches_() {
    local files_list="$1" batch_dir="$2" count lists index=0 file
    count="$(grep -c . "$files_list" || true)"
    (( count > 0 )) || return 0
    if (( is_parallel == 1 )); then
        lists=$(( jobs < count ? jobs : count ))
        while IFS= read -r file; do
            printf '%s\n' "$file" >>"$batch_dir/$(( index % lists ))"
            index=$(( index + 1 ))
        done < <(xargs -a "$files_list" -d '\n' stat -c '%s %n' -- | sort -rn -k1,1 | cut -d' ' -f2-)
    else
        lists=$(( (count + BATCH - 1) / BATCH ))
        while IFS= read -r file; do
            printf '%s\n' "$file" >>"$batch_dir/$(( index / BATCH ))"
            index=$(( index + 1 ))
        done <"$files_list"
    fi
    seq 0 $(( lists - 1 ))
}

# Runs the formatter on each file of $1 and prints the drifting files.
# Returns 2 when a batch failed, so that no file goes unchecked in silence.
drifting_files() {
    local files_list="$1" batch_dir name failed=0
    local -a names=() pids=()
    batch_dir="$(mktemp -d)"
    mapfile -t names < <(make_batches_ "$files_list" "$batch_dir")
    for name in "${names[@]}"; do
        if (( is_parallel == 1 )); then
            ( mapfile -t batch <"$batch_dir/$name"; run_batch_ "${batch[@]}" ) >"$batch_dir/$name.drift" &
            pids+=("$!")
        else
            ( mapfile -t batch <"$batch_dir/$name"; run_batch_ "${batch[@]}" ) >"$batch_dir/$name.drift" || failed=1
        fi
    done
    local pid
    for pid in "${pids[@]}"; do
        wait "$pid" || failed=1
    done
    for name in "${names[@]}"; do
        cat "$batch_dir/$name.drift"
    done
    rm -rf -- "$batch_dir"
    (( failed == 0 )) || return 2
}

if [[ "$mode" == "self-test" ]]; then
    # Plant one file that the config will want to change, and one it will
    # not, so the arms prove detection AND the absence of a false positive.
    tmp_root="$(mktemp -d)"
    trap 'rm -rf "$tmp_root"' EXIT
    mkdir -p "$tmp_root/include/crucible"
    cp "$REPO_ROOT/.clang-format" "$tmp_root/.clang-format"
    # Each inner run uses a cache in the planted tree, never the cache of the
    # user.
    export CRUCIBLE_CLANG_FORMAT_CACHE="$tmp_root/cache"

    cat >"$tmp_root/include/crucible/planted_clean.h" <<'CLEAN'
#pragma once
namespace crucible::planted {
struct Clean {
    int value = 0;
};
}  // namespace crucible::planted
CLEAN
    # Run the planted-clean file through the formatter so the arm asserts
    # "no false positive" against what this config actually produces,
    # rather than against what looks tidy to a human.  In a subshell with
    # the planted tree as the working directory, for the same reason the
    # scan stays relative: the container wrapper mounts $PWD.
    ( cd "$tmp_root" && "$formatter" -i include/crucible/planted_clean.h )
    cp "$tmp_root/include/crucible/planted_clean.h" "$tmp_root/clean.h.saved"

    rc=0
    CRUCIBLE_CLANG_FORMAT_TEST_ROOT="$tmp_root" bash "${BASH_SOURCE[0]}" >/dev/null 2>&1 || rc=$?
    if (( rc != 0 )); then
        printf 'check-clang-format: SELF-TEST FAILED — a formatted tree must pass (got %d).\n' "$rc" >&2
        exit 2
    fi

    # The clean run filled the cache, so a second run checks no file.
    rc=0
    report="$(CRUCIBLE_CLANG_FORMAT_TEST_ROOT="$tmp_root" bash "${BASH_SOURCE[0]}" 2>&1 >/dev/null)" || rc=$?
    if (( rc != 0 )) || [[ "$report" != *"0 checked, 1 from the cache"* ]]; then
        printf 'check-clang-format: SELF-TEST FAILED — a second clean run must take each file from the cache (got exit %d):\n%s\n' \
            "$rc" "$report" >&2
        exit 2
    fi

    cat >"$tmp_root/include/crucible/planted_drift.h" <<'DRIFT'
#pragma once
namespace crucible::planted {
struct    Drift {
      int   value=0;
};
}
DRIFT
    rc=0
    CRUCIBLE_CLANG_FORMAT_TEST_ROOT="$tmp_root" bash "${BASH_SOURCE[0]}" >/dev/null 2>&1 || rc=$?
    if (( rc != 1 )); then
        printf 'check-clang-format: SELF-TEST FAILED — planted drift must exit 1 (got %d).\n' "$rc" >&2
        exit 2
    fi
    rm -f "$tmp_root/include/crucible/planted_drift.h"

    # A cached clean file that then drifts has a new key, so the guard
    # checks it again and reports it.
    cp "$tmp_root/include/crucible/planted_clean.h" "$tmp_root/include/crucible/planted_clean.h.tmp"
    printf 'struct    Late {  int x=0; };\n' >>"$tmp_root/include/crucible/planted_clean.h"
    rc=0
    CRUCIBLE_CLANG_FORMAT_TEST_ROOT="$tmp_root" bash "${BASH_SOURCE[0]}" >/dev/null 2>&1 || rc=$?
    if (( rc != 1 )); then
        printf 'check-clang-format: SELF-TEST FAILED — a cached file that drifts must exit 1 (got %d).\n' "$rc" >&2
        exit 2
    fi
    mv "$tmp_root/include/crucible/planted_clean.h.tmp" "$tmp_root/include/crucible/planted_clean.h"

    # A config change gives each file a new key.  A file that is clean and
    # cached under an indent of four drifts under an indent of two.
    config_root="$tmp_root/config"
    mkdir -p "$config_root/include/crucible"
    printf -- '---\nBasedOnStyle: LLVM\nIndentWidth: 4\n...\n' >"$config_root/.clang-format"
    cp "$tmp_root/clean.h.saved" "$config_root/include/crucible/planted_clean.h"
    ( cd "$config_root" && "$formatter" -i include/crucible/planted_clean.h )
    rc=0
    CRUCIBLE_CLANG_FORMAT_TEST_ROOT="$config_root" bash "${BASH_SOURCE[0]}" >/dev/null 2>&1 || rc=$?
    if (( rc != 0 )); then
        printf 'check-clang-format: SELF-TEST FAILED — a tree formatted with its own config must pass (got %d).\n' "$rc" >&2
        exit 2
    fi
    printf -- '---\nBasedOnStyle: LLVM\nIndentWidth: 2\n...\n' >"$config_root/.clang-format"
    rc=0
    CRUCIBLE_CLANG_FORMAT_TEST_ROOT="$config_root" bash "${BASH_SOURCE[0]}" >/dev/null 2>&1 || rc=$?
    if (( rc != 1 )); then
        printf 'check-clang-format: SELF-TEST FAILED — a cached file must be checked again under a changed config (got %d).\n' "$rc" >&2
        exit 2
    fi

    # A formatter that fails without a violation line checked nothing.  The
    # guard must say so, not report a clean tree.
    rc=0
    CLANG_FORMAT=false CRUCIBLE_CLANG_FORMAT_TEST_ROOT="$tmp_root" bash "${BASH_SOURCE[0]}" >/dev/null 2>&1 || rc=$?
    if (( rc != 2 )); then
        printf 'check-clang-format: SELF-TEST FAILED — a formatter that fails must exit 2 (got %d).\n' "$rc" >&2
        exit 2
    fi

    # A config the formatter cannot read makes every batch fail the same way.
    broken_root="$tmp_root/broken"
    mkdir -p "$broken_root/include/crucible"
    printf 'NotARealClangFormatKey: 7\n' >"$broken_root/.clang-format"
    cp "$tmp_root/include/crucible/planted_clean.h" "$broken_root/include/crucible/planted_clean.h"
    rc=0
    CRUCIBLE_CLANG_FORMAT_TEST_ROOT="$broken_root" bash "${BASH_SOURCE[0]}" >/dev/null 2>&1 || rc=$?
    if (( rc != 2 )); then
        printf 'check-clang-format: SELF-TEST FAILED — an unreadable config must exit 2 (got %d).\n' "$rc" >&2
        exit 2
    fi

    # Many drifting files in parallel batches: each one is reported once.
    many_root="$tmp_root/many"
    mkdir -p "$many_root/include/crucible"
    cp "$REPO_ROOT/.clang-format" "$many_root/.clang-format"
    for (( index = 0; index < 40; ++index )); do
        cp "$tmp_root/clean.h.saved" "$many_root/include/crucible/clean_$index.h"
        printf 'struct    Drift%d {  int x=0; };\n' "$index" >"$many_root/include/crucible/drift_$index.h"
    done
    rc=0
    report="$(CRUCIBLE_CLANG_FORMAT_JOBS=8 CRUCIBLE_CLANG_FORMAT_TEST_ROOT="$many_root" \
        bash "${BASH_SOURCE[0]}" 2>&1 >/dev/null)" || rc=$?
    if (( rc != 1 )) || [[ "$report" != *"40 file(s) drift"* ]]; then
        printf 'check-clang-format: SELF-TEST FAILED — 40 drifting files in parallel batches must all be reported (got exit %d):\n%s\n' \
            "$rc" "$(printf '%s\n' "$report" | head -1)" >&2
        exit 2
    fi

    # A formatter that reads its stdin, as `podman run -i` does, must see
    # every file.  The stub is a script, so the guard runs it in sequence in
    # batches of BATCH files.  The planted tree has one more drifting file
    # than a batch holds, so one path stays on the list after the first
    # batch.  A formatter call with the list as its stdin reads that path,
    # and the count is one less than the file count.  The inner run gets
    # stdin from /dev/null, so the stub never waits on a terminal.
    stdin_root="$tmp_root/stdin"
    mkdir -p "$stdin_root/include/crucible"
    cp "$REPO_ROOT/.clang-format" "$stdin_root/.clang-format"
    for (( index = 0; index <= BATCH; ++index )); do
        printf 'struct    Drift%d {  int x=0; };\n' "$index" >"$stdin_root/include/crucible/drift_$index.h"
    done
    stub="$tmp_root/reads_stdin_formatter"
    printf '#!/usr/bin/env bash\ncat >/dev/null\nexec %q "$@"\n' "$(command -v "$formatter")" >"$stub"
    chmod +x "$stub"
    rc=0
    report="$(CLANG_FORMAT="$stub" CRUCIBLE_CLANG_FORMAT_TEST_ROOT="$stdin_root" \
        bash "${BASH_SOURCE[0]}" </dev/null 2>&1 >/dev/null)" || rc=$?
    if (( rc != 1 )) || [[ "$report" != *"$((BATCH + 1)) file(s) drift"* ]]; then
        printf 'check-clang-format: SELF-TEST FAILED — a formatter that reads stdin must see all %d drifting files (got exit %d):\n%s\n' \
            "$((BATCH + 1))" "$rc" "$(printf '%s\n' "$report" | head -1)" >&2
        exit 2
    fi

    printf 'check-clang-format: self-test passed — a formatted tree passes and fills the cache, planted drift exits 1, a cached file that drifts or a config change is checked again, parallel batches report each file, a failing formatter or an unreadable config exits 2, and a formatter that reads stdin sees every file.\n' >&2
    exit 0
fi

work_dir="$(mktemp -d)"
trap 'rm -rf -- "$work_dir"' EXIT
list_files >"$work_dir/files"

if [[ ! -s "$work_dir/files" ]]; then
    printf 'check-clang-format: no source files found under %s\n' "$scan_root" >&2
    exit 2
fi
file_count="$(grep -c . "$work_dir/files")"

# One line for each file: the SHA-256 of its content, two spaces, its path.
xargs -a "$work_dir/files" -d '\n' sha256sum -- >"$work_dir/hashes"

cache_file=""
if [[ -n "$cache_dir" ]]; then
    mkdir -p -- "$cache_dir"
    cache_file="$cache_dir/$(cache_identity_)"
fi
if [[ -n "$cache_file" && -s "$cache_file" ]]; then
    grep -Fxv -f "$cache_file" "$work_dir/hashes" >"$work_dir/unknown" || true
else
    cp "$work_dir/hashes" "$work_dir/unknown"
fi
# A SHA-256 is 64 hex digits, and sha256sum puts two characters before the path.
cut -c67- "$work_dir/unknown" >"$work_dir/to_check"
checked_count="$(grep -c . "$work_dir/to_check" || true)"

drift_rc=0
drifting_files "$work_dir/to_check" >"$work_dir/drift_raw" || drift_rc=$?
if (( drift_rc != 0 )); then
    printf 'check-clang-format: the formatter did not check every file, so the tree is not known to be clean.\n' >&2
    exit 2
fi
sort -u "$work_dir/drift_raw" >"$work_dir/drift"
drift_count="$(grep -c . "$work_dir/drift" || true)"

# The new cache holds each file of this run that does not drift.  A file of
# another revision leaves the cache, so its size stays at one line for each
# file of the tree.  The write goes to a temporary file and a rename, so a
# parallel run never reads half a cache.  A cache file that no run used for
# 30 days is removed.
if [[ -n "$cache_file" ]]; then
    declare -A is_drifting=()
    while IFS= read -r drifted; do is_drifting["$drifted"]=1; done <"$work_dir/drift"
    cache_tmp="$(mktemp -- "$cache_file.XXXXXX")"
    while IFS= read -r line; do
        [[ -n "${is_drifting["${line:66}"]:-}" ]] || printf '%s\n' "$line"
    done <"$work_dir/hashes" >"$cache_tmp"
    mv -f -- "$cache_tmp" "$cache_file"
    "$finder" -t f -d 1 --changed-before 30d . "$cache_dir" -X rm -f -- 2>/dev/null || true
fi

if [[ "$mode" == "fix" ]]; then
    if (( drift_count == 0 )); then
        printf 'check-clang-format: already clean — nothing to reformat.\n' >&2
        exit 0
    fi
    xargs -a "$work_dir/drift" -d '\n' -n "$BATCH" "$formatter" -i </dev/null
    printf 'check-clang-format: reformatted %d file(s).\n' "$drift_count" >&2
    exit 0
fi

if (( drift_count == 0 )); then
    printf 'check-clang-format: clean — all %d files match .clang-format (%s; %d checked, %d from the cache).\n' \
        "$file_count" "$formatter" "$checked_count" "$(( file_count - checked_count ))" >&2
    exit 0
fi

printf 'check-clang-format: %d file(s) drift from .clang-format:\n' "$drift_count" >&2
while IFS= read -r drifted; do printf '  %s\n' "$drifted" >&2; done <"$work_dir/drift"
printf '\nRun utils/scripts/check-clang-format.sh --fix to reformat, then commit the\nresult on its own so the diff stays reviewable.\n' >&2
exit 1
