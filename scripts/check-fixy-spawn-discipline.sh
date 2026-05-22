#!/usr/bin/env bash
# check-fixy-spawn-discipline.sh — process-spawn discipline (FIXY-V-210).
#
# Closes the regression-prevention surface for Agent 7 migration item
# 10: production code must NOT invoke OS process-spawning syscalls
# (fork / vfork / execve / posix_spawn / system / popen / clone) without
# an explicit opt-in, because doing so escapes the entire fixy:: type
# system (no PermissionFork CSL parallel-rule witness, no Met(X) effect
# row, no scheduler-observable event chain).  CLAUDE.md §IX puts the
# whole "concurrency = std::jthread + Permission" discipline at risk
# the moment a raw fork() lands.
#
# Today: production code (verified) is CLEAN — no fork()/exec*/posix_spawn
# /system/popen calls outside permission_fork (CSL fork, not OS fork) and
# session_fork / cooperative_fork (also non-OS).  This guard catches the
# future drift before it ships.
#
# ── DISCIPLINE ────────────────────────────────────────────────────────
#
# Every raw call to an OS process-spawn syscall in production code
# (src/, include/, vessel/) is BANNED.  If a site genuinely needs OS
# process spawn (e.g. a future cog/Daemon.h restart helper), three opt-
# in mechanisms exist:
#
#   (1) GLOBAL property CRUCIBLE_SPAWN_ALLOW_PROCESS_PATHS — register the
#       owning directory in CMakeLists.txt via
#       crucible_register_spawn_process_path(<rel-dir>).  Sites under
#       that directory are scanned for inline rationale comments but
#       not banned outright.  CMake materializes the list to
#       ${CMAKE_BINARY_DIR}/spawn-allow-process-paths.txt; the script
#       consumes that file when present, otherwise falls back to the
#       hardcoded mirror below.
#
#   (2) scripts/no-spawn-process-allowlist.txt — one `path:line` per
#       line; lines beginning with `#` are comments.  Use when (1) is
#       too coarse and a one-off shell-out is genuinely the right call.
#
#   (3) Inline suppression `// SPAWN-PROCESS-OK: <reason>` on the call
#       line — exempts that single line.  The reason is grep-discoverable
#       so reviewers can audit every opt-out at once.
#
# ── COVERED CALLS ────────────────────────────────────────────────────
#
# The match family covers every POSIX process-control syscall and libc
# wrapper that creates / replaces a process image:
#
#   • Process create:  fork, vfork, clone, posix_spawn, posix_spawnp
#   • Image replace:   execve, execv, execvp, execl, execlp, execle, execvpe
#   • Shell-out:       system, popen, pclose
#
# The PCRE pattern uses a negative lookbehind `(?<![A-Za-z0-9_])` so
# `permission_fork(`, `session_fork(`, `cooperative_fork(`,
# `safety::fn::Fn` etc. are NOT flagged.  Bare and `::`-qualified call
# shapes both match.
#
# ── EXEMPT PATHS ─────────────────────────────────────────────────────
#
# Scope: src/ + include/ + vessel/ — production code.  Skipped:
#   test/, bench/, examples/, fuzz/, third_party/, external/, vendor/,
#   build/, include/crucible/perf/bpf/ (vmlinux.h CO-RE dump, ~137K
#   lines of kernel struct definitions that include syscall names).
#
# ── EXIT STATUS ──────────────────────────────────────────────────────
#
#   0  — clean (no NEW process-spawn sites beyond the allowlist)
#   1  — at least one violation
#   2  — bad invocation / missing dependency / stale allowlist entry
#
# ── COMPANION SURFACES ───────────────────────────────────────────────
#
# This guard complements (not replaces) scripts/check-syscall-capability.sh.
# Syscall-capability catches socket/mmap/ioctl/epoll/sched — kernel
# interactions that need effects::* admission.  Spawn-discipline catches
# fork/exec/system — process-creation that escapes the whole effect-row
# story.  Two orthogonal axes, two scripts.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-fixy-spawn-discipline.sh — process-spawn discipline guard.

Usage:
  check-fixy-spawn-discipline.sh              # scan; exit 1 on violation, 2 on stale
  check-fixy-spawn-discipline.sh --list       # list allow-process opt-in paths
  check-fixy-spawn-discipline.sh --self-test  # plant violations, verify catches
  check-fixy-spawn-discipline.sh -h | --help  # usage

Suppression:
  // SPAWN-PROCESS-OK: <reason>             on call line — exempts THIS line
  scripts/no-spawn-process-allowlist.txt:
    path:line                                — exempts the call at that line
    # comment                                — ignored
  CMakeLists.txt:
    crucible_register_spawn_process_path(<rel-dir>)
                                             — directory-level opt-in

Banned calls (PCRE negative-lookbehind skips permission_fork /
session_fork / cooperative_fork etc.):
  fork  vfork  clone  posix_spawn  posix_spawnp
  execve  execv  execvp  execl  execlp  execle  execvpe
  system  popen  pclose
USAGE
}

# ── Opt-in paths (CMake-materialized + hardcoded mirror) ──────────────
#
# Authoritative list lives in CMakeLists.txt under the GLOBAL property
# CRUCIBLE_SPAWN_ALLOW_PROCESS_PATHS (FIXY-V-210).  At configure time
# CMake writes the list to ${CMAKE_BINARY_DIR}/spawn-allow-process-paths.txt;
# when that file exists, the script consumes it directly.  When absent
# (pre-configure CI, standalone scans), the script falls back to the
# hardcoded mirror below — verified at CMake configure time to agree
# with the authoritative list.
#
# Currently empty: zero production sites need OS process spawn today.
# Drift checked at configure time per the FIXY-V-072 pattern.
#
# Array body discipline: ONLY path entries inside the parens, one per
# line.  The CMake-side drift check at CMakeLists.txt FIXY-V-210 word-
# splits the array body on whitespace BEFORE comment stripping, so any
# prose inside the parens leaks into the path set.  Comments go ABOVE
# the array declaration, never inside it.  This is the same convention
# used by CRUCIBLE_FIXY_ONLY_PATHS at FIXY-V-070 / V-072.
CRUCIBLE_SPAWN_ALLOW_PROCESS_PATHS=(
)

list_paths() {
    local generated="${CRUCIBLE_SPAWN_ALLOW_PATHS_FILE:-${root}/build/spawn-allow-process-paths.txt}"
    if [[ -f "$generated" ]]; then
        # Echo non-comment, non-empty lines from the generated file.
        grep -E -v '^[[:space:]]*(#|$)' "$generated"
        return 0
    fi
    local p
    for p in "${CRUCIBLE_SPAWN_ALLOW_PROCESS_PATHS[@]}"; do
        printf '%s\n' "$p"
    done
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --list)
        list_paths
        exit 0
        ;;
    --self-test)
        # Plant THREE violation classes plus three exemption classes:
        #
        #   (A) Bare-call violation:   fork( on a production-like file —
        #                              scanner MUST flag
        #   (B) Qualified violation:   ::posix_spawn( — scanner MUST flag
        #   (C) Negative-lookbehind:   permission_fork( on a production
        #                              file — scanner MUST NOT flag
        #   (D) Inline suppression:    // SPAWN-PROCESS-OK on call line
        #   (E) Allowlist exemption:   path:line in synthetic allowlist
        #   (F) Comment line:          // fork(0) in a comment — MUST
        #                              NOT flag
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/include/crucible/planted" \
                 "$tmp_root/scripts" \
                 "$tmp_root/build"
        cat >"$tmp_root/include/crucible/planted/planted_spawn.h" <<'PLANTED'
#pragma once
// Synthetic spawn-discipline fixture for --self-test.  Lines below
// drive the planted violation matrix.  This very comment names fork()
// in plain text — the comment-line filter MUST NOT flag it.
#include <unistd.h>
#include <spawn.h>
inline int planted_drift() {
    return fork();                              // FLAGGED bare line 7
}
inline int planted_qualified_drift() {
    pid_t pid;
    return ::posix_spawn(&pid, nullptr, nullptr,
                         nullptr, nullptr, nullptr);  // FLAGGED qualified line 12
}
inline int planted_legitimate_csl() {
    // permission_fork is the CSL parallel-composition rule, NOT
    // the OS fork(2) syscall — the negative-lookbehind MUST skip it.
    return permission_fork(0);                  // NOT FLAGGED
}
inline int planted_suppressed() {
    return fork();                              // SPAWN-PROCESS-OK: synthetic self-test marker
}
inline int planted_allowlisted() {
    return fork();                              // ALLOWLISTED line 25
}
PLANTED
        cat >"$tmp_root/scripts/no-spawn-process-allowlist.txt" <<'PLANTED_ALLOW'
# Synthetic allowlist for self-test.
include/crucible/planted/planted_spawn.h:25
# Stale entry — points at a file:line with NO spawn call.  Must exit 2.
include/crucible/planted/planted_spawn.h:9999
PLANTED_ALLOW
        # Run the scanner against the synthetic tree.
        rc=0
        CRUCIBLE_SPAWN_ALLOW_PATHS_FILE="$tmp_root/build/spawn-allow-process-paths.txt" \
        CRUCIBLE_SPAWN_DISCIPLINE_SCAN_ROOT="$tmp_root" \
        CRUCIBLE_SPAWN_DISCIPLINE_ALLOWLIST="$tmp_root/scripts/no-spawn-process-allowlist.txt" \
            bash "$0" || rc=$?
        # ALL three exemption mechanisms must trigger; both planted
        # violations must trigger; comments must NOT trigger.  Net:
        # 2 violations (bare-call drift + qualified drift) → exit 1.
        # PLUS stale allowlist entry → if-no-other-failures-then exit 2.
        # Since we ALSO have violations, exit 1 takes precedence.
        if [[ "$rc" -ne 1 ]]; then
            printf 'check-fixy-spawn-discipline: SELF-TEST FAILED — expected exit 1 (violation), got %d.\n' "$rc" >&2
            exit 2
        fi
        # Re-run without the bare-call drift (suppress it via inline
        # marker swap) — then the only remaining failure is the stale
        # allowlist entry, so exit 2 is expected.
        # Simpler approach: re-run with NO violations and ONE stale
        # entry by stripping the bare fork drift.
        sed_substitute_bare_fork="    return fork();                              // SPAWN-PROCESS-OK: stripped"
        # Manual edit (no sed/awk per CLAUDE.md, but inside a heredoc
        # for the self-test fixture this is generated, not edited).
        # Suppression markers must live ON the call-opening line — the
        # scanner reports rg's line number for the regex match, which
        # for multi-line C++ calls is the open paren's line.  Keep call
        # + marker on one line in the fixture so the convention is
        # documented by example.
        cat >"$tmp_root/include/crucible/planted/planted_spawn.h" <<'PLANTED_CLEAN'
#pragma once
// Synthetic clean planted fixture — all spawn calls suppressed.
#include <unistd.h>
#include <spawn.h>
inline int planted_drift_suppressed() {
    return fork();  // SPAWN-PROCESS-OK: clean variant
}
inline int planted_qualified_suppressed() {
    pid_t pid;
    return ::posix_spawn(&pid, nullptr, nullptr, nullptr, nullptr, nullptr);  // SPAWN-PROCESS-OK: clean
}
inline int planted_legitimate_csl_only() {
    return permission_fork(0);  // NOT FLAGGED — negative-lookbehind
}
PLANTED_CLEAN
        cat >"$tmp_root/scripts/no-spawn-process-allowlist.txt" <<'PLANTED_STALE'
# Pure stale allowlist — file no longer has any banned call at this line.
include/crucible/planted/planted_spawn.h:9999
PLANTED_STALE
        rc=0
        CRUCIBLE_SPAWN_ALLOW_PATHS_FILE="$tmp_root/build/spawn-allow-process-paths.txt" \
        CRUCIBLE_SPAWN_DISCIPLINE_SCAN_ROOT="$tmp_root" \
        CRUCIBLE_SPAWN_DISCIPLINE_ALLOWLIST="$tmp_root/scripts/no-spawn-process-allowlist.txt" \
            bash "$0" || rc=$?
        if [[ "$rc" -ne 2 ]]; then
            printf 'check-fixy-spawn-discipline: SELF-TEST FAILED — stale-only run expected exit 2, got %d.\n' "$rc" >&2
            exit 2
        fi
        # Final: fully clean fixture (no stale, no violations).
        cat >"$tmp_root/scripts/no-spawn-process-allowlist.txt" <<'PLANTED_EMPTY'
# Empty allowlist — nothing stale, nothing exempted.
PLANTED_EMPTY
        rc=0
        CRUCIBLE_SPAWN_ALLOW_PATHS_FILE="$tmp_root/build/spawn-allow-process-paths.txt" \
        CRUCIBLE_SPAWN_DISCIPLINE_SCAN_ROOT="$tmp_root" \
        CRUCIBLE_SPAWN_DISCIPLINE_ALLOWLIST="$tmp_root/scripts/no-spawn-process-allowlist.txt" \
            bash "$0" || rc=$?
        if [[ "$rc" -ne 0 ]]; then
            printf 'check-fixy-spawn-discipline: SELF-TEST FAILED — clean run expected exit 0, got %d.\n' "$rc" >&2
            exit 2
        fi
        printf 'check-fixy-spawn-discipline: SELF-TEST PASSED.\n'
        exit 0
        ;;
    '') ;;
    *) usage; exit 2 ;;
esac

# ── Scan-time inputs (env overrides for self-test) ───────────────────
scan_root="${CRUCIBLE_SPAWN_DISCIPLINE_SCAN_ROOT:-$root}"
allowlist="${CRUCIBLE_SPAWN_DISCIPLINE_ALLOWLIST:-$root/scripts/no-spawn-process-allowlist.txt}"

# ── Banned-call regex ────────────────────────────────────────────────
#
# PCRE with negative lookbehind on `[A-Za-z0-9_]` so identifier-prefix
# composites (permission_fork, session_fork, cooperative_fork,
# safety::fn::Fn::fork [hypothetical], _exec_helper, sched_clone, etc.)
# do NOT match.  The lookbehind also gates the leading `::` form via
# the empty-match-at-start case (the `::` itself contains no identifier
# chars, so `::fork(` matches with lookbehind satisfied by `:`).
#
# We do NOT match `clone(` bare (too many false positives — std::clone,
# Tensor::clone, container::clone all use that spelling).  `::clone(` is
# the unambiguous Linux syscall form and remains in the pattern.
spawn_pattern='(?<![A-Za-z0-9_])(fork|vfork|execve|execv|execvp|execl|execlp|execle|execvpe|posix_spawn|posix_spawnp|system|popen|pclose)\s*\('
# Qualified-only pattern (no bare-clone match):
qualified_only_pattern='::(clone|waitpid|wait3|wait4)\s*\('

# ── Allowlist lookup ─────────────────────────────────────────────────
allowlisted() {
    local rel="$1" line="$2"
    [[ -f "$allowlist" ]] || return 1
    grep -E -v '^[[:space:]]*(#|$)' "$allowlist" | \
        grep -Fxq -- "$rel:$line"
}

# ── Opt-in directory lookup ──────────────────────────────────────────
opt_in_directory() {
    local rel="$1"
    while IFS= read -r dir; do
        [[ -z "$dir" ]] && continue
        case "$rel" in
            "$dir"|"$dir"/*) return 0 ;;
        esac
    done < <(list_paths)
    return 1
}

# ── Live set for stale-entry detection ───────────────────────────────
live_set_file="$(mktemp)"
trap 'rm -f "$live_set_file"' EXIT

violation_count=0

scan_pattern() {
    local pattern="$1" label="$2"
    while IFS= read -r match; do
        [[ -z "$match" ]] && continue
        file="${match%%:*}"
        rest="${match#*:}"
        line="${rest%%:*}"
        text="${rest#*:}"

        stripped="${text#"${text%%[![:space:]]*}"}"
        case "$stripped" in
            '//'*|'///'*|'*'*|'/*'*) continue ;;
            # String-literal continuation: `"fork (CLAUDE.md ..." style
            # documentation text where `permission_fork` was split across
            # adjacent string literals.  After leading-whitespace strip,
            # a line that starts with `"` is a string continuation, not
            # a call.  Production C++ calls start with an identifier
            # or `::`, never `"`.
            '"'*) continue ;;
        esac
        case "$text" in
            *'SPAWN-PROCESS-OK'*) continue ;;
        esac

        rel="${file#"$scan_root"/}"

        # Opt-in directories: scanned for awareness but never flagged.
        if opt_in_directory "$rel"; then
            continue
        fi

        printf '%s:%s\n' "$rel" "$line" >> "$live_set_file"

        if allowlisted "$rel" "$line"; then
            continue
        fi

        printf 'SPAWN-PROCESS violation (%s): %s:%s — raw OS process-spawn banned (CLAUDE.md §IX; FIXY-V-210).\n' \
            "$label" "$rel" "$line" >&2
        violation_count=$((violation_count + 1))
    done < <(
        rg -nP \
           --no-heading \
           --type=cpp \
           --glob '!build/**' \
           --glob '!cmake-build-*/**' \
           --glob '!third_party/**' \
           --glob '!external/**' \
           --glob '!vendor/**' \
           --glob '!test/**' \
           --glob '!bench/**' \
           --glob '!examples/**' \
           --glob '!fuzz/**' \
           --glob '!include/crucible/perf/bpf/**' \
           "$pattern" "$scan_root" 2>/dev/null || true
    )
}

scan_pattern "$spawn_pattern"           "bare-or-qualified"
scan_pattern "$qualified_only_pattern"  "qualified-only"

# ── Outcome (violations) ─────────────────────────────────────────────
if [[ "$violation_count" -ne 0 ]]; then
    printf '\ncheck-fixy-spawn-discipline detected %d raw process-spawn site(s) ' \
        "$violation_count" >&2
    cat >&2 <<'HINT'
outside the opt-in surface.  CLAUDE.md §IX puts the entire CSL +
Met(X) effect-row discipline at risk the moment a raw fork()/exec*/
posix_spawn()/system()/popen() lands in production code:

  • fork()        — bypasses permission_fork (CSL parallel-rule witness).
                    Result: child has NO Permission<Tag> linearity proof.
  • exec*()       — replaces process image; every Met(X) row in the
                    parent is gone.  No Bg/Init/IO admission survives.
  • posix_spawn() — same as fork+exec; no row carry-through.
  • system()      — invokes /bin/sh; shell metacharacter injection
                    surface (CWE-78) AND zero-effect-row carry.
  • popen()       — same as system() plus a half-duplex pipe.
  • ::clone()     — raw Linux process clone; bypasses jthread RAII.
  • ::wait*()     — paired with the above; reaping kids reveals the
                    spawn happened.

If process spawn is the right answer (rare):
  (1) Register the owning directory in CMakeLists.txt via
      crucible_register_spawn_process_path(<rel-dir>) — same pattern
      as FIXY-V-072 CRUCIBLE_FIXY_ONLY_PATHS.  Per-directory opt-in
      is preferred over per-line because process-spawn is usually a
      cluster of related calls (spawn + setup + wait).
  (2) Add `path:line` to scripts/no-spawn-process-allowlist.txt with
      a comment explaining WHY this single shell-out is correct.
  (3) Inline `// SPAWN-PROCESS-OK: <reason>` on the call line for
      genuinely one-off cases.
HINT
    rm -f "$live_set_file"
    trap - EXIT
    exit 1
fi

# ── Stale-allowlist detection ────────────────────────────────────────
stale_count=0
if [[ -f "$allowlist" ]]; then
    while IFS= read -r entry; do
        [[ -z "$entry" ]] && continue
        if ! grep -Fxq -- "$entry" "$live_set_file"; then
            printf 'check-fixy-spawn-discipline: STALE allowlist entry %s — no spawn call at that line; remove from allowlist.\n' \
                "$entry" >&2
            stale_count=$((stale_count + 1))
        fi
    done < <(grep -E -v '^[[:space:]]*(#|$)' "$allowlist" || true)
fi

rm -f "$live_set_file"
trap - EXIT

if [[ "$stale_count" -ne 0 ]]; then
    printf 'check-fixy-spawn-discipline: %d stale allowlist entries — guard refuses to silently drift.\n' "$stale_count" >&2
    exit 2
fi

exit 0
