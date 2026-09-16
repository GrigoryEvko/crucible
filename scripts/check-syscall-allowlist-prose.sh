#!/usr/bin/env bash
# check-syscall-allowlist-prose.sh — hold the syscall allowlist's prose
# to the code it names.
#
# ── WHY THIS EXISTS ──────────────────────────────────────────────────
#
# scripts/syscall-capability-allowlist.txt grandfathers bare-syscall
# sites.  Each entry is `path:<call text>  — <effects::* cap proof + note>`,
# where <call text> is the trimmed, comment-stripped source of the call
# line (a content key, immune to line shifts), and the note is the whole
# point: check-syscall-capability.sh only asks whether a key exists, never
# what the entry says about it.
#
# A key can therefore stay valid while the prose beside it rots: the
# guard stays green, the audit surface reads as reviewed, and the
# sentence a reviewer relies on is false.  The entry for
# src/cntp/IncastControl.cpp said "TCP_QUICKACK setsockopt" for a call
# that sets TCP_RTO_MIN_US, and TCP_QUICKACK appears nowhere in the
# tree.  Nothing noticed, because nothing was looking.
#
# ── WHAT IS CHECKED ──────────────────────────────────────────────────
#
# Four properties, each mechanical:
#
#   (A) resolvable key    — the file exists and some line's comment-stripped
#                           trimmed source equals the call text.
#   (B) syscall agreement — the syscall invoked at that line is named in
#                           the prose.  `::syscall(SYS_x, ...)` counts as
#                           x, so a raw-multiplexer site has to name the
#                           syscall it multiplexes rather than the word
#                           "syscall".
#   (C) token agreement   — every SHOUTING_SNAKE token in the prose
#                           (TCP_QUICKACK, MADV_HUGEPAGE, O_NOFOLLOW ...)
#                           must appear literally somewhere in that file.
#                           A constant the file never mentions is prose
#                           about some other code.
#   (D) symbol agreement  — every `Ns::member` name in the prose's `via`
#                           clause must appear literally in that file.
#                           Catches an entry that names a function the
#                           file does not have.
#
# ── WHAT IS NOT CHECKED ──────────────────────────────────────────────
#
# The effects::* capability claim itself.  Deciding whether a call site
# really holds effects::IO rather than effects::Init needs the type
# system, not a grep, and this guard does not pretend otherwise.  (D)
# confirms a named symbol EXISTS in the file, not that the call sits
# inside it — a helper called from three functions will satisfy (D) for
# any of the three.  Prose that is merely vague rather than false
# ("effects::Init via Hardening::apply", no syscall named) is caught by
# (B) only when it omits the syscall; a wrong-but-plausible capability
# tier passes everything here.
#
# Exit status:
#   0 — every entry's prose agrees with the code it names
#   1 — at least one entry's prose contradicts the code
#   2 — bad invocation, missing dependency, or an unreadable allowlist

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-syscall-allowlist-prose.sh — hold syscall-capability-allowlist.txt
prose to the code it names.

Usage:
  check-syscall-allowlist-prose.sh              # scan; exit 1 on rot
  check-syscall-allowlist-prose.sh --self-test  # plant rot, verify catch
  check-syscall-allowlist-prose.sh -h | --help  # usage
USAGE
}

# Longest-first so recvmsg wins over recv and sched_setaffinity over
# nothing shorter.  Order matters: a leftmost-first PCRE alternation
# would otherwise report `recv` for a `::recvmsg(` call and then blame
# prose that correctly says recvmsg.
SYSCALL_ALT='sched_setaffinity|sched_getaffinity|epoll_create1|clock_settime|clock_gettime|perf_event_open|sched_setattr|sched_getattr|gettimeofday|sigprocmask|munlockall|setsockopt|getsockopt|faccessat|fdatasync|epoll_wait|sched_yield|recvfrom|recvmsg|sendmsg|shutdown|nanosleep|sigaction|epoll_ctl|getrandom|getrlimit|setrlimit|mprotect|mlockall|mincore|madvise|munlock|eventfd|waitpid|openat|execve|unlink|rename|pselect|munmap|mremap|connect|listen|accept|socket|sendto|mlock2|prctl|ioctl|mkdir|rmdir|fsync|pread|write|pwrite|readv|writev|fstat|lstat|fcntl|flock|futex|chmod|chown|access|clone|vfork|pipe2|dup2|dup3|mlock|mmap|bind|send|recv|open|close|read|stat|fork|kill|signal|poll|select|pipe|dup|brk|sbrk|bpf|syscall'

resolve_syscalls() {
    # $1 = the source line.  Prints one effective syscall name per line.
    local code="$1"
    # `::syscall(SYS_x, ...)` — the effective syscall is x, not "syscall".
    printf '%s\n' "$code" | grep -oP 'SYS_[A-Za-z0-9_]+' | sed 's/^SYS_//'
    printf '%s\n' "$code" \
        | grep -oP "(?<![A-Za-z_0-9])(::)?(${SYSCALL_ALT})\s*\(" \
        | grep -oP "(${SYSCALL_ALT})" \
        | grep -v '^syscall$'
}

scan() {
    local allowlist="$1" scan_root="$2" rc=0
    local entry key prose path lineno file code name token sym

    while IFS= read -r entry; do
        case "$entry" in ''|\#*) continue ;; esac
        key="${entry%%—*}"
        key="${key%"${key##*[![:space:]]}"}"
        prose="${entry#*—}"
        path="${key%%:*}"
        call_text="${key#*:}"
        file="$scan_root/$path"

        # (A) resolvable key — the file exists and holds a line whose
        # comment-stripped, trimmed source is exactly the call text.
        if [[ ! -f "$file" ]]; then
            printf 'PROSE-ROT %s — names a file that does not exist.\n' "$key" >&2
            rc=1
            continue
        fi
        code=""
        while IFS= read -r candidate || [[ -n "$candidate" ]]; do
            local stripped_line="${candidate%%//*}"
            stripped_line="${stripped_line#"${stripped_line%%[![:space:]]*}"}"
            stripped_line="${stripped_line%"${stripped_line##*[![:space:]]}"}"
            if [[ "$stripped_line" == "$call_text" ]]; then
                code="$candidate"
                break
            fi
        done < <(grep -F -- "$call_text" "$file" || true)
        if [[ -z "$code" ]]; then
            printf 'PROSE-ROT %s — no line in %s has this call text.\n' "$key" "$path" >&2
            rc=1
            continue
        fi

        # (B) syscall agreement
        local names hit=0 any=0
        names="$(resolve_syscalls "$code" | sort -u)"
        if [[ -n "$names" ]]; then
            any=1
            while IFS= read -r name; do
                [[ -z "$name" ]] && continue
                # Word boundary on the left only, so prose may name the
                # syscall inside a wrapper's identifier (open -> open_read).
                if printf '%s' "$prose" | grep -qP "(?<![A-Za-z_0-9])${name}"; then
                    hit=1
                    break
                fi
            done <<<"$names"
            if [[ "$hit" -eq 0 ]]; then
                printf 'PROSE-ROT %s — prose names no syscall; the line calls: %s\n' \
                    "$key" "$(printf '%s' "$names" | tr '\n' ' ')" >&2
                printf '    prose: %s\n' "$(printf '%s' "$prose" | sed 's/^ *//')" >&2
                printf '    code : %s\n' "$(printf '%s' "$code" | sed 's/^ *//' | cut -c1-100)" >&2
                rc=1
            fi
        fi
        (( any )) || true

        # (C) SHOUTING_SNAKE token agreement
        while IFS= read -r token; do
            [[ -z "$token" ]] && continue
            if ! grep -qF -- "$token" "$file"; then
                printf 'PROSE-ROT %s — prose names %s, which appears nowhere in %s.\n' \
                    "$key" "$token" "$path" >&2
                printf '    prose: %s\n' "$(printf '%s' "$prose" | sed 's/^ *//')" >&2
                rc=1
            fi
        done < <(printf '%s\n' "$prose" | grep -oP '\b[A-Z][A-Z0-9]*(_[A-Z0-9]+)+\b' | sort -u)

        # (D) `via Ns::member` symbol agreement.
        #
        # The name must appear followed by a non-identifier character, so
        # that naming `open_write` does not pass on a file whose only
        # symbol is `open_write_truncate`.  A plain substring test let
        # exactly that through.
        while IFS= read -r sym; do
            [[ -z "$sym" ]] && continue
            if ! grep -qP -- "\Q${sym}\E(?![A-Za-z_0-9])" "$file"; then
                printf 'PROSE-ROT %s — prose names symbol %s, which appears nowhere in %s.\n' \
                    "$key" "$sym" "$path" >&2
                printf '    prose: %s\n' "$(printf '%s' "$prose" | sed 's/^ *//')" >&2
                rc=1
            fi
        done < <(printf '%s\n' "$prose" \
                 | grep -oP 'via\s+\K[A-Za-z_][A-Za-z0-9_]*(::[~A-Za-z_][A-Za-z0-9_]*)+' \
                 | tr ':' '\n' | grep -v '^$' | sort -u)
    done < "$allowlist"

    return "$rc"
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test)
        # Four phases, one per checked property, each planted and then
        # removed.  A phase that passes on the rotted tree means the
        # property is not actually checked and the guard is decoration.
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/src/planted" "$tmp_root/scripts"
        cat >"$tmp_root/src/planted/planted_prose.cpp" <<'PLANTED'
// Synthetic fixture for check-syscall-allowlist-prose.sh --self-test.
#include <sys/socket.h>
namespace crucible::planted {
struct PlantedSock final {
    int apply(int fd) noexcept {
        return ::setsockopt(fd, 6, TCP_PLANTED_OPT, nullptr, 0);
    }
};
}  // namespace crucible::planted
PLANTED
        good='src/planted/planted_prose.cpp:return ::setsockopt(fd, 6, TCP_PLANTED_OPT, nullptr, 0);  — effects::IO via PlantedSock::apply (TCP_PLANTED_OPT setsockopt)'
        al="$tmp_root/scripts/syscall-capability-allowlist.txt"

        phase() {  # $1 = label, $2 = entry, $3 = expected rc, $4 = expected diagnostic substring
            printf '# planted\n%s\n' "$2" >"$al"
            local out rc=0
            out="$(mktemp)"
            scan "$al" "$tmp_root" 2>"$out" || rc=$?
            if [[ "$rc" -ne "$3" ]]; then
                printf 'check-syscall-allowlist-prose: SELF-TEST FAILED — %s expected rc=%s, got %s.\n' \
                    "$1" "$3" "$rc" >&2
                printf '── scanner stderr ───\n%s\n────────────────────\n' "$(cat "$out")" >&2
                rm -f "$out"; exit 2
            fi
            if [[ -n "$4" ]] && ! grep -qF -- "$4" "$out"; then
                printf 'check-syscall-allowlist-prose: SELF-TEST FAILED — %s diagnostic missing %s.\n' \
                    "$1" "$4" >&2
                printf '── scanner stderr ───\n%s\n────────────────────\n' "$(cat "$out")" >&2
                rm -f "$out"; exit 2
            fi
            rm -f "$out"
            printf 'check-syscall-allowlist-prose: self-test %s passed.\n' "$1" >&2
        }

        phase 'phase 0 (honest entry reads clean)' "$good" 0 ''
        phase 'phase A (unresolvable key)' \
            'src/planted/planted_prose.cpp:return ::setsockopt(fd, 6, NO_SUCH_LINE, nullptr, 0);  — effects::IO via PlantedSock::apply (TCP_PLANTED_OPT setsockopt)' \
            1 'no line in src/planted/planted_prose.cpp has this call text'
        phase 'phase B (prose names no syscall)' \
            'src/planted/planted_prose.cpp:return ::setsockopt(fd, 6, TCP_PLANTED_OPT, nullptr, 0);  — effects::IO via PlantedSock::apply (ctx-bound)' \
            1 'prose names no syscall'
        phase 'phase C (constant absent from the file)' \
            'src/planted/planted_prose.cpp:return ::setsockopt(fd, 6, TCP_PLANTED_OPT, nullptr, 0);  — effects::IO via PlantedSock::apply (TCP_QUICKACK setsockopt)' \
            1 'TCP_QUICKACK, which appears nowhere'
        phase 'phase D (symbol absent from the file)' \
            'src/planted/planted_prose.cpp:return ::setsockopt(fd, 6, TCP_PLANTED_OPT, nullptr, 0);  — effects::IO via NoSuchType::apply (TCP_PLANTED_OPT setsockopt)' \
            1 'symbol NoSuchType, which appears nowhere'
        phase 'phase E (rot removed, guard goes clean again)' "$good" 0 ''

        printf 'check-syscall-allowlist-prose: self-test passed — all 6 phases green.\n' >&2
        exit 0
        ;;
    "") ;;
    *) printf 'check-syscall-allowlist-prose: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

scan_root="${CRUCIBLE_SYSCALL_PROSE_TEST_ROOT:-$root}"
allowlist="$scan_root/scripts/syscall-capability-allowlist.txt"

if [[ ! -f "$allowlist" ]]; then
    printf 'check-syscall-allowlist-prose: %s not found.\n' "$allowlist" >&2
    exit 2
fi
if ! command -v grep >/dev/null 2>&1; then
    printf 'check-syscall-allowlist-prose: grep is required\n' >&2
    exit 2
fi

rc=0
scan "$allowlist" "$scan_root" || rc=$?

if [[ "$rc" -ne 0 ]]; then
    cat >&2 <<'HINT'

The entries above have a valid path:line key, so check-syscall-capability.sh
reports clean while the sentence beside each key is false.  An audit
surface that is green and wrong is worse than one that is red.

Fix the prose to describe the call at the named line: which syscall it
invokes, which function issues it, and which effects::* capability the
caller holds.  If the key itself drifted, re-key the entry first — the
rule is to refresh a line-keyed entry in the same commit as the edit that
moved the line.
HINT
    exit 1
fi

printf 'check-syscall-allowlist-prose: clean — every entry names code that is there.\n' >&2
exit 0
