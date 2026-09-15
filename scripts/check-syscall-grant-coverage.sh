#!/usr/bin/env bash
# check-syscall-grant-coverage.sh — derive the hardening grant set from
# the code that issues the system calls, and fail when the declared set
# and the derived set disagree.
#
# ── WHY THIS EXISTS ──────────────────────────────────────────────────
#
# include/crucible/warden/Hardening.h publishes
# mint_hardening_syscall_grants: a type-level tuple naming every
# privileged system call the apply path issues.  That tuple is the
# auditable claim about what this code does to the kernel.
#
# Beside it sit a tuple_size static_assert and
# test/test_fixy_v_180_hardening_syscall_grants.cpp, which asserts the
# count and each element by index.  Neither reads the call sites.  Both
# compare the list against a second copy of the same list, written by
# the same hand, so a syscall the code issues but the list omits leaves
# every check green.  That is how sched_getaffinity and sched_getattr
# went unlisted: apply() called them at two call sites while the tuple
# named seven entries and the test asserted seven.
#
# This guard closes the loop the other direction.  It reads the call
# sites out of the header, maps each to a SyscallId enumerator, and
# compares that derived set with the enumerators the tuple names.  A
# syscall added to the code without an entry fails; an entry with no
# call site behind it fails too, because an over-broad grant set is a
# false claim in the other direction.
#
# ── WHAT IT MATCHES ──────────────────────────────────────────────────
#
# Two call shapes, because the header uses both:
#
#   ::name(            the direct libc / global-scope spelling
#   name_sys(          the raw-syscall helpers this header defines
#                      (sched_setattr_sys, sched_getattr_sys, mlock2_sys),
#                      each of which wraps ::syscall(SYS_name, ...)
#
# `::syscall(SYS_name, ...)` inside a *_sys helper body is deliberately
# NOT counted on its own.  Counting it would name the helper's syscall
# twice and would also add a bare `syscall` entry that no grant models.
# The helper's own name carries the identity.
#
# ── WHAT IT DOES NOT SEE ─────────────────────────────────────────────
#
# A call reached through a function defined in another header (a helper
# in CpuTopology.h, say) is invisible here: this reads one file, not a
# call graph.  A call spelled through a function pointer, a macro, or a
# template parameter is invisible for the same reason.  The guard makes
# the common case — a syscall written literally in this header — the
# case that cannot drift, and says so rather than implying more.
#
# Exit status:
#   0 — derived set and declared set agree
#   1 — they disagree (missing grant, or grant with no call site)
#   2 — bad invocation, missing dependency, or the header shape changed
#       so much that neither set could be parsed

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-syscall-grant-coverage.sh — derive the hardening syscall grant set
from the call sites and compare it with the declared tuple.

Usage:
  check-syscall-grant-coverage.sh              # scan; exit 1 on drift
  check-syscall-grant-coverage.sh --list       # print both sets
  check-syscall-grant-coverage.sh --self-test  # plant drift, verify catch
  check-syscall-grant-coverage.sh -h | --help  # usage
USAGE
}

# The header under audit, and the tuple inside it.  Kept as variables so
# --self-test can point the same logic at a planted fixture.
HEADER_REL="include/crucible/warden/Hardening.h"
TUPLE_NAME="mint_hardening_syscall_grants"

# ── Derive the set the code issues ───────────────────────────────────
#
# Prints one syscall name per line, sorted and de-duplicated.  Comment
# lines are dropped first so that prose naming a syscall does not count
# as a call site.
derive_from_calls() {
    local file="$1"
    grep -vE '^[[:space:]]*(//|\*|/\*)' "$file" \
        | grep -oE '(::[a-z_][a-z0-9_]*\(|\b[a-z_][a-z0-9_]*_sys\()' \
        | sed -e 's/($//' -e 's/(//' -e 's/^:://' \
        | while IFS= read -r name; do
            case "$name" in
                # A *_sys helper stands for the syscall it wraps.
                *_sys) printf '%s\n' "${name%_sys}" ;;
                # Not a syscall: the raw multiplexer, and the C++ names
                # that share the shape of a global-scope call.
                syscall|new|delete|operator) ;;
                *) printf '%s\n' "$name" ;;
            esac
        done \
        | grep -xE "$(known_syscalls_alternation)" \
        | sort -u
}

# Only names the SyscallId catalog knows are treated as syscalls; any
# other `::foo(` in the header is ordinary C++.  Keeping this list here
# rather than deriving it from Per.h is deliberate: a typo in Per.h
# should not silently widen what this guard accepts.
known_syscalls_alternation() {
    printf '%s' 'sched_setaffinity|sched_getaffinity|sched_setattr|sched_getattr|mlock|mlock2|munlock|madvise|prctl|mmap|munmap|mprotect|open|openat|close|read|write|pread|pwrite|fsync|fdatasync|socket|connect|sendmsg|recvmsg|clone|execve|ptrace|capset|bpf|perf_event_open|futex|sched_yield'
}

# ── Read the set the tuple declares ──────────────────────────────────
#
# Prints one syscall name per line, sorted and de-duplicated.  The tuple
# spans many lines, so the region from the `using <TUPLE_NAME> =` line to
# its terminating `;` is isolated first.
derive_from_tuple() {
    local file="$1"
    awk -v name="$TUPLE_NAME" '
        $0 ~ ("using[[:space:]]+" name "[[:space:]]*=") { inside = 1 }
        inside { print }
        inside && /;[[:space:]]*$/ { inside = 0 }
    ' "$file" \
        | grep -oE 'SyscallId::[a-z_][a-z0-9_]*' \
        | sed 's/^SyscallId:://' \
        | sort -u
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --list)
        header="$root/$HEADER_REL"
        printf 'derived from call sites in %s:\n' "$HEADER_REL"
        derive_from_calls "$header" | sed 's/^/  /'
        printf 'declared by %s:\n' "$TUPLE_NAME"
        derive_from_tuple "$header" | sed 's/^/  /'
        exit 0
        ;;
    --self-test)
        # Three phases, each a tree planted under a temp root:
        #   1. agreeing sets              -> rc 0
        #   2. a call site with no grant  -> rc 1, names the syscall
        #   3. a grant with no call site  -> rc 1, names the syscall
        # Phase 1 failing means the parser stopped seeing one of the two
        # sets and the guard would pass on anything.  Phases 2 and 3
        # each cover one drift direction; a guard that only caught one
        # would still let the other class through.
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/$(dirname "$HEADER_REL")"
        planted="$tmp_root/$HEADER_REL"

        write_fixture() {
            # $1 = extra call site (may be empty), $2 = extra grant (may be empty)
            cat >"$planted" <<FIXTURE
#pragma once
// Synthetic Hardening.h shape for --self-test.
namespace crucible::warden {
inline int mlock2_sys(const void* a, unsigned long l, unsigned f) noexcept {
    return static_cast<int>(::syscall(SYS_mlock2, a, l, f));
}
inline void planted_apply() noexcept {
    (void)::sched_setaffinity(0, 0, nullptr);
    (void)::madvise(nullptr, 0, 0);
    (void)mlock2_sys(nullptr, 0, 0);
    ${1}
}
// A prose mention of ::prctl( must not count as a call site.
using ${TUPLE_NAME} =
    std::tuple<per<SyscallId::sched_setaffinity>,
               per<SyscallId::madvise>,
               per<SyscallId::mlock2>${2}>;
}  // namespace crucible::warden
FIXTURE
        }

        run_planted() {
            CRUCIBLE_SYSCALL_GRANT_TEST_ROOT="$tmp_root" \
                bash "${BASH_SOURCE[0]}" 2>"$1" && return 0 || return $?
        }

        # ── Phase 1: the two sets agree ──────────────────────────────
        write_fixture "" ""
        out="$(mktemp)"
        rc=0
        run_planted "$out" || rc=$?
        if [[ "$rc" -ne 0 ]]; then
            printf 'check-syscall-grant-coverage: SELF-TEST FAILED — agreeing sets reported drift (rc=%s).\n' "$rc" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' "$(cat "$out")" >&2
            rm -f "$out"; exit 2
        fi
        rm -f "$out"
        printf 'check-syscall-grant-coverage: self-test phase 1 passed — agreeing sets read clean.\n' >&2

        # ── Phase 2: a call site with no grant ───────────────────────
        write_fixture '(void)::sched_getaffinity(0, 0, nullptr);' ""
        out="$(mktemp)"
        rc=0
        run_planted "$out" || rc=$?
        if [[ "$rc" -ne 1 ]]; then
            printf 'check-syscall-grant-coverage: SELF-TEST FAILED — ungranted call site expected rc=1, got %s.\n' "$rc" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' "$(cat "$out")" >&2
            rm -f "$out"; exit 2
        fi
        if ! grep -q 'issued but not granted:.*sched_getaffinity' "$out"; then
            printf 'check-syscall-grant-coverage: SELF-TEST FAILED — ungranted call site not named in the diagnostic.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' "$(cat "$out")" >&2
            rm -f "$out"; exit 2
        fi
        rm -f "$out"
        printf 'check-syscall-grant-coverage: self-test phase 2 passed — a call site with no grant fails.\n' >&2

        # ── Phase 3: a grant with no call site ───────────────────────
        write_fixture "" ',
               per<SyscallId::ptrace>'
        out="$(mktemp)"
        rc=0
        run_planted "$out" || rc=$?
        if [[ "$rc" -ne 1 ]]; then
            printf 'check-syscall-grant-coverage: SELF-TEST FAILED — unbacked grant expected rc=1, got %s.\n' "$rc" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' "$(cat "$out")" >&2
            rm -f "$out"; exit 2
        fi
        if ! grep -q 'granted but never issued:.*ptrace' "$out"; then
            printf 'check-syscall-grant-coverage: SELF-TEST FAILED — unbacked grant not named in the diagnostic.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' "$(cat "$out")" >&2
            rm -f "$out"; exit 2
        fi
        rm -f "$out"
        printf 'check-syscall-grant-coverage: self-test phase 3 passed — a grant with no call site fails.\n' >&2

        printf 'check-syscall-grant-coverage: self-test passed — all 3 phases green.\n' >&2
        exit 0
        ;;
    "") ;;
    *) printf 'check-syscall-grant-coverage: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

scan_root="${CRUCIBLE_SYSCALL_GRANT_TEST_ROOT:-$root}"
header="$scan_root/$HEADER_REL"

if [[ ! -f "$header" ]]; then
    printf 'check-syscall-grant-coverage: %s not found under %s\n' "$HEADER_REL" "$scan_root" >&2
    exit 2
fi

issued="$(derive_from_calls "$header" || true)"
granted="$(derive_from_tuple "$header" || true)"

if [[ -z "$granted" ]]; then
    printf 'check-syscall-grant-coverage: could not read any entry out of %s.  The tuple was renamed or reshaped; update TUPLE_NAME / derive_from_tuple rather than leaving the guard reading nothing.\n' \
        "$TUPLE_NAME" >&2
    exit 2
fi
if [[ -z "$issued" ]]; then
    printf 'check-syscall-grant-coverage: could not read any call site out of %s.  The call spellings changed; update derive_from_calls rather than leaving the guard reading nothing.\n' \
        "$HEADER_REL" >&2
    exit 2
fi

missing="$(comm -23 <(printf '%s\n' "$issued") <(printf '%s\n' "$granted") | tr '\n' ' ')"
extra="$(comm -13 <(printf '%s\n' "$issued") <(printf '%s\n' "$granted") | tr '\n' ' ')"
missing="${missing% }"
extra="${extra% }"

status=0
if [[ -n "$missing" ]]; then
    printf 'SYSCALL-GRANT issued but not granted: %s\n' "$missing" >&2
    status=1
fi
if [[ -n "$extra" ]]; then
    printf 'SYSCALL-GRANT granted but never issued: %s\n' "$extra" >&2
    status=1
fi

if [[ "$status" -ne 0 ]]; then
    cat >&2 <<HINT

${TUPLE_NAME} in ${HEADER_REL} disagrees with the call sites
beside it.  The tuple is the auditable claim about what this code does
to the kernel, so a smaller tuple understates the claim and a larger one
overstates it.

  issued but not granted — add per<SyscallId::<name>> to the tuple, add a
    family_tier_v static_assert beside it, bump the tuple_size assert,
    and extend test/test_fixy_v_180_hardening_syscall_grants.cpp.  If the
    enumerator does not exist yet, append it at the next free ordinal in
    include/crucible/fixy/syscall/Per.h (ordinals are append-only — an
    existing one keeps its value so federation cache keys never drift).

  granted but never issued — either the call was removed and the grant
    should go with it, or the call moved out of this header.  A grant
    reached through another header is invisible to this guard; if that is
    the case, say so in a comment beside the entry so the next reader
    does not delete a live grant.
HINT
    exit 1
fi

printf 'check-syscall-grant-coverage: clean — %s names exactly the syscalls issued in %s.\n' \
    "$TUPLE_NAME" "$HEADER_REL" >&2
exit 0
