#!/usr/bin/env bash
# check-syscall-capability.sh — syscall capability discipline.
#
# Closes the regression-prevention surface for a defect in which every
# syscall path bypassed the effect-row capabilities, so the hot path could
# invoke netlink or sysctl by accident.  The original fix wrapped
# syscall sites with proper effects:: capability admission; this guard
# prevents new
# unreviewed syscall sites from sneaking in.
#
# ── DISCIPLINE ────────────────────────────────────────────────────────
#
# Every direct invocation of a Linux syscall (or libc thin wrapper) in
# production code (include/, src/, vessel/) MUST either carry an
# in-statement `// SYSCALL-CAP-OK: <reason>` marker or appear in
# scripts/syscall-capability-allowlist.txt.  Each allowlist entry is
#
#     path:<call text>  — <effects::* cap proof + capability discipline note>
#
# where <call text> is the trimmed source of the call line with its
# trailing `//` comment removed.  That is a CONTENT KEY, not a line
# number: the entry survives every edit above the call and goes stale
# only when the call itself changes.  Two calls with identical text in
# one file share one entry.  A stale entry (no live call with that text)
# is exit 2, so the allowlist drains in lockstep with the code.
#
# `--emit-keys` prints a template entry for every live site that is
# neither marked nor allowlisted, with the syscall(s) it invokes already
# named, so the human writes only the capability claim.
#
# Adding a NEW syscall site WITHOUT a marker or an allowlist entry fails
# CI.  This forces every new syscall introduction through the review
# surface of the effect-row capabilities in CLAUDE.md §1.
#
# Inline suppression (rare): `// SYSCALL-CAP-OK: <reason>` on the call
# statement exempts that statement.  Use when the syscall site is
# structurally part of a Ctx-bound mint and the capability admission is
# verifiable at the static_assert level (not a substitute for the allowlist).
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
#   0 — clean (no NEW syscall sites beyond the allowlist, no stale entries)
#   1 — at least one violation (takes precedence over stale)
#   2 — stale allowlist entry, bad invocation, or missing dependency

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-syscall-capability.sh — syscall capability discipline.

Usage:
  check-syscall-capability.sh              # scan; exit 1 on violation, 2 on stale entry
  check-syscall-capability.sh --emit-keys  # print template allowlist entries for unlisted sites
  check-syscall-capability.sh --self-test  # plant a violation, verify catch
  check-syscall-capability.sh -h | --help  # usage

Suppression:
  // SYSCALL-CAP-OK: <reason>              on the call statement — exempts it
  scripts/syscall-capability-allowlist.txt:
    path:<call text>  — <effects::* cap proof>   — exempts every call whose
                                            trimmed, comment-stripped source
                                            equals <call text> (content key)
    # comment                              — ignored

CLAUDE.md §1 — every syscall site must hold an effects::* capability
(Init / IO / Block / Bg) admitted through a Ctx-bound boundary.
USAGE
}

mode=scan
case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --emit-keys) mode=emit ;;
    --self-test)
        # Plant ONE violation in a synthetic file under a temp root.
        # The scanner must flag it; otherwise the regex is broken.
        # Also plant ONE allowlisted call, ONE inline-suppressed call,
        # and ONE commented call to verify the suppression mechanisms.
        # The three ::socket calls carry DISTINCT arguments so each maps
        # to exactly one content key.
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/src/planted" "$tmp_root/scripts"
        cat >"$tmp_root/src/planted/planted_syscall.cpp" <<'PLANTED'
// Synthetic syscall-capability fixture for --self-test.
#include <sys/socket.h>
namespace crucible::planted {
inline int planted_drift() {
    return ::socket(1, 0, 0);  // FLAGGED — must be caught
}
inline int planted_allowlisted() {
    return ::socket(2, 0, 0);  // ALLOWLISTED — must NOT be caught
}
inline int planted_suppressed() {
    return ::socket(3, 0, 0);  // SYSCALL-CAP-OK: synthetic --self-test marker
}
inline int planted_in_comment() {
    // ::socket(4, 0, 0);      // commented — must NOT be caught
    return 0;
}
struct PlantedTagDoc final {};  // ::socket(2) doc-comment — must NOT be caught
// ── Unqualified branch ───────────────────────────────────────────────
// `::` is a convention, not a requirement.  A syscall spelled without
// it hits the same kernel, and was invisible to this guard until the
// unqualified branch landed.  Assertions below resolve these lines by
// grepping for the marker identifiers, so the fixture can grow without
// desyncing the hardcoded line numbers above.
inline int planted_unqualified_flagged() {
    return sched_getaffinity(0, 0, nullptr);  // FLAGGED — must be caught
}
// Boundary: `close` is an ordinary C++ member spelling in this tree, so
// the unqualified branch deliberately omits it.  Such a name still has
// to be spelled `::close(` to be caught.
struct PlantedAmbiguousSkip final {
    int fd_ = -1;
    void planted_ambiguous_ok() { close(fd_); }  // must NOT be caught
};
// A DECLARATION named after a syscall is not a call site: the return
// type puts an identifier char immediately before the name, which the
// call-position requirement rejects.
struct PlantedDeclSkip final {
    int planted_decl_ok_ioctl(unsigned long req) const noexcept;
    int ioctl(unsigned long req) const noexcept;  // planted_decl_ok_bare
};
// A member call through `.` or `->` is not a syscall site either.
inline int planted_member_ok(PlantedDeclSkip& d) { return d.ioctl(0); }
}  // namespace crucible::planted
PLANTED
        # The allowlist entry is CONTENT-KEYED on the second ::socket call.
        cat >"$tmp_root/scripts/syscall-capability-allowlist.txt" <<'ALLOW'
# self-test grandfathered entry (content-keyed)
src/planted/planted_syscall.cpp:return ::socket(2, 0, 0);  — effects::Init proof (synthetic test)
ALLOW
        result_file="$(mktemp)"
        sc_fail() {
            printf 'check-syscall-capability: SELF-TEST FAILED — %s\n' "$1" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        }
        if CRUCIBLE_SYSCALL_CAP_TEST_ROOT="$tmp_root" \
           bash "${BASH_SOURCE[0]}" 2>"$result_file"; then
            sc_fail "planted violation not caught."
        fi
        # The planted_drift line (line 5) must be flagged.
        grep -qF 'planted_syscall.cpp:5' "$result_file" || sc_fail "expected diagnostic for line 5 missing."
        # The allowlisted line (line 8) must NOT be flagged.
        if grep -qF 'planted_syscall.cpp:8' "$result_file"; then sc_fail "allowlist entry leaked."; fi
        # The inline-suppressed line (line 11) must NOT be flagged.
        if grep -qF 'planted_syscall.cpp:11' "$result_file"; then sc_fail "SYSCALL-CAP-OK marker leaked."; fi
        # The commented-out call (line 14) must NOT be flagged.
        if grep -qF 'planted_syscall.cpp:14' "$result_file"; then sc_fail "comment line leaked through filter."; fi
        # The trailing-comment doc tag (line 17) must NOT be flagged — the
        # `::socket(2)` token lives only in the trailing comment.
        if grep -qF 'planted_syscall.cpp:17' "$result_file"; then sc_fail "trailing-comment doc-tag leaked through filter."; fi
        # ── Unqualified branch assertions ────────────────────────────
        # Line numbers resolved from the fixture by marker identifier,
        # so appending further cases cannot silently desync them.
        planted_file="$tmp_root/src/planted/planted_syscall.cpp"
        sc_line_of() { grep -n -- "$1" "$planted_file" | head -1 | cut -d: -f1; }
        unq_line="$(sc_line_of 'return sched_getaffinity')"
        grep -qF "planted_syscall.cpp:${unq_line}" "$result_file" || \
            sc_fail "unqualified syscall at line ${unq_line} not caught — the unqualified branch is dead."
        for marker in planted_ambiguous_ok planted_decl_ok_ioctl planted_decl_ok_bare planted_member_ok; do
            skip_line="$(sc_line_of "$marker")"
            if grep -qF "planted_syscall.cpp:${skip_line}" "$result_file"; then
                sc_fail "boundary case ${marker} (line ${skip_line}) was flagged — the unqualified branch is over-matching."
            fi
        done
        # The violation message carries the content key the human would
        # paste into the allowlist.
        grep -qF 'Allowlist key: src/planted/planted_syscall.cpp:return ::socket(1, 0, 0);' "$result_file" || \
            sc_fail "the violation did not print its content key."

        # ── --emit-keys prints a template for each unlisted site ─────
        emit_file="$(mktemp)"
        CRUCIBLE_SYSCALL_CAP_TEST_ROOT="$tmp_root" bash "${BASH_SOURCE[0]}" --emit-keys >"$emit_file" 2>"$result_file" || \
            sc_fail "--emit-keys exited non-zero."
        grep -qF 'src/planted/planted_syscall.cpp:return ::socket(1, 0, 0);  — effects::? via ? (socket)' "$emit_file" || \
            { cat "$emit_file" >>"$result_file"; sc_fail "--emit-keys did not print the template for the flagged ::socket site."; }
        grep -qF 'src/planted/planted_syscall.cpp:return sched_getaffinity(0, 0, nullptr);  — effects::? via ? (sched_getaffinity)' "$emit_file" || \
            { cat "$emit_file" >>"$result_file"; sc_fail "--emit-keys did not print the template for the unqualified site."; }
        if grep -qF '::socket(2, 0, 0)' "$emit_file"; then sc_fail "--emit-keys printed an already-allowlisted site."; fi
        if grep -qF '::socket(3, 0, 0)' "$emit_file"; then sc_fail "--emit-keys printed a marker-suppressed site."; fi
        rm -f "$emit_file"

        # ── Phase 2: drift-proofing — the content key survives a line shift
        # Two blank lines above every call; both live sites allowlisted so
        # the scan reaches a clean exit 0.
        cat >"$tmp_root/src/planted/planted_syscall.cpp" <<'PLANTED'


// Synthetic syscall-capability fixture for --self-test (line-shifted).
#include <sys/socket.h>
namespace crucible::planted {
inline int planted_drift() {
    return ::socket(1, 0, 0);
}
inline int planted_allowlisted() {
    return ::socket(2, 0, 0);
}
}  // namespace crucible::planted
PLANTED
        cat >"$tmp_root/scripts/syscall-capability-allowlist.txt" <<'ALLOW'
# self-test live entries (content-keyed; survive the line shift)
src/planted/planted_syscall.cpp:return ::socket(1, 0, 0);  — effects::Init proof (synthetic)
src/planted/planted_syscall.cpp:return ::socket(2, 0, 0);  — effects::Init proof (synthetic)
ALLOW
        drift_rc=0
        CRUCIBLE_SYSCALL_CAP_TEST_ROOT="$tmp_root" bash "${BASH_SOURCE[0]}" 2>"$result_file" || drift_rc=$?
        [[ "$drift_rc" -eq 0 ]] || sc_fail "phase 2 — content key did NOT survive the line shift (exit $drift_rc, want 0)."

        # ── Phase 3: stale-entry detection ───────────────────────────
        cat >"$tmp_root/scripts/syscall-capability-allowlist.txt" <<'ALLOW'
src/planted/planted_syscall.cpp:return ::socket(1, 0, 0);  — effects::Init proof (synthetic)
src/planted/planted_syscall.cpp:return ::socket(2, 0, 0);  — effects::Init proof (synthetic)
# stale: no such call text in the file
src/planted/planted_syscall.cpp:return ::socket(99, 0, 0);  — effects::Init proof (synthetic, stale)
ALLOW
        stale_rc=0
        CRUCIBLE_SYSCALL_CAP_TEST_ROOT="$tmp_root" bash "${BASH_SOURCE[0]}" 2>"$result_file" || stale_rc=$?
        [[ "$stale_rc" -eq 2 ]] || sc_fail "phase 3 — expected exit 2 on a stale entry, got $stale_rc."
        stale_emitted="$(grep -c '^SYSCALL-CAP stale:' "$result_file" || true)"
        [[ "$stale_emitted" -eq 1 ]] || sc_fail "phase 3 — expected exactly 1 stale diagnostic, got $stale_emitted."
        grep -qF '::socket(99, 0, 0)' "$result_file" || sc_fail "phase 3 — the stale entry was not named."

        rm -f "$result_file"
        printf 'check-syscall-capability: self-test passed — qualified and unqualified drift caught with content keys printed, allowlist + marker + comment filters honoured, boundary cases clean, --emit-keys templates correct, keys survive a line shift, stale entries flagged.\n' >&2
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
# Match the global-scope `::<syscall>(` form ONLY.  The negative
# lookbehind `(?<![a-zA-Z_0-9])` excludes namespace-qualified calls
# (`std::simd::select`, `crucible::foo::open`, etc.) — those are not
# POSIX syscalls even when they share the name.
#
# The syscall list covers every syscall path: the
# network family, scheduler, memory, file I/O, process, time, fd
# manipulation, polling, sync, signal, BPF, random.  Each entry below
# represents a kernel-state observation or mutation that requires
# explicit effects::* capability admission.
syscall_names='socket|bind|listen|connect|accept|send|sendto|sendmsg|recv|recvfrom|recvmsg|shutdown|setsockopt|getsockopt|mmap|munmap|mremap|mlock|mlock2|munlock|munlockall|mlockall|madvise|mincore|mprotect|brk|sbrk|sched_setaffinity|sched_getaffinity|sched_setattr|sched_getattr|sched_yield|prctl|syscall|epoll_create1|epoll_ctl|epoll_wait|eventfd|ioctl|open|openat|close|read|write|pread|pwrite|readv|writev|fsync|fdatasync|stat|fstat|lstat|unlink|rename|mkdir|rmdir|fork|vfork|clone|execve|waitpid|kill|sigaction|signal|sigprocmask|clock_gettime|clock_settime|nanosleep|gettimeofday|pipe|pipe2|dup|dup2|dup3|fcntl|flock|poll|select|pselect|futex|bpf|getrandom|getrlimit|setrlimit|chmod|chown|access|faccessat'

# ── Branch 2: the UNQUALIFIED call form ──────────────────────────────
# `::name(` is a convention, not a requirement — `sched_getaffinity(0,
# ...)` compiles and hits the very same kernel.  Branch 1's leading
# `::` made every such site INVISIBLE to this guard: no allowlist
# entry, no marker, clean exit.  That is a class of bypass, not one
# site.
#
# The unqualified form cannot use the full name list.  Roughly half of
# it — open / close / read / write / send / recv / accept / select /
# poll / connect / bind / listen / socket / stat / kill / signal /
# fork / clone / access / rename / pipe / dup / brk — are ordinary C++
# member and free-function spellings in this tree (`handle.close()`,
# `SocketFd socket() const`, `Session::send(...)`, and clang-format
# wrapping a return type onto its own line puts a bare `accept(` at
# column 5).  Matching those unqualified would bury the signal.
#
# So branch 2 carries only the KERNEL-ONLY spellings: names that have
# no plausible C++ identifier collision.  A site using an ambiguous
# name still has to spell `::` to be caught — which is the existing
# house convention anyway, and is what every allowlist entry uses.
unqualified_syscall_names='sendto|sendmsg|recvfrom|recvmsg|setsockopt|getsockopt|mmap|munmap|mremap|mlock|mlock2|munlock|munlockall|mlockall|madvise|mincore|mprotect|sched_setaffinity|sched_getaffinity|sched_setattr|sched_getattr|sched_yield|prctl|syscall|epoll_create1|epoll_ctl|epoll_wait|eventfd|ioctl|openat|pread|pwrite|readv|writev|fsync|fdatasync|fstat|lstat|unlink|mkdir|rmdir|vfork|execve|waitpid|sigaction|sigprocmask|clock_gettime|clock_settime|nanosleep|gettimeofday|pipe2|dup2|dup3|fcntl|flock|pselect|futex|bpf|getrandom|getrlimit|setrlimit|chmod|chown|faccessat'

# Divergence guard: every branch-2 name must also be a branch-1 name,
# or the two lists have drifted and a syscall is covered unqualified
# but not qualified.
while IFS= read -r _n; do
    case "|${syscall_names}|" in
        *"|${_n}|"*) ;;
        *) printf 'check-syscall-capability: internal error — %s is in unqualified_syscall_names but not syscall_names.\n' "$_n" >&2
           exit 2 ;;
    esac
done < <(printf '%s\n' "$unqualified_syscall_names" | tr '|' '\n')

# Branch 2 requires the name to sit in CALL position: at line start, or
# right after one of `; { } ( , = & | ! ? :` or `return`.  That keeps
# out declarations (`SocketFd socket() const` — preceded by an
# identifier) while still catching a bare statement call.  The
# lookbehind additionally rejects `.name(`, `->name(` and `ns::name(`.
candidate_pattern="(?<![a-zA-Z_0-9])::(${syscall_names})\s*\(|(?:^|[;{}(,=&|!?:]|\breturn)\s*(?<![a-zA-Z_0-9:.>])(${unqualified_syscall_names})\s*\("

# ── Allowlist (content-keyed) ────────────────────────────────────────
# Each entry: `path:<call text>  — <prose>`.  The key is everything before
# the em dash, trailing whitespace stripped.  Keys are collected once.
allow_keys_file="$(mktemp)"
live_set_file="$(mktemp)"
trap 'rm -f "$allow_keys_file" "$live_set_file"' EXIT
if [[ -f "$allowlist" ]]; then
    while IFS= read -r entry; do
        trimmed="${entry#"${entry%%[![:space:]]*}"}"
        case "$trimmed" in ''|'#'*) continue ;; esac
        key="${trimmed%%—*}"
        key="${key%"${key##*[![:space:]]}"}"
        [[ -n "$key" ]] && printf '%s\n' "$key" >>"$allow_keys_file"
    done <"$allowlist"
fi

allowlisted() {
    grep -Fxq -- "$1" "$allow_keys_file" 2>/dev/null
}

# The syscall(s) a code line invokes, for the --emit-keys template.
# `::syscall(SYS_x, ...)` names x, not "syscall".
names_of() {
    local code="$1" names
    names="$(printf '%s\n' "$code" | rg -oP 'SYS_[A-Za-z0-9_]+' | sed 's/^SYS_//' || true)"
    names+=" $(printf '%s\n' "$code" | rg -oP "$candidate_pattern" | rg -oP "(${syscall_names})(?=\s*\()" | rg -v '^syscall$' || true)"
    printf '%s' "$names" | tr ' ' '\n' | rg -v '^$' | sort -u | tr '\n' ' ' | sed 's/ $//'
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

    # Inline suppression — `// SYSCALL-CAP-OK: <reason>`.  The marker scopes to
    # the call statement, not the line: a reformat may wrap a long argument
    # list so the trailing comment lands below the syscall token.  Scan to the
    # first line bearing ';' or ending in '}' (that line included), so a later
    # statement's marker cannot leak backwards.
    sc_suppressed=0
    sc_probe=$line
    sc_limit=$((line + 12))
    while (( sc_probe <= sc_limit )); do
        sc_text="$(sed -n "${sc_probe}p" "$file" 2>/dev/null)"
        case "$sc_text" in
            *'SYSCALL-CAP-OK'*) sc_suppressed=1; break ;;
        esac
        case "$sc_text" in
            *';'*) break ;;
        esac
        case "${sc_text%"${sc_text##*[![:space:]]}"}" in
            *'}') break ;;
        esac
        sc_probe=$((sc_probe + 1))
    done
    (( sc_suppressed )) && continue

    # Trailing-comment strip — the regex can match a syscall token that
    # lives only inside a `//` comment (a tag-struct or enumerator
    # doc-comment naming the syscall, e.g. `struct Fsync {};  // ::fsync`,
    # or an `#include <fcntl.h>  // ::open(...)` annotation).  Re-test the
    # code part: if the token survives only in the comment, this is not a
    # call site.  Mirrors check-fixy-spawn-discipline.sh's strip.
    code="${text%%//*}"
    if ! printf '%s' "$code" | rg -qP -- "$candidate_pattern"; then
        continue
    fi

    rel="${file#"$scan_root"/}"

    # The content key: the code part, trimmed at both ends.
    key_text="${code#"${code%%[![:space:]]*}"}"
    key_text="${key_text%"${key_text##*[![:space:]]}"}"
    key="$rel:$key_text"
    printf '%s\n' "$key" >>"$live_set_file"

    if allowlisted "$key"; then
        continue
    fi

    if [[ "$mode" == emit ]]; then
        printf '%s  — effects::? via ? (%s)\n' "$key" "$(names_of "$code")"
        continue
    fi

    printf 'SYSCALL-CAP violation: %s:%s — bare Linux syscall site missing effects::* capability admission.  Allowlist key: %s\n' \
        "$rel" "$line" "$key" >&2
    violation_count=$((violation_count + 1))
done < <(
    rg -nP \
       --no-heading \
       --type=cpp \
       --glob '!build*/**' \
       --glob '!cmake-build-*/**' \
       --glob '!third_party/**' \
       --glob '!external/**' \
       --glob '!vendor/**' \
       --glob '!test/**' \
       --glob '!bench/**' \
       --glob '!examples/**' \
       "$candidate_pattern" "${scan_dirs[@]}" 2>/dev/null || true
)

[[ "$mode" == emit ]] && exit 0

# ── Outcome (violations — take precedence over stale) ────────────────
if [[ "$violation_count" -ne 0 ]]; then
    cat >&2 <<HINT

check-syscall-capability detected ${violation_count} new Linux syscall
site(s) outside the allowlist.  CLAUDE.md §1 requires
every syscall invocation to hold a proper effects::* capability
(Init / IO / Block / Bg), admitted through a Ctx-bound boundary.

Remediations, in order of preference:

  (1) Route the syscall through a §XXI mint factory that admits the
      right Ctx row (e.g. mint_hardening for sched_setattr,
      mint_hot_region_registry_handle for mlock).

  (2) Wrap the call in a function that takes an effects::* cap-tag
      parameter and confirms the row at the type level.

  (3) Annotate the statement with '// SYSCALL-CAP-OK: <reason>' for
      structurally-justified exceptions (e.g. inside a mint body
      where the requires-clause already confirmed the capability).

  (4) Add the printed 'Allowlist key' to
      scripts/syscall-capability-allowlist.txt, followed by
      '  — <effects::* cap proof>'.  \`--emit-keys\` prints a template
      for every unlisted site with its syscall(s) named.
HINT
    exit 1
fi

# ── Outcome (stale allowlist entries) ────────────────────────────────
# Every allowlist key must name a LIVE call.  A key with no live call
# points at code that was rewritten or removed; it must go, or it could
# re-grandfather a future call that happens to have the same text.
stale_count=0
while IFS= read -r key; do
    [[ -z "$key" ]] && continue
    if ! grep -Fxq -- "$key" "$live_set_file" 2>/dev/null; then
        printf 'SYSCALL-CAP stale: %s — no live syscall call with this text in the file (rewritten or removed); remove the entry.\n' "$key" >&2
        stale_count=$((stale_count + 1))
    fi
done <"$allow_keys_file"

if [[ "$stale_count" -ne 0 ]]; then
    cat >&2 <<HINT

check-syscall-capability detected ${stale_count} stale allowlist entr(y/ies).
Each names a call text that no longer exists in its file.  Prune them
from scripts/syscall-capability-allowlist.txt so the guard regains drift
coverage and cannot re-grandfather a future site with the same text.
HINT
    exit 2
fi

printf 'check-syscall-capability: clean — no new syscall sites without capability admission, no stale allowlist entries.\n' >&2
exit 0
