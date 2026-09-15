#!/usr/bin/env bash
# check-fullness-guard.sh — equality-as-fullness-test discipline.
#
# Closes the regression surface for the public-count overflow class
# (#62 / #63).  A container guards its push with a fullness test.  When
# that test is spelled `count == Capacity` instead of `count >=
# Capacity`, a count that has ALREADY passed the bound reads as "not
# full", and the next push writes outside the array.
#
#   if (count == Capacity) { return false; }   // 300 == 64 is false
#   entries[count] = incoming;                 // writes entries[300]
#   ++count;
#
# Four live instances of this shape were found in one sweep:
#   topology/Discovery.h:91        out-of-bounds write, 16-element array
#   canopy/Crdt.h:332              out-of-bounds write, OrSet entries
#   canopy/Crdt.h:829              out-of-bounds write, RgaList entries
#   cntp/GossipMulticast.h:71      out-of-bounds write, neighbour list
#
# `>=` costs the same instruction and is correct for every count the
# type can hold.  There is no case in this tree where `==` is required
# and `>=` is wrong.
#
# ── WHY THIS IS NOT A DEBUG-ONLY CONCERN ─────────────────────────────
#
# The release build ships -DNDEBUG WITHOUT -D_GLIBCXX_ASSERTIONS, so
# std::array::operator[] is unchecked there, and safety::FixedArray's
# operator[] carries no precondition clause by design.  CRUCIBLE_PRE is
# NDEBUG-gated off.  crucible_perf and six bench TUs compile with
# -fcontract-evaluation-semantic=ignore, which erases every contract in
# the headers they include.  A `pre()` is therefore not a guarantee for
# a header those TUs pull in.  The comparison operator is the whole
# enforcement in production.
#
# ── WHAT IS SCANNED ──────────────────────────────────────────────────
#
# A line is a candidate when it compares a COUNTER against a
# CAPACITY-SHAPED BOUND with `==`:
#
#   counter       a bare or dotted identifier — `size`, `state.count`,
#                 `hdr->n`.  Never a call: `v.size() == ...` is a
#                 container asking its own invariant, not a counter.
#   bound         `Capacity` / `capacity`, `Max<Word>`, `MAX_WORD`,
#                 `max_word`, or `<expr>.size()` / `.capacity()` /
#                 `.max_size()`.
#
# `!=` is NOT scanned.  Every `!= <bound>` in this tree is a not-found
# sentinel (`find_peer` returns MaxPeers on miss), and no
# `!=`-as-not-full instance exists.  Adding it would produce sentinel
# noise and catch nothing.  Extend `comparison_ops` below if one ever
# appears.
#
# ── THE PRIVATE-COUNTER HEURISTIC ────────────────────────────────────
#
# `==` against a capacity is only dangerous when the counter can
# actually exceed the bound — that is, when it is NOT protected by a
# class invariant.  In this tree the house convention marks a private
# member with a trailing underscore, and that convention tracks the
# danger exactly: across the ~30 `==`-against-capacity sites surveyed,
# EVERY invariant-protected counter is named with a trailing underscore
# (`peer_count_`, `size_`, `slot_count_`, `event_count_`, `count_`) and
# EVERY unprotected public one is named without (`size`, `count`,
# `state.count`).
#
# So a counter whose final name segment ends in `_` is skipped.  This
# is a heuristic, not a proof: a private counter written by more than
# one method can still drift past its bound, and this guard will not
# see it.  The bound the guard DOES hold is that no new PUBLIC count
# can be guarded with `==`.
#
# ── SUPPRESSION ──────────────────────────────────────────────────────
#
# Inline: `// FULLNESS-OK: <reason>` on the comparison's statement.
# Allowlist: `path:line — <reason>` in
# scripts/fullness-guard-allowlist.txt for grandfathered sites.  Every
# entry says why the `==` is safe, or — for a site owned by another
# change — records the defect and who owns it.
#
# Exit status:
#   0 — clean
#   1 — at least one new `==` fullness test (takes precedence)
#   2 — stale allowlist entry, bad invocation, or missing dependency

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-fullness-guard.sh — equality-as-fullness-test discipline.

Usage:
  check-fullness-guard.sh              # scan; exit 1 on violation
  check-fullness-guard.sh --self-test  # plant violations, verify catch
  check-fullness-guard.sh -h | --help  # usage

Suppression:
  // FULLNESS-OK: <reason>             on the statement — exempts it
  scripts/fullness-guard-allowlist.txt:
    path:line — <reason>               — exempts that site
    # comment                          — ignored

Write `count >= Capacity`, never `count == Capacity`.  The release
build has no _GLIBCXX_ASSERTIONS and FixedArray::operator[] carries no
precondition, so the comparison is the only enforcement in production.
USAGE
}

# ── Pattern ──────────────────────────────────────────────────────────
# Kept in one place so the scanner and the self-test cannot drift.
comparison_ops='=='
bound_alternatives='(?:(?:Capacity|capacity|max_[a-z0-9_]+|MAX_[A-Z0-9_]+|Max[A-Z][A-Za-z0-9_]*)\b|[A-Za-z_][A-Za-z0-9_]*(?:\.|->)(?:size|capacity|max_size)\(\))'
# The counter: a dotted/arrow identifier chain NOT followed by `(`.
counter_alternative='[A-Za-z_][A-Za-z0-9_]*(?:(?:\.|->)[A-Za-z_][A-Za-z0-9_]*)*'
candidate_pattern="(?:^|[[:space:](!&|])(${counter_alternative})[[:space:]]*(?:${comparison_ops})[[:space:]]*${bound_alternatives}"

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test)
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/include/planted" "$tmp_root/scripts"
        cat >"$tmp_root/include/planted/planted_fullness.h" <<'PLANTED'
// Synthetic fullness-guard fixture for --self-test.
#pragma once
#include <array>
#include <cstdint>
namespace crucible::planted {

// planted_flag_public — FLAGGED: public counter, equality fullness test.
struct PlantedPublic {
    std::array<int, 8> entries{};
    std::uint16_t count = 0;
    bool push(int v) {
        if (count == Capacity) {
            return false;
        }
        entries[count] = v;
        ++count;
        return true;
    }
    static constexpr std::uint16_t Capacity = 8;
};

// planted_flag_dotted — FLAGGED: dotted member counter.
struct PlantedDotted {
    static constexpr std::size_t Capacity = 4;
    static bool push(PlantedPublic& state) {
        if (state.count == Capacity) {
            return false;
        }
        return true;
    }
};

// planted_flag_arraysize — FLAGGED: `.size()` bound spelling.
struct PlantedArraySize {
    std::array<int, 16> slots{};
    std::uint8_t size = 0;
    bool push(int v) {
        if (size == slots.size()) {
            return false;
        }
        slots[size++] = v;
        return true;
    }
};

// planted_allowlisted — must NOT be caught (allowlist entry below).
struct PlantedAllowlisted {
    static constexpr std::size_t Capacity = 8;
    std::size_t count = 0;
    bool full() const { return count == Capacity; }
};

// planted_suppressed — must NOT be caught (inline marker).
struct PlantedSuppressed {
    static constexpr std::size_t Capacity = 8;
    std::size_t count = 0;
    bool full() const {
        return count == Capacity;  // FULLNESS-OK: synthetic --self-test marker
    }
};

// planted_private_skip — must NOT be caught: trailing-underscore
// counter is the house marker for an invariant-protected member.
class PlantedPrivate {
public:
    static constexpr std::size_t Capacity = 8;
    bool full() const { return count_ == Capacity; }
private:
    std::size_t count_ = 0;
};

// planted_ge_skip — must NOT be caught: `>=` is the correct form.
struct PlantedCorrect {
    static constexpr std::size_t Capacity = 8;
    std::size_t count = 0;
    bool full() const { return count >= Capacity; }
};

// planted_call_skip — must NOT be caught: a container comparing its
// own size against its own capacity cannot be out of range.
struct PlantedSelfInvariant {
    std::array<int, 8> v{};
    bool full() const { return v.size() == v.max_size(); }
};

// planted_comment_skip — must NOT be caught: comment-only mention.
// if (count == Capacity) { return false; }
struct PlantedCommentOnly {
    int planted_comment_skip = 0;
};
}  // namespace crucible::planted
PLANTED
        # Resolve the allowlisted line by marker so appending cases
        # above cannot silently desync a hardcoded number.
        planted_file="$tmp_root/include/planted/planted_fullness.h"
        fg_line_of() { grep -n -- "$1" "$planted_file" | head -1 | cut -d: -f1; }
        allow_line="$(awk '/planted_allowlisted/{f=1} f&&/count == Capacity/{print NR; exit}' "$planted_file")"
        cat >"$tmp_root/scripts/fullness-guard-allowlist.txt" <<ALLOW
# self-test grandfathered entry
include/planted/planted_fullness.h:${allow_line} — synthetic --self-test entry
ALLOW
        result_file="$(mktemp)"
        if CRUCIBLE_FULLNESS_TEST_ROOT="$tmp_root" \
           bash "${BASH_SOURCE[0]}" 2>"$result_file"; then
            printf 'check-fullness-guard: SELF-TEST FAILED — planted violations not caught.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi

        fg_fail() {
            printf 'check-fullness-guard: SELF-TEST FAILED — %s\n' "$1" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        }

        # Every planted violation MUST be caught.  Line numbers are
        # resolved from the fixture, never hardcoded.
        for marker in 'if (count == Capacity)' 'if (state.count == Capacity)' 'if (size == slots.size())'; do
            want="$(grep -nF -- "$marker" "$planted_file" | head -1 | cut -d: -f1)"
            [[ -n "$want" ]] || fg_fail "fixture lost its '${marker}' case."
            grep -qF "planted_fullness.h:${want}" "$result_file" || \
                fg_fail "'${marker}' at line ${want} not caught — the pattern is dead."
        done

        # Every deliberate boundary case MUST stay clean.
        [[ -n "$allow_line" ]] || fg_fail "fixture lost its allowlist case."
        grep -qF "planted_fullness.h:${allow_line}" "$result_file" && \
            fg_fail "allowlist entry leaked (line ${allow_line})."
        for marker in planted_suppressed planted_private_skip planted_ge_skip \
                      planted_call_skip planted_comment_skip; do
            skip_line="$(fg_line_of "$marker")"
            [[ -n "$skip_line" ]] || fg_fail "fixture lost its ${marker} case."
            # Scan a small window: the case body follows its marker.
            for probe in $(seq "$skip_line" $((skip_line + 6))); do
                if grep -qF "planted_fullness.h:${probe}" "$result_file"; then
                    fg_fail "boundary case ${marker} was flagged at line ${probe} — over-matching."
                fi
            done
        done

        # Stale-entry detection must fire on an allowlist line that no
        # longer names a candidate.
        printf 'include/planted/planted_fullness.h:2 — stale probe\n' \
            >>"$tmp_root/scripts/fullness-guard-allowlist.txt"
        stale_out="$(mktemp)"
        set +e
        CRUCIBLE_FULLNESS_TEST_ROOT="$tmp_root" bash "${BASH_SOURCE[0]}" 2>"$stale_out"
        stale_rc=$?
        set -e
        if ! grep -qF 'stale allowlist entry' "$stale_out"; then
            printf 'check-fullness-guard: SELF-TEST FAILED — stale allowlist entry not reported (rc=%s).\n' \
                "$stale_rc" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' "$(cat "$stale_out")" >&2
            rm -f "$result_file" "$stale_out"
            exit 2
        fi
        rm -f "$result_file" "$stale_out"
        printf 'check-fullness-guard: self-test passed — public / dotted / .size() fullness tests all caught, and the allowlist, inline marker, trailing-underscore, >=, self-invariant-call and comment-only boundaries all stay clean; stale allowlist entries are reported.\n' >&2
        exit 0
        ;;
    "") ;;
    *) printf 'check-fullness-guard: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

# ── Scan-root override for --self-test recursion ─────────────────────
scan_root="${CRUCIBLE_FULLNESS_TEST_ROOT:-$root}"
allowlist="$scan_root/scripts/fullness-guard-allowlist.txt"

if ! command -v rg >/dev/null 2>&1; then
    printf 'check-fullness-guard: ripgrep (rg) is required\n' >&2
    exit 2
fi

allowlisted() {
    local rel="$1" line="$2"
    [[ -f "$allowlist" ]] || return 1
    grep -E -v '^[[:space:]]*(#|$)' "$allowlist" | awk '{print $1}' | grep -Fxq -- "$rel:$line"
}

# The counter whose final name segment ends in `_` is an
# invariant-protected member by house convention — see the header.
counter_is_protected() {
    local text="$1"
    printf '%s' "$text" | grep -qP -- \
        "(?:^|[[:space:](!&|])${counter_alternative}_[[:space:]]*(?:${comparison_ops})[[:space:]]*${bound_alternatives}"
}

# True when the line still carries an UNPROTECTED candidate.
line_is_candidate() {
    local text="$1"
    local code="${text%%//*}"
    printf '%s' "$code" | grep -qP -- "$candidate_pattern" || return 1
    # A line can hold both a protected and an unprotected comparison.
    # Strip every protected one, then re-test.
    local stripped
    stripped="$(printf '%s' "$code" | perl -pe "s/${counter_alternative}_\\s*(?:${comparison_ops})\\s*${bound_alternatives}//g")"
    printf '%s' "$stripped" | grep -qP -- "$candidate_pattern"
}

violation_count=0
stale_count=0
scan_dirs=()
for d in include src vessel; do
    [[ -d "$scan_root/$d" ]] && scan_dirs+=("$scan_root/$d")
done
[[ ${#scan_dirs[@]} -gt 0 ]] || { printf 'check-fullness-guard: no scan dirs under %s\n' "$scan_root" >&2; exit 2; }

hit_keys=""

while IFS= read -r match; do
    file="${match%%:*}"
    rest="${match#*:}"
    line="${rest%%:*}"
    text="${rest#*:}"

    stripped_lead="${text#"${text%%[![:space:]]*}"}"
    case "$stripped_lead" in
        '//'*|'///'*|'*'*|'/*'*) continue ;;
    esac

    line_is_candidate "$text" || continue

    # Inline suppression scoped to the statement, not the line: a
    # formatter may wrap a long condition so the marker lands below the
    # comparison.  Scan to the first line bearing ';' or ending in '{'
    # or '}' (that line included), so a later statement's marker cannot
    # leak backwards.
    fg_suppressed=0
    fg_probe=$line
    fg_limit=$((line + 8))
    while (( fg_probe <= fg_limit )); do
        fg_text="$(sed -n "${fg_probe}p" "$file" 2>/dev/null)"
        case "$fg_text" in
            *'FULLNESS-OK'*) fg_suppressed=1; break ;;
        esac
        case "$fg_text" in
            *';'*) break ;;
        esac
        case "${fg_text%"${fg_text##*[![:space:]]}"}" in
            *'{'|*'}') break ;;
        esac
        fg_probe=$((fg_probe + 1))
    done
    (( fg_suppressed )) && continue

    rel="${file#"$scan_root"/}"
    hit_keys="${hit_keys}${rel}:${line}"$'\n'

    if allowlisted "$rel" "$line"; then
        continue
    fi

    printf 'FULLNESS violation: %s:%s — equality fullness test against a capacity bound; write `>=` so a count already past the bound still reads as full (#62).\n' \
        "$rel" "$line" >&2
    violation_count=$((violation_count + 1))
done < <(
    rg -nP --no-heading --type=cpp \
       --glob '!build*/**' --glob '!cmake-build-*/**' \
       --glob '!third_party/**' --glob '!external/**' --glob '!vendor/**' \
       "$candidate_pattern" "${scan_dirs[@]}" 2>/dev/null || true
)

# ── Stale-entry detection ────────────────────────────────────────────
# An allowlist entry whose site no longer holds a candidate is dead
# weight, and would silently re-grandfather a future `==` reintroduced
# at that same line.
if [[ -f "$allowlist" ]]; then
    while IFS= read -r entry; do
        [[ -z "$entry" ]] && continue
        case "$entry" in '#'*) continue ;; esac
        key="$(printf '%s' "$entry" | awk '{print $1}')"
        [[ -z "$key" ]] && continue
        if ! printf '%s' "$hit_keys" | grep -Fxq -- "$key"; then
            printf 'FULLNESS stale allowlist entry: %s — no equality fullness test at that line any more; prune it.\n' \
                "$key" >&2
            stale_count=$((stale_count + 1))
        fi
    done < <(grep -E -v '^[[:space:]]*(#|$)' "$allowlist")
fi

if [[ "$violation_count" -ne 0 ]]; then
    # Quoted delimiter: the prose below contains backticks, which an
    # unquoted heredoc would run as command substitution.
    cat >&2 <<'HINT'

check-fullness-guard detected equality fullness
test(s) on an unprotected counter.  When the counter has already passed
the bound, `==` reads as "not full" and the next push writes outside
the array.  The release build has no _GLIBCXX_ASSERTIONS and
FixedArray::operator[] carries no precondition, so this comparison is
the only enforcement that reaches production.

Remediations, in order of preference:

  (1) Make the bad state unrepresentable: move the counter private and
      let the guarded push be its only writer, as
      topology::DiscoveryReport and cntp::MtlsPolicy now do.  A counter
      that cannot exceed the bound needs no comparison at all.

  (2) Write `>=` instead of `==`.  Same instruction, correct for every
      value the counter can hold.

  (3) Annotate with '// FULLNESS-OK: <reason>' when the comparison is
      structurally exact — a not-found sentinel, or a local whose bound
      is proven at the point of use.

  (4) Add 'path:line — <reason>' to
      scripts/fullness-guard-allowlist.txt for grandfathered sites.
HINT
    exit 1
fi

if [[ "$stale_count" -ne 0 ]]; then
    printf '\ncheck-fullness-guard: %s stale allowlist entr(y/ies).  Prune them so a future `==` at that line cannot be silently grandfathered.\n' \
        "$stale_count" >&2
    exit 2
fi

printf 'check-fullness-guard: clean — no new equality fullness tests on unprotected counters.\n' >&2
exit 0
