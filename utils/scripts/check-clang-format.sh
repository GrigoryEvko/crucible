#!/usr/bin/env bash
# check-clang-format.sh — the tree is clang-format clean, and stays clean.
#
# The repo .clang-format is explicit rather than inherited, so a formatter
# upgrade cannot silently move the style.  This guard is the other half of
# that: it fails CI when a file drifts from what the config produces.
#
# ── Run it SEQUENTIALLY.  This is not a style preference. ─────────────
#
# clang-format 23 is not installable on this host — it needs glibc 2.44 and
# the host ships 2.43 — so ~/.local/bin/clang-format-23 runs it in a
# container that bind-mounts the repo with an SELinux relabel (`:Z`).  Each
# invocation relabels.  Running N of them in parallel over a 3,300-file tree
# makes N relabels race, and a container that loses the race cannot read
# .clang-format, silently falls back to LLVM default style, and reports
# almost every file as needing changes.
#
# Measured on this tree: `xargs -P 8` reported 2690 of 3333 files dirty in
# 52 s wall with 121 MINUTES of system time — the system-time figure is the
# tell, and the answer was garbage.  The same scan with `-n 250` and no
# `-P` takes 12 s and reports the true count, which was 32.
#
# So: batch the file list, one formatter process per batch, no -P.  Batching
# matters for a different reason than parallelism does — it amortizes
# container start-up, which is most of the cost.
#
# ── Every formatter call reads stdin from /dev/null ──────────────────
#
# Inside the read loop, stdin is the file list.  The container wrapper runs
# `podman run -i`, which sends stdin into the container.  The first batch
# then got the remaining paths, and the loop checked only that batch.  On
# this tree that scan reported 100 of the 749 drifting files, and it
# printed no error.  --self-test runs a formatter that reads its stdin and
# requires the full count.
#
# ── Which binary ─────────────────────────────────────────────────────
#
# $CLANG_FORMAT wins if set.  Otherwise clang-format-23, then clang-format.
# Version 22.1.8 and 23.1.0 were verified to produce byte-identical output
# across this tree with this config, so CI may use either.
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
  CLANG_FORMAT   formatter to use (default: clang-format-23, then clang-format)

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

formatter="${CLANG_FORMAT:-}"
if [[ -z "$formatter" ]]; then
    if command -v clang-format-23 >/dev/null 2>&1; then
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
formatter_version="$("$formatter" --version 2>/dev/null || echo 'unknown')"
case "$formatter_version" in
    *" 22."*|*" 23."*) ;;
    *) printf 'check-clang-format: NOTE — %s.  The repo .clang-format was verified against 22.1.8 and 23.1.0; another major may report drift that is not real.\n' \
              "$formatter_version" >&2 ;;
esac

# One batch size for both modes.  250 keeps every argv well under ARG_MAX
# while holding the container count to fourteen for the whole tree.
readonly BATCH=250

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

drifting_files() {
    local files_list="$1" file failed=0
    local -a batch=()
    while IFS= read -r file; do
        batch+=("$file")
        if (( ${#batch[@]} == BATCH )); then
            run_batch_ "${batch[@]}" || failed=1
            batch=()
        fi
    done <"$files_list"
    if (( ${#batch[@]} > 0 )); then
        run_batch_ "${batch[@]}" || failed=1
    fi
    (( failed == 0 )) || return 2
}

if [[ "$mode" == "self-test" ]]; then
    # Plant one file that the config will want to change, and one it will
    # not, so the arms prove detection AND the absence of a false positive.
    tmp_root="$(mktemp -d)"
    trap 'rm -rf "$tmp_root"' EXIT
    mkdir -p "$tmp_root/include/crucible"
    cp "$REPO_ROOT/.clang-format" "$tmp_root/.clang-format"

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

    rc=0
    CRUCIBLE_CLANG_FORMAT_TEST_ROOT="$tmp_root" bash "${BASH_SOURCE[0]}" >/dev/null 2>&1 || rc=$?
    if (( rc != 0 )); then
        printf 'check-clang-format: SELF-TEST FAILED — a formatted tree must pass (got %d).\n' "$rc" >&2
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

    # A formatter that reads its stdin, as `podman run -i` does, must see
    # every file.  The planted tree has one more drifting file than a batch
    # holds.  A full batch then runs inside the read loop with one path left
    # on the list.  A formatter call with the list as its stdin reads that
    # path, and the count is one less than the file count.  The inner run
    # gets stdin from /dev/null, so the stub never waits on a terminal.
    stdin_root="$tmp_root/stdin"
    mkdir -p "$stdin_root/include/crucible"
    cp "$REPO_ROOT/.clang-format" "$stdin_root/.clang-format"
    for (( index = 0; index <= BATCH; ++index )); do
        cp "$tmp_root/include/crucible/planted_drift.h" "$stdin_root/include/crucible/drift_$index.h"
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

    printf 'check-clang-format: self-test passed — a formatted tree passes, planted drift exits 1, a failing formatter or an unreadable config exits 2, and a formatter that reads stdin sees every file.\n' >&2
    exit 0
fi

files_tmp="$(mktemp)"
drift_tmp="$(mktemp)"
trap 'rm -f "$files_tmp" "$drift_tmp"' EXIT
list_files >"$files_tmp"

if [[ ! -s "$files_tmp" ]]; then
    printf 'check-clang-format: no source files found under %s\n' "$scan_root" >&2
    exit 2
fi

drift_rc=0
drifting_files "$files_tmp" >"$drift_tmp" || drift_rc=$?
if (( drift_rc != 0 )); then
    printf 'check-clang-format: the formatter did not check every file, so the tree is not known to be clean.\n' >&2
    exit 2
fi
drift_count="$(sort -u "$drift_tmp" | grep -c . || true)"

if [[ "$mode" == "fix" ]]; then
    if (( drift_count == 0 )); then
        printf 'check-clang-format: already clean — nothing to reformat.\n' >&2
        exit 0
    fi
    xargs -a "$drift_tmp" -n "$BATCH" "$formatter" -i </dev/null
    printf 'check-clang-format: reformatted %d file(s).\n' "$drift_count" >&2
    exit 0
fi

if (( drift_count == 0 )); then
    printf 'check-clang-format: clean — all %d files match .clang-format (%s).\n' \
        "$(grep -c . "$files_tmp")" "$formatter" >&2
    exit 0
fi

printf 'check-clang-format: %d file(s) drift from .clang-format:\n' "$drift_count" >&2
while IFS= read -r drifted; do printf '  %s\n' "$drifted" >&2; done <"$drift_tmp"
printf '\nRun utils/scripts/check-clang-format.sh --fix to reformat, then commit the\nresult on its own so the diff stays reviewable.\n' >&2
exit 1
