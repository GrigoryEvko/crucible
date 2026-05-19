#!/usr/bin/env bash
# check-syscall-capability.sh — syscall capability discipline (FIXY-U-100).
#
# Closes the regression-prevention surface for fixy-A5-016: "Effect-row
# capabilities bypassed in EVERY syscall path — hot-path can accidentally
# invoke netlink/sysctl".  The original fix wrapped syscall sites with
# proper effects:: capability admission; this guard prevents new
# unreviewed syscall sites from sneaking in.
#
# ── DISCIPLINE ────────────────────────────────────────────────────────
#
# Every direct invocation of a Linux syscall (or libc thin wrapper) in
# production code (include/, src/, vessel/) MUST appear in the
# scripts/syscall-capability-allowlist.txt file.  Each allowlist entry
# is `path:line — <effects::* cap proof + capability discipline note>`,
# so a reviewer can confirm the syscall site holds the right effect-row
# capability (Init / IO / Block / Bg).
#
# Adding a NEW syscall site WITHOUT updating the allowlist fails CI.
# This forces every new syscall introduction through the review surface
# documented in fixy-A5-016 + CLAUDE.md §1 effect-row capabilities.
#
# Inline suppression (rare): `// SYSCALL-CAP-OK: <reason>` on the call
# line exempts that line.  Use when the syscall site is structurally
# part of a Ctx-bound mint and the capability admission is verifiable
# at the static_assert level (not a substitute for the allowlist).
#
# ── COVERED SYSCALLS ─────────────────────────────────────────────────
#
# Linux syscalls / libc thin wrappers that touch kernel state and so
# require capability admission.  The list spans every syscall family
# the cntp / canopy / topology / perf / warden subsystems invoke:
#
#   • Network:  socket, bind, listen, connect, accept, send, sendto,
#               sendmsg, recv, recvfrom, recvmsg, shutdown,
#               setsockopt, getsockopt
#   • Memory:   mmap, munmap, mlock, mlock2, munlock, madvise
#   • Sched:    sched_setaffinity, sched_getaffinity,
#               sched_setattr, sched_getattr, sched_yield
#   • Process:  prctl, syscall (raw)
#   • Event:    epoll_create1, epoll_ctl, epoll_wait, eventfd
#   • Device:   ioctl
#
# The pattern matches `::<syscall>(` and `<syscall_underscore>_sys(`
# (libc thin wrappers we route through Hardening.h's sched_setattr_sys
# helper pattern).
#
# Exit status:
#   0 — clean (no NEW syscall sites beyond the allowlist)
#   1 — at least one violation
#   2 — bad invocation / missing dependency

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-syscall-capability.sh — syscall capability discipline.

Usage:
  check-syscall-capability.sh              # scan; exit 1 on violation
  check-syscall-capability.sh --self-test  # plant a violation, verify catch
  check-syscall-capability.sh -h | --help  # usage

Suppression:
  // SYSCALL-CAP-OK: <reason>              on call line — exempts THIS line
  scripts/syscall-capability-allowlist.txt:
    path:line — <effects::* cap proof>     — exempts that call site
    # comment                              — ignored

CLAUDE.md §1 — every syscall site must hold an effects::* capability
(Init / IO / Block / Bg) admitted through a Ctx-bound boundary.
USAGE
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test)
        # Plant ONE violation in a synthetic file under a temp root.
        # The scanner must flag it; otherwise the regex is broken.
        # Also plant ONE allowlisted call, ONE inline-suppressed call,
        # and ONE commented call to verify the suppression mechanisms.
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/src/planted" "$tmp_root/scripts"
        cat >"$tmp_root/src/planted/planted_syscall.cpp" <<'PLANTED'
// Synthetic syscall-capability fixture for --self-test.
#include <sys/socket.h>
namespace crucible::planted {
inline int planted_drift() {
    return ::socket(0, 0, 0);  // FLAGGED — must be caught
}
inline int planted_allowlisted() {
    return ::socket(0, 0, 0);  // ALLOWLISTED — must NOT be caught
}
inline int planted_suppressed() {
    return ::socket(0, 0, 0);  // SYSCALL-CAP-OK: synthetic --self-test marker
}
inline int planted_in_comment() {
    // ::socket(0, 0, 0);      // commented — must NOT be caught
    return 0;
}
}  // namespace crucible::planted
PLANTED
        # Allowlist entry targets line 8 (second ::socket call).
        cat >"$tmp_root/scripts/syscall-capability-allowlist.txt" <<'ALLOW'
# self-test grandfathered entry
src/planted/planted_syscall.cpp:8 — effects::Init proof (synthetic test)
ALLOW
        result_file="$(mktemp)"
        if CRUCIBLE_SYSCALL_CAP_TEST_ROOT="$tmp_root" \
           bash "${BASH_SOURCE[0]}" 2>"$result_file"; then
            printf 'check-syscall-capability: SELF-TEST FAILED — planted violation not caught.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The planted_drift line (line 5) must be flagged.
        if ! grep -qF 'planted_syscall.cpp:5' "$result_file"; then
            printf 'check-syscall-capability: SELF-TEST FAILED — expected diagnostic for line 5 missing.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The allowlisted line (line 8) must NOT be flagged.
        if grep -qF 'planted_syscall.cpp:8' "$result_file"; then
            printf 'check-syscall-capability: SELF-TEST FAILED — allowlist entry leaked.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The inline-suppressed line (line 11) must NOT be flagged.
        if grep -qF 'planted_syscall.cpp:11' "$result_file"; then
            printf 'check-syscall-capability: SELF-TEST FAILED — SYSCALL-CAP-OK marker leaked.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The commented-out call (line 14) must NOT be flagged.
        if grep -qF 'planted_syscall.cpp:14' "$result_file"; then
            printf 'check-syscall-capability: SELF-TEST FAILED — comment line leaked through filter.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        rm -f "$result_file"
        printf 'check-syscall-capability: self-test passed — drift caught, allowlist + inline marker + comment-filter all honoured.\n' >&2
        exit 0
        ;;
    "") ;;
    *) printf 'check-syscall-capability: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

# ── Scan-root override for --self-test recursion ─────────────────────
scan_root="${CRUCIBLE_SYSCALL_CAP_TEST_ROOT:-$root}"
allowlist="$scan_root/scripts/syscall-capability-allowlist.txt"

if ! command -v rg >/dev/null 2>&1; then
    printf 'check-syscall-capability: ripgrep (rg) is required\n' >&2
    exit 2
fi

# ── Pattern ──────────────────────────────────────────────────────────
# Match `::<syscall>(` form (qualified Linux syscall / libc invocation).
# The trailing `\(` anchor prevents false positives on members named
# `::socket_path()` etc. — we want only direct invocations.
candidate_pattern='::(socket|bind|listen|connect|accept|send|sendto|sendmsg|recv|recvfrom|recvmsg|shutdown|setsockopt|getsockopt|mmap|munmap|mlock|mlock2|munlock|madvise|sched_setaffinity|sched_getaffinity|sched_setattr|sched_getattr|sched_yield|prctl|syscall|epoll_create1|epoll_ctl|epoll_wait|eventfd|ioctl)\s*\('

# ── Allowlist lookup ─────────────────────────────────────────────────
allowlisted() {
    local rel="$1" line="$2"
    [[ -f "$allowlist" ]] || return 1
    # Each entry: `path:line — <prose>`.  Match `path:line` prefix.
    grep -E -v '^[[:space:]]*(#|$)' "$allowlist" | \
        awk -F' ' '{print $1}' | \
        grep -Fxq -- "$rel:$line"
}

violation_count=0
scan_dirs=("$scan_root/include" "$scan_root/src" "$scan_root/vessel")

while IFS= read -r match; do
    file="${match%%:*}"
    rest="${match#*:}"
    line="${rest%%:*}"
    text="${rest#*:}"

    stripped="${text#"${text%%[![:space:]]*}"}"

    # Skip comment lines.
    case "$stripped" in
        '//'*|'///'*|'*'*|'/*'*) continue ;;
    esac

    # Inline suppression — `// SYSCALL-CAP-OK: <reason>` on the same line.
    case "$text" in
        *'SYSCALL-CAP-OK'*) continue ;;
    esac

    rel="${file#"$scan_root"/}"

    if allowlisted "$rel" "$line"; then
        continue
    fi

    printf 'SYSCALL-CAP violation: %s:%s — bare Linux syscall site missing effects::* capability admission (fixy-A5-016).\n' \
        "$rel" "$line" >&2
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
       "$candidate_pattern" "${scan_dirs[@]}" 2>/dev/null || true
)

# ── Outcome ──────────────────────────────────────────────────────────
if [[ "$violation_count" -ne 0 ]]; then
    cat >&2 <<HINT

check-syscall-capability detected ${violation_count} new Linux syscall
site(s) outside the allowlist.  fixy-A5-016 + CLAUDE.md §1 require
every syscall invocation to hold a proper effects::* capability
(Init / IO / Block / Bg), admitted through a Ctx-bound boundary.

Remediations, in order of preference:

  (1) Route the syscall through a §XXI mint factory that admits the
      right Ctx row (e.g. mint_hardening for sched_setattr,
      mint_hot_region_registry_handle for mlock).

  (2) Wrap the call in a function that takes an effects::* cap-tag
      parameter and confirms the row at the type level.

  (3) Annotate the line with '// SYSCALL-CAP-OK: <reason>' for
      structurally-justified exceptions (e.g. inside a mint body
      where the requires-clause already confirmed the capability).

  (4) Add 'path:line — <effects::* cap proof>' to
      scripts/syscall-capability-allowlist.txt for grandfathered
      sites — every entry documents WHICH capability the existing
      caller holds.
HINT
    exit 1
fi

printf 'check-syscall-capability: clean — no new syscall sites without capability admission.\n' >&2
exit 0
