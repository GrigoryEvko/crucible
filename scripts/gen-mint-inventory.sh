#!/usr/bin/env bash
# gen-mint-inventory.sh — generate misc/mint-inventory.md auditor snapshot.
#
# Companion to FIXY-U-002 (test/test_fixy_umbrella_reach.cpp).  U-002 is
# the CI GATE — a constexpr static_assert matrix that fails CI if any
# substrate mint is not reachable through fixy::.  This script is the
# AUDITOR SURFACE — a human-readable inventory of every `mint_*` factory
# in the substrate, its fixy:: re-export status, §XXI compliance, and
# HS14 fixture count.
#
# The inventory is COMMITTED at HEAD and REGENERATED on demand.  CI
# does NOT fail on inventory regeneration (it's a snapshot, not a
# gate).  Future PRs that add or remove a mint should regenerate the
# inventory in the same commit so the file stays current.
#
# Usage:
#   scripts/gen-mint-inventory.sh                 # write misc/mint-inventory.md
#   scripts/gen-mint-inventory.sh --stdout        # print to stdout
#   scripts/gen-mint-inventory.sh --check         # diff against HEAD copy; exit 0 clean / 1 drift
#   scripts/gen-mint-inventory.sh --self-test     # plant a mint, verify capture
#   scripts/gen-mint-inventory.sh -h | --help     # usage
#
# Acceptance gates (per FIXY-U-106):
#   - runs in <5s on full codebase
#   - one section per substrate tree, mints sorted by name
#   - gap markers: [✗ NO-FIXY] / [⚠ NO-NODISCARD] / [⚠ NO-CONSTEXPR] /
#     [⚠ NO-NOEXCEPT] / [⚠ NO-REQUIRES] / [⚠ <2 HS14]
#   - --self-test verifies the scanner captures a planted mint declaration

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
gen-mint-inventory.sh — substrate mint inventory generator.

Usage:
  gen-mint-inventory.sh                 # write misc/mint-inventory.md
  gen-mint-inventory.sh --stdout        # print to stdout
  gen-mint-inventory.sh --check         # diff against HEAD copy
  gen-mint-inventory.sh --self-test     # plant a mint, verify capture
  gen-mint-inventory.sh -h | --help     # usage
USAGE
}

mode="write"
case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --stdout)   mode="stdout" ;;
    --check)    mode="check" ;;
    --self-test) mode="self-test" ;;
    "") ;;
    *) printf 'gen-mint-inventory: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

if ! command -v rg >/dev/null 2>&1; then
    printf 'gen-mint-inventory: ripgrep (rg) is required\n' >&2
    exit 2
fi

scan_root="${CRUCIBLE_MINT_INVENTORY_TEST_ROOT:-$root}"
trees=(safety effects algebra concurrent sessions permissions bridges handles cipher warden perf)

# ── Extract qualifiers for one declaration site ──────────────────────
# Inputs: file path (absolute), line number.
# Reads lines [N-10 .. N+5] and inspects them for [[nodiscard]],
# constexpr/consteval, noexcept, requires, a ctx-bound first
# parameter (Ctx const& / eff::IsExecCtx), and TWO §XXI carve-out
# markers — `cv` for `// §XXI carve-out: cx=alloc` and `cv_rq` for
# `// §XXI carve-out: rq=pre` (FIXY-FOUND-082, value-dependent
# predicate that cannot lift into a `requires`-clause concept; the
# gate is a P2900 `pre(...)` clause instead).
#
# FIXY-V-021: the window was widened from `line - 2` to `line - 10`
# to accept a multi-line carve-out marker between the `requires`
# line and the `[[nodiscard]]` line.  Ten lines of upward reach
# accommodate the canonical layout
#
#   template <…>
#       requires ctx_fits_X<…>
#   // §XXI carve-out: cx=alloc — ≤7 lines of rationale
#   [[nodiscard]] inline …
#   mint_X(…) noexcept { … }
#
# without losing the `requires` line off the top of the window.
# Comments inside the carve-out marker MUST NOT contain the literal
# tokens `constexpr` or `consteval` — `\b(constexpr|consteval)\b` is
# the cx-detection regex and a match anywhere in the window flips cx
# to Y, mis-attributing a documented carve-out as a compliance pass.
# Use paraphrase ("compile-time evaluation", "constant evaluation")
# instead; the marker text itself is the §XXI grep-target, not the
# language-feature name.
#
# When `cv=1` AND `cx=0`, the inventory renders the cx column as
# `- (alloc)` instead of bare `-` — surfacing that the absence is
# documented, not a §XXI compliance gap.  When `cv_rq=1` AND `rq=0`,
# the inventory analogously renders the rq column as `- (pre)`.
# A site that carries either marker AND the language qualifier the
# carve-out is excusing is a contradiction; the inventory still
# emits `Y` in that case (qualifier detection wins) but the auditor
# surface still grep-finds the marker.
#
# Outputs TAB-separated: nd<TAB>cx<TAB>ne<TAB>rq<TAB>cb<TAB>cv<TAB>cv_rq
# (each 0 or 1).
extract_qualifiers() {
    local file="$1" line="$2"
    local start=$(( line - 10 ))
    local end=$(( line + 5 ))
    (( start < 1 )) && start=1

    # FIXY-FOUND-088: tighten the backward window so a sibling overload's
    # qualifiers (constexpr / noexcept / requires) don't leak forward into
    # the current mint's qualifier set.  Canonical defect: mint_chaselev_owner
    # (line 87, constexpr) sits 7 lines above mint_chaselev_thief (line 94,
    # NOT constexpr) — the original 10-line window swept up `constexpr`
    # from the sibling, falsely flagging mint_chaselev_thief as cx=Y.
    #
    # Walk back from `line-1` to `start`; if we find a line whose stripped
    # form starts with `}` (closing brace of the previous function's body),
    # advance `start` to just after that line.  The window then contains
    # ONLY the current mint's signature region + any doc / carve-out marker
    # comments placed above its template line.
    local probe=$(( line - 1 ))
    while (( probe >= start )); do
        local probe_text
        probe_text="$(sed -n "${probe}p" "$file" 2>/dev/null)"
        local probe_stripped="${probe_text#"${probe_text%%[![:space:]]*}"}"
        case "$probe_stripped" in
            '}'*) start=$(( probe + 1 )); break ;;
        esac
        probe=$(( probe - 1 ))
    done

    local window
    window="$(sed -n "${start},${end}p" "$file" 2>/dev/null || true)"

    local nd=0 cx=0 ne=0 rq=0 cb=0 cv=0 cv_rq=0
    if grep -qE '\[\[nodiscard\]\]' <<<"$window"; then nd=1; fi
    if grep -qE '\b(constexpr|consteval)\b' <<<"$window"; then cx=1; fi
    if grep -qE '\bnoexcept\b' <<<"$window"; then ne=1; fi
    if grep -qE '\brequires\b' <<<"$window"; then rq=1; fi
    if grep -qE '(Ctx[[:space:]]+const[[:space:]]*&|effects::IsExecCtx|eff::IsExecCtx)' <<<"$window"; then cb=1; fi
    # FIXY-V-021 / FIXY-FOUND-088: cx=alloc carve-out marker.  Mirrors
    # cv_rq below — the marker is AUTHORITATIVE.  Force cx=0 when cv=1
    # because the carve-out comment legitimately contains the word
    # "constexpr" in its rationale text (e.g. "constexpr would lie"),
    # and `\b(constexpr|consteval)\b` would otherwise false-positive on
    # that prose.  A site that has BOTH the marker AND a real constexpr
    # qualifier is an internal contradiction; the marker wins (reviewers
    # grep the marker and see the intent).
    if grep -qF '§XXI carve-out: cx=alloc' <<<"$window"; then
        cv=1
        cx=0
    fi
    # FIXY-FOUND-082: §XXI carve-out for value-dependent gates that
    # cannot lift into a `requires`-clause (the predicate inspects
    # carrier runtime state, not just template arguments).  Canonical
    # example: mint_view's `pre(view_ok(c, type_identity<Tag>{}))`.
    # When cv_rq=1 AND rq=0, the inventory renders the rq column as
    # `- (pre)` — surfacing that the absence is documented, not a §XXI
    # compliance gap.  The marker is AUTHORITATIVE: we force rq=0 when
    # cv_rq=1 because the carve-out comment legitimately contains the
    # word "requires" (rationale text), and the `\brequires\b` regex
    # would otherwise false-positive on that prose.
    if grep -qF '§XXI carve-out: rq=pre' <<<"$window"; then
        cv_rq=1
        rq=0
    fi

    printf '%d\t%d\t%d\t%d\t%d\t%d\t%d\n' "$nd" "$cx" "$ne" "$rq" "$cb" "$cv" "$cv_rq"
}

# ── Scan one substrate tree for mint_* declarations ──────────────────
# Emits TAB-separated rows: tree<TAB>name<TAB>file:line<TAB>nd<TAB>cx<TAB>ne<TAB>rq<TAB>cb<TAB>cv<TAB>cv_rq
#
# Filter: skips comment lines (//, ///, *, /*) and pure-call lines
# (lines whose first non-whitespace token is `return` or `auto X =`).
scan_substrate() {
    local tree="$1"
    local dir="$scan_root/include/crucible/$tree"
    [[ -d "$dir" ]] || return 0

    while IFS=: read -r file line text; do
        # Skip comment lines.
        local stripped="${text#"${text%%[![:space:]]*}"}"
        case "$stripped" in
            '//'*|'///'*|'*'*|'/*'*) continue ;;
            'return '*|'return('*|'auto '*=*) continue ;;
            # Defaulted/deleted special member.  A passkey class named
            # `mint_*_key` declares its OWN ctor as
            # `mint_*_key() ... = default;` (or `= delete;`), which
            # matches `\bmint_…(` but is a CONSTRUCTOR, not a factory
            # (e.g. PermissionInherit.h's mint_permission_inherit_key —
            # the real factory there is `mint_permission_inherit`).
            # `= default` / `= delete` is special-member-only syntax,
            # never used by a mint factory, so this is a zero-false-
            # positive drop — the third phantom class after the U-110
            # word-boundary fix (substring phantoms) closed the first two.
            *'= default'*|*'=default'*|*'= delete'*|*'=delete'*) continue ;;
            # FIXY-U-118: string-literal continuation.  A multi-line
            # static_assert diagnostic message naming a mint factory
            # (e.g. `"mint_persisted_session(ctx, ...) requires ..."`)
            # produces continuation lines whose stripped form starts
            # with a quote.  These are NEVER declaration sites — they
            # are documentation text inside a string literal that the
            # mint_* token happens to appear in.  Surveyed 13 such
            # matches across permissions/Permission.h, permissions/
            # FederationPermission.h, bridges/SessionPersistence.h —
            # all 13 start with `"` after leading whitespace.  A
            # standalone match on Cipher.h's `mint_open_view()` member
            # function previously lost out to bridges/SessionPersistence.h's
            # diagnostic-string mention because the scanner picked the
            # first file-order match; with this skip the diagnostic-string
            # match never enters dedup, so the member-function site wins
            # canonically (or, when no real declaration exists, the name
            # drops from inventory entirely — the desired outcome for
            # phantom-only matches).
            '"'*) continue ;;
        esac

        # FIXY-V-014: skip `friend` declarations.  A `friend` declaration
        # of a free-function mint inside a class body is the ACCESS-GRANT,
        # not the canonical authorization point.  Crucially, GCC 16
        # `-Werror=attributes` rejects `[[nodiscard]]` on a non-defining
        # friend declaration (P2900/P3441 attribute-ignorability rules),
        # so the friend-line ALWAYS has nd=N even when the actual mint
        # factory definition (at module scope, later in the same TU)
        # carries `[[nodiscard]]`.  Without this skip, the friend at
        # `Endpoint.h:377` lex-sorts before the free-function definition
        # at `Endpoint.h:592` (string-sort of `file:LINE` yields
        # "377" < "592"), dedup picks the friend, and the inventory
        # spuriously shows nd=N on a fully §XXI-compliant mint.  Friend
        # declarations are also structurally never the §XXI grep-target
        # (the convention is "every cross-tier composition factory MUST
        # be named mint_<noun>" — the factory itself, not its access grant).
        case "$stripped" in
            'friend '*|'friend('*) continue ;;
        esac

        # FIXY-FOUND-082: multi-line `friend` declarations also need to
        # skip.  The `friend` keyword appears on the line ABOVE the mint
        # name when the return type and signature span two lines, e.g.:
        #     template <typename Tag_, typename Carrier_>
        #     friend constexpr ScopedView<Carrier_, Tag_>
        #     mint_view(Carrier_ const& c) noexcept;
        # The single-line skip above misses this because line N (the
        # mint_NAME line) does NOT start with `friend`; the keyword is on
        # line N-1.  Walk back through blank-trimmed previous lines until
        # we hit a `friend` (skip), an opening brace / semicolon (NOT a
        # friend — fall through), or 5 lines (also NOT a friend).
        local prev_idx=$((line - 1))
        local saw_friend=0
        local walked=0
        while (( prev_idx >= 1 && walked < 5 )); do
            local prev_text
            prev_text="$(sed -n "${prev_idx}p" "$file" 2>/dev/null)"
            local prev_stripped="${prev_text#"${prev_text%%[![:space:]]*}"}"
            # Empty / comment-only line: keep walking.
            case "$prev_stripped" in
                ''|'//'*|'///'*|'*'*|'/*'*) prev_idx=$((prev_idx - 1)); walked=$((walked + 1)); continue ;;
            esac
            # Friend keyword as start of the previous declaration line —
            # we're inside a multi-line friend.
            case "$prev_stripped" in
                'friend '*|'friend('*) saw_friend=1; break ;;
            esac
            # Any other declaration-shape token closes the look-back —
            # the mint is NOT part of a friend declaration.
            break
        done
        (( saw_friend == 1 )) && continue

        # Extract mint name from the matched line.  awk avoids head -1 +
        # pipefail traps; rg's pattern guarantees at least one match.
        # The `(^|[^A-Za-z0-9_])` prefix is a word-boundary guard: without
        # it, `cap_mint_key()` mis-extracts as `mint_key` and
        # `…_mint_boundary(` as `mint_boundary` (substring matches inside a
        # larger identifier).  The trailing `[[:space:]]*\(` anchors to the
        # actual factory call, so type-qualified return lines that also
        # contain an unrelated `_mint_` token pick the real callee.
        local name
        name="$(awk 'match($0, /(^|[^A-Za-z0-9_])mint_[a-z0-9_]+[[:space:]]*\(/) {
                         s = substr($0, RSTART, RLENGTH);
                         sub(/^[^A-Za-z0-9_]*/, "", s);
                         sub(/[[:space:]]*\(.*$/, "", s);
                         print s; exit }' <<<"$text")"
        [[ -z "$name" ]] && continue

        # Get qualifiers.
        local quals
        quals="$(extract_qualifiers "$file" "$line")"

        local rel="${file#"$scan_root"/}"
        printf '%s\t%s\t%s:%s\t%s\n' "$tree" "$name" "$rel" "$line" "$quals"
    done < <(
        rg -nP \
           --no-heading \
           --type=cpp \
           --glob '!_*.h' \
           '\bmint_[a-z0-9_]+\s*\(' "$dir" 2>/dev/null || true
    )
}

# ── Scan top-level headers for class-member mint_X declarations ──────
# FIXY-U-118b (Part 2): member-function mints are CLASS METHODS (e.g.
# `Cipher::mint_open_view`, `ReplayEngine::mint_active_view`).  They
# CANNOT be `using`-re-exported at namespace scope — a `using`-decl
# moves a free-function NAME into another namespace, but a member
# function's authority is the object whose method it is.  Therefore the
# §XXI authorization-point grep-target lives at the class-method
# declaration site itself, and the "fixy re-export" cell is structurally
# inapplicable.
#
# This scanner discovers them by walking `include/crucible/*.h` top-level
# files (the substrate trees scanned above never contain top-level
# headers — they live under safety/, sessions/, etc.) plus a curated set
# of inner trees where member-function mints have been observed.  For
# each `^\s+\[\[nodiscard\]\]…mint_X(` match, it reverse-scans for the
# enclosing `^(class|struct) … NAME` declaration, stripping known
# `CRUCIBLE_*` annotation macros (CRUCIBLE_OWNER, CRUCIBLE_API, …).
#
# Emits TAB-separated rows:
#   class_name<TAB>mint_name<TAB>file:line<TAB>nd<TAB>cx<TAB>ne<TAB>rq<TAB>cb
scan_member_function_mints() {
    local files=()
    local f
    # Top-level Crucible headers (Cipher.h, Vigil.h, PoolAllocator.h,
    # ReplayEngine.h, CKernel.h, SchemaTable.h, CrucibleContext.h, ...)
    # are not part of any substrate tree in `trees=()`.  Scan them here.
    for f in "$scan_root"/include/crucible/*.h; do
        [[ -f "$f" ]] && files+=("$f")
    done
    [[ ${#files[@]} -eq 0 ]] && return 0

    while IFS=: read -r file line text; do
        # Same line-skip discipline as scan_substrate: drop comments and
        # string-literal continuations.  The leading-whitespace +
        # `[[nodiscard]]` anchor of the rg pattern already excludes
        # `return ...` and `auto x = ...` call sites, so those cases
        # never reach this filter.
        local stripped="${text#"${text%%[![:space:]]*}"}"
        case "$stripped" in
            '//'*|'///'*|'*'*|'/*'*) continue ;;
            '"'*) continue ;;
        esac

        # Extract mint name from text.  Awk regex does NOT support
        # `\b` as word-boundary (awk treats `\b` as literal backspace,
        # 0x08); use the same explicit-character-class trick scan_substrate
        # uses — `(^|[^A-Za-z0-9_])` left anchor, with a trailing `(`
        # right anchor to land on the actual factory token (avoids
        # matching `mint_*_key` member of a passkey class declared
        # earlier in the same line).
        local name
        name="$(awk 'match($0, /(^|[^A-Za-z0-9_])mint_[a-z0-9_]+[[:space:]]*\(/) {
                         s = substr($0, RSTART, RLENGTH);
                         sub(/^[^A-Za-z0-9_]*/, "", s);
                         sub(/[[:space:]]*\(.*$/, "", s);
                         print s; exit }' <<<"$text")"
        [[ -z "$name" ]] && continue

        # Walk forward through $file recording the most recent column-0
        # `class|struct NAME` declaration; print it when line number
        # reaches the mint's line.  Two annotation families are stripped
        # before the name match so the regex's `[A-Za-z_]` anchor lands
        # on the actual identifier:
        #   1. `[[...]]` attribute blocks — handles `class [[nodiscard]]
        #      Foo {` and `class [[gnu::const, gnu::pure]] Bar {`.
        #      Stripped FIRST because the alternate ordering (`class
        #      CRUCIBLE_OWNER [[nodiscard]] Baz`) is also legal and
        #      stripping CRUCIBLE_* first would leave `class
        #      [[nodiscard]] Baz` which would still need attribute strip.
        #   2. `CRUCIBLE_*` annotation macros (CRUCIBLE_OWNER,
        #      CRUCIBLE_API, …) — handles `class CRUCIBLE_OWNER Cipher {`.
        # FIXY-U-118c: pre-empts a class-name mis-attribution bug where
        # a `class [[nodiscard]] Inner { mint_x(...) }` nested inside an
        # outer `class CRUCIBLE_OWNER Outer { ... }` would be silently
        # attributed to Outer because the inner `match()` would fail to
        # capture (the leading `[` is not in `[A-Za-z_]`) and `last_class`
        # would retain the previous (wrong) value.
        local class_name
        class_name="$(awk -v target="$line" '
            /^(class|struct)[[:space:]]/ {
                cleaned = $0
                gsub(/\[\[[^]]*\]\][[:space:]]*/, "", cleaned)
                gsub(/CRUCIBLE_[A-Z_]+[[:space:]]+/, "", cleaned)
                if (match(cleaned, /^(class|struct)[[:space:]]+([A-Za-z_][A-Za-z0-9_]*)/, m)) {
                    last_class = m[2]
                }
            }
            NR == target { print last_class; exit }
        ' "$file")"

        [[ -z "$class_name" ]] && class_name="(unknown)"

        local quals
        quals="$(extract_qualifiers "$file" "$line")"

        local rel="${file#"$scan_root"/}"
        printf '%s\t%s\t%s:%s\t%s\n' "$class_name" "$name" "$rel" "$line" "$quals"
    done < <(
        # `--with-filename` (-H) is LOAD-BEARING: when rg is given exactly
        # one path argument it auto-omits the filename prefix, breaking
        # the `IFS=: read -r file line text` parse downstream (the line
        # number lands in $file, the body in $line, and $text is empty).
        # The production scan always sees ≥8 top-level headers so the
        # bug never manifested; --self-test plants exactly one header so
        # the bug surfaced as "0 member-function mints captured" until
        # FIXY-U-118c forced the prefix.
        rg -nP --no-heading --with-filename \
           '^\s+\[\[nodiscard\]\][^{]*\bmint_[a-z0-9_]+\s*\(' \
           "${files[@]}" 2>/dev/null || true
    )
}

# ── Scan fixy/ for fixy-ORIGIN mint declarations (FIXY-V-271) ────────
# The substrate scan above cross-references each substrate mint INTO
# fixy/ for its re-export site (fixy_reexport_for).  But a mint whose
# CANONICAL declaration lives in fixy/ with NO substrate counterpart —
# the hardware-axis grant mints (mint_simd_width / mint_vendor_intrinsic
# / mint_tsc_grant / mint_msr_grant / mint_asm_grant), the syscall/mmap/
# io factories (mint_file / mint_mmap / mint_io_uring_ring / ...), the
# durable-Cipher writers (mint_warm_writer / mint_cold_writer / ...), the
# spawn factories (mint_spawn / mint_parallel_for), and the async-pipeline
# mints (mint_async_copy / mint_mbarrier_arrive / mint_mbarrier_wait) —
# never appears in any `trees=()` directory, so it was previously
# INVISIBLE to the inventory.  This scanner closes that gap: it walks
# include/crucible/fixy/ (recursing into grant/, sync/, spawn/, syscall/)
# and emits every free-function mint_* declaration whose name is NOT
# already a substrate mint.
#
# Names present in BOTH fixy/ and a substrate tree are EXCLUDED here:
#   * A substrate mint forwarded/re-exported through fixy/ is already
#     tracked in the substrate section (its `fixy` cell records the
#     re-export site).
#   * A name-collision between a substrate token mint and a DISTINCT
#     fixy grant mint (e.g. safety::mint_scoped_fence the token mint vs
#     the grant::hw::scope mint_scoped_fence) is the pre-existing by-name
#     attribution limitation: the substrate row absorbs the shared name.
#     Resolving the collision is out of scope for V-271 (whose premise is
#     the previously-untracked fixy-ORIGIN mints, not name disambiguation).
#
# The "fixy re-export" column is structurally inapplicable here (these
# mints ARE the fixy declaration), exactly like the member-function
# section — so the emitted row omits it.  nd/cx/ne/rq/cb compliance and
# HS14 fixture coverage are still audited.
#
# Input:  $1 = path to a sorted file of substrate mint names (one per
#              line) to exclude.
# Emits TAB-separated rows:
#   name<TAB>file:line<TAB>nd<TAB>cx<TAB>ne<TAB>rq<TAB>cb<TAB>cv
scan_fixy_mints() {
    local exclude_file="$1"
    local dir="$scan_root/include/crucible/fixy"
    [[ -d "$dir" ]] || return 0

    while IFS=: read -r file line text; do
        # Same line-skip discipline as scan_substrate: drop comment lines,
        # pure-call lines, defaulted/deleted special members, string-literal
        # continuations, and friend declarations.
        local stripped="${text#"${text%%[![:space:]]*}"}"
        case "$stripped" in
            '//'*|'///'*|'*'*|'/*'*) continue ;;
            'return '*|'return('*|'auto '*=*) continue ;;
            *'= default'*|*'=default'*|*'= delete'*|*'=delete'*) continue ;;
            '"'*) continue ;;
            'friend '*|'friend('*) continue ;;
        esac

        local name
        name="$(awk 'match($0, /(^|[^A-Za-z0-9_])mint_[a-z0-9_]+[[:space:]]*\(/) {
                         s = substr($0, RSTART, RLENGTH);
                         sub(/^[^A-Za-z0-9_]*/, "", s);
                         sub(/[[:space:]]*\(.*$/, "", s);
                         print s; exit }' <<<"$text")"
        [[ -z "$name" ]] && continue

        # Exclude substrate-origin names (forwarders / re-exports /
        # name-collisions) — those belong in the substrate section.
        # `grep -qxF` is a whole-line fixed match so `mint_fn` excludes
        # ONLY `mint_fn`, never `mint_fn_for`.
        if [[ -s "$exclude_file" ]] && grep -qxF "$name" "$exclude_file"; then
            continue
        fi

        local quals
        quals="$(extract_qualifiers "$file" "$line")"

        local rel="${file#"$scan_root"/}"
        printf '%s\t%s:%s\t%s\n' "$name" "$rel" "$line" "$quals"
    done < <(
        rg -nP \
           --no-heading \
           --type=cpp \
           --glob '!_*.h' \
           '\bmint_[a-z0-9_]+\s*\(' "$dir" 2>/dev/null || true
    )
}

# ── Cross-reference one mint_name into fixy/ for re-export status ────
# Returns: fixy_path (e.g. "include/crucible/fixy/Sess.h:123") or empty.
# We accept the FIRST match (sorted by file then line) as the canonical
# re-export site; a using-decl or namespace-alias is the typical shape.
fixy_reexport_for() {
    local name="$1"
    # rg exits 1 on no-match; under pipefail we'd inherit that.
    # Capture via command substitution + `|| true` to keep the function safe.
    # `sort` before `head -1` canonicalizes the choice; without it the
    # winner depends on rg's `readdir` order which varies by filesystem.
    local raw
    raw="$(rg -nP --no-heading --type=cpp \
              "\b${name}\b" "$scan_root/include/crucible/fixy/" 2>/dev/null || true)"
    [[ -z "$raw" ]] && return 0
    printf '%s\n' "$raw" | sort | head -1 | awk -F: '{print $1 ":" $2}'
}

# ── Count neg-compile fixtures mentioning a mint_name ────────────────
# HS14 fixtures live in the neg-test tree matching the mint's substrate
# (warden_neg for warden/, perf_neg for perf/, effects_neg / safety_neg /
# sessions_neg / fixy_neg / ...), NOT only fixy_neg.  A substrate-only
# mint (no fixy:: re-export) is still HS14-covered if its own tree holds
# the fixtures — counting only fixy_neg falsely flagged ~33 such mints.
# Span every test/*_neg/ tree so the count reflects ACTUAL coverage; the
# separate NO-FIXY cell still records whether the mint is fixy-re-exported.
# FIXY-V-271: the count is the number of FILES under test/*_neg/ that
# mention a `mint_*` name (word-boundary match).  The per-mint form
# (`rg -lP "\bNAME\b" | wc -l`, one rg spawn per mint, ~150 for the full
# inventory) pushed the generator past the FIXY-U-106 <5s acceptance
# budget once the fixy-origin section added 30 more lookups.  Replaced
# with a SINGLE rg pass (build_hs14_index) that tokenizes every
# `\bmint_[a-z0-9_]+\b` occurrence across all neg trees, deduplicates
# (file, token) pairs, and counts distinct files per token into
# CRUCIBLE_HS14_INDEX.  The broad token regex captures the WHOLE
# identifier (underscores + digits), so `mint_async_copy` and
# `mint_async_copy_extra` stay distinct — semantically identical to the
# old per-name `\bNAME\b` form.  The --check drift gate + --self-test
# HS14:2 assertion validate the counts match.
declare -A CRUCIBLE_HS14_INDEX=()

build_hs14_index() {
    local dirs=() d
    for d in "$scan_root"/test/*_neg/; do
        [[ -d "$d" ]] && dirs+=("$d")
    done
    [[ ${#dirs[@]} -eq 0 ]] && return 0

    local name count
    # Process substitution (`< <(...)`) runs the while loop in THIS shell
    # so the global associative-array assignments persist; a pipe would
    # spawn a subshell and lose them.  No -n, so the token is the last
    # colon-field and the file is $1 (test/*_neg/ paths contain no colon).
    while IFS=$'\t' read -r name count; do
        [[ -n "$name" ]] && CRUCIBLE_HS14_INDEX["$name"]="$count"
    done < <(
        rg -oP --no-heading --with-filename '\bmint_[a-z0-9_]+\b' \
           "${dirs[@]}" 2>/dev/null \
          | awk -F: '{
                file = $1; tok = $NF;
                key = file SUBSEP tok;
                if (!(key in seen)) { seen[key] = 1; cnt[tok]++ }
            } END { for (t in cnt) printf "%s\t%s\n", t, cnt[t] }'
    )
}

hs14_count_for() {
    printf '%s' "${CRUCIBLE_HS14_INDEX[$1]:-0}"
}

# ── --self-test ──────────────────────────────────────────────────────
if [[ "$mode" == "self-test" ]]; then
    tmp_root="$(mktemp -d)"
    trap 'rm -rf "$tmp_root"' EXIT
    mkdir -p "$tmp_root/include/crucible/safety" \
             "$tmp_root/include/crucible/fixy" \
             "$tmp_root/test/fixy_neg"
    cat >"$tmp_root/include/crucible/safety/PlantedSelfTest.h" <<'PLANTED'
#pragma once
// Synthetic mint-inventory fixture.
namespace crucible::planted {
struct PlantedToken {};
[[nodiscard]] constexpr PlantedToken
mint_planted_token() noexcept { return {}; }
}
PLANTED
    cat >"$tmp_root/include/crucible/fixy/Planted.h" <<'FIXY'
#pragma once
#include <crucible/safety/PlantedSelfTest.h>
namespace crucible::fixy::planted {
// FIXY-V-271: forwarding DEFINITION re-exporting the substrate mint.  Shares
// the name mint_planted_token with safety/PlantedSelfTest.h, so it (a) feeds
// the substrate row's `fixy` re-export cell AND (b) MUST be EXCLUDED from the
// fixy-origin section (the substrate section owns the canonical row).
[[nodiscard]] constexpr ::crucible::planted::PlantedToken
mint_planted_token() noexcept { return ::crucible::planted::mint_planted_token(); }
// FIXY-V-271: a fixy-ORIGIN mint with NO substrate counterpart — MUST
// surface in the "fixy-origin mints" section.
struct FixyOriginToken {};
[[nodiscard]] constexpr FixyOriginToken
mint_planted_fixy_origin() noexcept { return {}; }
}
FIXY
    cat >"$tmp_root/test/fixy_neg/neg_fixy_planted_a.cpp" <<'NEG'
#include <crucible/fixy/Planted.h>
// references mint_planted_token to verify HS14 counter
NEG
    cat >"$tmp_root/test/fixy_neg/neg_fixy_planted_b.cpp" <<'NEG'
#include <crucible/fixy/Planted.h>
// references mint_planted_token (second fixture for HS14 threshold)
NEG
    # ── Plant class-method mints — covers scan_member_function_mints ──
    # FIXY-U-118c extension: the substrate path above tests free-function
    # mint capture + fixy re-export + HS14 counter.  This block additionally
    # plants two class-method mints (one under each annotation family) so
    # `scan_member_function_mints` is exercised end-to-end:
    #
    #   1. `class CRUCIBLE_OWNER MacroHost`        — CRUCIBLE_* macro
    #                                                annotation, was the
    #                                                original known case.
    #   2. `class [[nodiscard]] AttrHost`          — `[[...]]` attribute
    #                                                annotation, the new
    #                                                FIXY-U-118c-covered case.
    #
    # The self-test then verifies BOTH class names extract correctly (i.e.
    # `MacroHost::mint_planted_macro` and `AttrHost::mint_planted_attr`,
    # NOT `(unknown)::...`) — proving the awk gsub strip handles each
    # annotation kind and the rg `--with-filename` prefix is honoured.
    cat >"$tmp_root/include/crucible/PlantedHost.h" <<'HOST'
#pragma once
// Synthetic member-function mint-inventory fixture.
#define CRUCIBLE_OWNER  /* deliberately empty for the self-test */
namespace crucible::planted {
class CRUCIBLE_OWNER MacroHost {
public:
    [[nodiscard]] int mint_planted_macro() const noexcept { return 0; }
};
class [[nodiscard]] AttrHost {
public:
    [[nodiscard]] int mint_planted_attr() const noexcept { return 0; }
};
}
HOST
    out="$(mktemp)"
    CRUCIBLE_MINT_INVENTORY_TEST_ROOT="$tmp_root" \
        bash "${BASH_SOURCE[0]}" --stdout >"$out" 2>/dev/null

    if ! grep -qF 'mint_planted_token' "$out"; then
        printf 'gen-mint-inventory: SELF-TEST FAILED — planted mint not captured.\n' >&2
        printf '── output ───\n%s\n────────────\n' "$(cat "$out")" >&2
        rm -f "$out"
        exit 2
    fi
    # Verify fixy re-export captured.
    if ! grep -F 'mint_planted_token' "$out" | grep -qF 'fixy/Planted.h'; then
        printf 'gen-mint-inventory: SELF-TEST FAILED — fixy re-export not captured.\n' >&2
        printf '── output ───\n%s\n────────────\n' "$(cat "$out")" >&2
        rm -f "$out"
        exit 2
    fi
    # Verify HS14 count >= 2 (we planted 2 fixtures).
    line="$(grep -F 'mint_planted_token' "$out" | head -1)"
    if ! grep -qE 'HS14:[[:space:]]*2' <<<"$line"; then
        printf 'gen-mint-inventory: SELF-TEST FAILED — HS14 count not 2.\n' >&2
        printf '── line ───\n%s\n──────────\n' "$line" >&2
        rm -f "$out"
        exit 2
    fi
    # ── Member-function mint discovery + class-name extraction ──
    # FIXY-U-118c: assert BOTH annotation families resolve to the right
    # enclosing class.  Each check is independently failable so the
    # diagnostic points at the specific failure mode (rg prefix bug,
    # CRUCIBLE_ macro strip bug, or [[...]] attribute strip bug).
    if ! grep -qF 'MacroHost::mint_planted_macro' "$out"; then
        printf 'gen-mint-inventory: SELF-TEST FAILED — MacroHost::mint_planted_macro not captured.\n' >&2
        printf '   (CRUCIBLE_* annotation-macro strip regressed OR rg --with-filename missing.)\n' >&2
        printf '── output ───\n%s\n────────────\n' "$(cat "$out")" >&2
        rm -f "$out"
        exit 2
    fi
    if ! grep -qF 'AttrHost::mint_planted_attr' "$out"; then
        printf 'gen-mint-inventory: SELF-TEST FAILED — AttrHost::mint_planted_attr not captured.\n' >&2
        printf '   (`[[...]]` attribute strip regressed OR class-name fell through to (unknown).)\n' >&2
        printf '── output ───\n%s\n────────────\n' "$(cat "$out")" >&2
        rm -f "$out"
        exit 2
    fi
    # Negative: verify NO `(unknown)::mint_planted_*` row leaked through
    # (would indicate the strip succeeded for one annotation but not the
    # other, and the result fell through to the `[[ -z "$class_name" ]]
    # && class_name="(unknown)"` fallback).
    if grep -qE '\(unknown\)::mint_planted_' "$out"; then
        printf 'gen-mint-inventory: SELF-TEST FAILED — planted mint fell through to (unknown)::.\n' >&2
        printf '── output ───\n%s\n────────────\n' "$(cat "$out")" >&2
        rm -f "$out"
        exit 2
    fi
    # ── fixy-origin mint discovery (FIXY-V-271) ──
    # Planted fixy/Planted.h DEFINES mint_planted_fixy_origin (no substrate
    # counterpart) and a same-named forwarding mint_planted_token (substrate
    # origin).  Assert the fixy-origin SECTION captures the former and
    # EXCLUDES the latter — a substrate mint re-exported through fixy/
    # belongs in the substrate section, not fixy-origin.
    fixy_section="$(awk '/^## fixy-origin mints$/{f=1;next} /^## /{f=0} f' "$out")"
    if ! grep -qF 'mint_planted_fixy_origin' <<<"$fixy_section"; then
        printf 'gen-mint-inventory: SELF-TEST FAILED — fixy-origin mint not captured in fixy-origin section.\n' >&2
        printf '── output ───\n%s\n────────────\n' "$(cat "$out")" >&2
        rm -f "$out"
        exit 2
    fi
    if grep -qF 'mint_planted_token' <<<"$fixy_section"; then
        printf 'gen-mint-inventory: SELF-TEST FAILED — substrate mint_planted_token leaked into fixy-origin section (must be excluded).\n' >&2
        printf '── output ───\n%s\n────────────\n' "$(cat "$out")" >&2
        rm -f "$out"
        exit 2
    fi
    rm -f "$out"
    printf 'gen-mint-inventory: self-test passed — substrate mint + fixy re-export + HS14 count + member-function discovery (CRUCIBLE_* + [[...]] annotation families) + fixy-origin capture/exclusion all honoured.\n' >&2
    exit 0
fi

# ── Build the inventory ──────────────────────────────────────────────
inventory_tmp="$(mktemp)"
trap 'rm -f "$inventory_tmp"' EXIT

for tree in "${trees[@]}"; do
    scan_substrate "$tree"
done | sort -t$'\t' -k1,1 -k2,2 -k3,3 | \
    awk -F'\t' '!seen[$1"\t"$2]++' >"$inventory_tmp"
# Note: some mint names are declared in multiple files (e.g.
# `mint_consumer_session` in both CalendarGridSession.h and
# SpscSession.h).  We canonicalize on the FIRST declaration when
# sorted by (tree, name, file:line); awk-based dedup keeps that
# choice deterministic across runs.  Future PRs that add an
# alphabetically-earlier declaration will surface as drift via
# `--check`, prompting the auditor to re-snapshot or reconcile.

# ── Emit markdown ────────────────────────────────────────────────────
emit_inventory() {
    # FIXY-V-271: one-time HS14 fixture-count index (replaces ~150 per-mint
    # rg spawns) — keeps generation under the FIXY-U-106 <5s budget.
    build_hs14_index

    local generated_at
    generated_at="$(date -u +'%Y-%m-%dT%H:%M:%SZ')"
    cat <<HEADER
# Mint inventory — auditor snapshot

Generated by \`scripts/gen-mint-inventory.sh\` (FIXY-U-106).

This is the auditor-facing companion to \`test/test_fixy_umbrella_reach.cpp\`
(FIXY-U-002, the CI gate).  The inventory below is a SNAPSHOT — regenerate
in the same PR that adds, removes, or renames a substrate \`mint_*\` factory:

\`\`\`bash
scripts/gen-mint-inventory.sh                 # write misc/mint-inventory.md
scripts/gen-mint-inventory.sh --check         # diff against HEAD; exit 1 on drift
\`\`\`

Per CLAUDE.md §XXI Universal Mint Pattern, every cross-tier composition
factory is named \`mint_<noun>\`.  Each row records:

| Column | Meaning |
|---|---|
| \`mint_name\` | The factory's identifier. |
| \`file:line\` | Substrate declaration site (canonical). |
| \`nd cx ne rq\` | §XXI compliance flags: \`[[nodiscard]]\` / \`constexpr\` (or \`consteval\`) / \`noexcept\` / \`requires\`-clause.  \`Y\` = present, \`-\` = absent.  \`- (alloc)\` in the \`cx\` column = documented carve-out (FIXY-V-021): the mint genuinely allocates (BPF program load, perf_event_open + mmap, heap, syscall) so the §XXI \`constexpr\` qualifier would lie about the runtime cost.  The carve-out is grep-discoverable via the \`// §XXI carve-out: cx=alloc\` marker placed immediately above the \`[[nodiscard]]\` line at the mint signature.  \`- (pre)\` in the \`rq\` column = documented carve-out (FIXY-FOUND-082): the gate is a P2900 \`pre(...)\` clause instead of a \`requires\`-clause because the predicate is value-dependent (inspects carrier runtime state, not just template arguments) and cannot lift into a concept.  Canonical example: \`mint_view\`'s \`pre(view_ok(c, type_identity<Tag>{}))\`.  Marker: \`// §XXI carve-out: rq=pre\` above the mint signature. |
| \`cb\` | Authorization shape: \`ctx\` (ctx-bound mint, \`Ctx const&\` first parameter), \`token\` (token mint, derives authority from a parent token), or \`member\` (class-method mint — see "Member-function mints" section below). |
| \`fixy\` | fixy:: re-export site (\`include/crucible/fixy/...\`) or \`[✗ NO-FIXY]\` gap.  Inapplicable for the \`member\` row (class-method mints cannot be \`using\`-re-exported at namespace scope). |
| \`HS14\` | Count of neg-compile fixtures across all \`test/*_neg/\` trees (fixy_neg, warden_neg, perf_neg, effects_neg, safety_neg, …) mentioning this mint (HS14 floor is 2). |

Gap markers: \`[✗ NO-FIXY]\` (substrate mint not re-exported through fixy::),
\`[⚠ <2 HS14]\` (HS14 fixture floor not met).  §XXI compliance shortfalls
appear as \`-\` in the flag columns.  \`- (alloc)\` is documented absence,
not a gap.  The auditor surface for member-function mints lives in a
separate "Member-function mints" section after the substrate trees
(FIXY-U-118b).

Snapshot generated: \`$generated_at\`.

HEADER

    local current_tree=""
    local violation_count=0
    while IFS=$'\t' read -r tree name fileline nd cx ne rq cb cv cv_rq; do
        if [[ "$tree" != "$current_tree" ]]; then
            [[ -n "$current_tree" ]] && printf '\n'
            printf '## %s/\n\n' "$tree"
            printf '| mint_name | file:line | nd | cx | ne | rq | cb | fixy | HS14 |\n'
            printf '|---|---|---|---|---|---|---|---|---|\n'
            current_tree="$tree"
        fi

        local fixy hs14
        fixy="$(fixy_reexport_for "$name")"
        hs14="$(hs14_count_for "$name")"

        local fixy_cell
        if [[ -z "$fixy" ]]; then
            fixy_cell='[✗ NO-FIXY]'
        else
            fixy_cell="\`${fixy#"$scan_root"/}\`"
        fi

        local nd_cell="-" cx_cell="-" ne_cell="-" rq_cell="-" cb_cell="token"
        [[ "$nd" == "1" ]] && nd_cell="Y"
        [[ "$cx" == "1" ]] && cx_cell="Y"
        [[ "$ne" == "1" ]] && ne_cell="Y"
        [[ "$rq" == "1" ]] && rq_cell="Y"
        [[ "$cb" == "1" ]] && cb_cell="ctx"

        # FIXY-V-021: `cx=- (alloc)` is the documented carve-out for mints
        # that genuinely allocate (BPF load + perf_event_open + mmap, heap,
        # syscall) — CLAUDE.md §XXI rule says `constexpr` would lie about
        # the runtime cost.  The carve-out marker `// §XXI carve-out:
        # cx=alloc` placed above the mint signature surfaces the rationale
        # in the auditor's grep window.  A site that carries BOTH the
        # marker AND `constexpr` is a contradiction; we emit `Y` (constexpr
        # detection wins) and rely on review to flag it.
        if [[ "$cv" == "1" && "$cx" == "0" ]]; then
            cx_cell="- (alloc)"
        fi

        # FIXY-FOUND-082: `rq=- (pre)` is the documented carve-out for
        # mints whose gate is a P2900 `pre(...)` clause instead of a
        # `requires`-clause concept.  Used when the predicate is value-
        # dependent (inspects carrier runtime state, not just template
        # arguments) and cannot lift into a concept.  Canonical example:
        # `mint_view<Tag>(carrier)`'s `pre(view_ok(c, type_identity<Tag>))`.
        # SFINAE/concept machinery cannot inspect this gate; mint_view
        # appears always-callable until the call site fires the pre at
        # runtime/consteval.  The marker `// §XXI carve-out: rq=pre` placed
        # above the mint signature surfaces the rationale.
        if [[ "$cv_rq" == "1" && "$rq" == "0" ]]; then
            rq_cell="- (pre)"
        fi

        local hs14_cell="HS14: $hs14"
        if (( hs14 < 2 )); then
            hs14_cell="HS14: $hs14 ⚠"
        fi

        printf '| `%s` | `%s` | %s | %s | %s | %s | %s | %s | %s |\n' \
            "$name" "$fileline" "$nd_cell" "$cx_cell" "$ne_cell" \
            "$rq_cell" "$cb_cell" "$fixy_cell" "$hs14_cell"

        [[ -z "$fixy" ]] && violation_count=$((violation_count + 1))
    done <"$inventory_tmp"

    # ── Member-function mints (FIXY-U-118b Part 2) ───────────────────
    # Class-method mint factories live under a distinct section because
    # they cannot be `using`-re-exported at namespace scope.  See
    # scan_member_function_mints() above for the discovery + class-name
    # resolution algorithm.  The "fixy" cell is structurally inapplicable
    # for this surface — auditors verify §XXI nodiscard/constexpr/noexcept
    # compliance at the class-method declaration site itself.
    local mf_rows mf_count=0
    mf_rows="$(scan_member_function_mints | sort -t$'\t' -k1,1 -k2,2 -k3,3)"
    if [[ -n "$mf_rows" ]]; then
        printf '\n## Member-function mints\n\n'
        printf 'These §XXI mints are class methods, not namespace-level free\n'
        printf 'functions.  They CANNOT be `using`-re-exported through fixy::\n'
        printf '(a using-declaration moves a free-function NAME into another\n'
        printf 'namespace, but a member function'\''s authority is the object\n'
        printf 'whose method it is).  This section is the §XXI grep-target for\n'
        printf 'member-function mints — the inventory cell for "fixy re-export"\n'
        printf 'is structurally inapplicable, but `nd cx ne rq` compliance is\n'
        printf 'still audited, and HS14 fixture coverage is still counted.\n'
        printf '\n'
        printf 'The `cb` column carries `member` (instead of `ctx` / `token`)\n'
        printf 'to distinguish this third authorization shape.\n\n'
        printf '| class::mint_name | file:line | nd | cx | ne | rq | cb | HS14 |\n'
        printf '|---|---|---|---|---|---|---|---|\n'
        while IFS=$'\t' read -r class_name name fileline nd cx ne rq cb cv cv_rq; do
            [[ -z "$class_name" ]] && continue
            local mf_nd_cell="-" mf_cx_cell="-" mf_ne_cell="-" mf_rq_cell="-"
            [[ "$nd" == "1" ]] && mf_nd_cell="Y"
            [[ "$cx" == "1" ]] && mf_cx_cell="Y"
            [[ "$ne" == "1" ]] && mf_ne_cell="Y"
            [[ "$rq" == "1" ]] && mf_rq_cell="Y"

            # FIXY-V-021: §XXI alloc carve-out for member-function mints.
            # See substrate loop for full rationale.
            if [[ "$cv" == "1" && "$cx" == "0" ]]; then
                mf_cx_cell="- (alloc)"
            fi
            # FIXY-FOUND-082: §XXI rq=pre carve-out for member-function mints.
            if [[ "$cv_rq" == "1" && "$rq" == "0" ]]; then
                mf_rq_cell="- (pre)"
            fi

            local mf_hs14 mf_hs14_cell
            mf_hs14="$(hs14_count_for "$name")"
            mf_hs14_cell="HS14: $mf_hs14"
            (( mf_hs14 < 2 )) && mf_hs14_cell="HS14: $mf_hs14 ⚠"

            printf '| `%s::%s` | `%s` | %s | %s | %s | %s | %s | %s |\n' \
                "$class_name" "$name" "$fileline" "$mf_nd_cell" "$mf_cx_cell" \
                "$mf_ne_cell" "$mf_rq_cell" "member" "$mf_hs14_cell"
            mf_count=$((mf_count + 1))
        done <<<"$mf_rows"
    fi

    # ── fixy-origin mints (FIXY-V-271) ───────────────────────────────
    # Free-function mints whose canonical declaration lives in fixy/ with
    # no substrate counterpart.  See scan_fixy_mints() for the discovery +
    # substrate-name exclusion algorithm.  The "fixy re-export" cell is
    # structurally inapplicable (these ARE fixy), so it is omitted — but
    # §XXI nd/cx/ne/rq/cb compliance and HS14 fixture coverage are audited
    # exactly as for substrate mints.
    local fixy_rows fixy_origin_count=0 sub_names_tmp
    sub_names_tmp="$(mktemp)"
    cut -f2 "$inventory_tmp" | sort -u >"$sub_names_tmp"
    fixy_rows="$(scan_fixy_mints "$sub_names_tmp" \
                   | sort -t$'\t' -k1,1 -k2,2 | awk -F'\t' '!seen[$1]++')"
    rm -f "$sub_names_tmp"
    if [[ -n "$fixy_rows" ]]; then
        printf '\n## fixy-origin mints\n\n'
        printf 'These §XXI mints are declared canonically in `include/crucible/fixy/`\n'
        printf 'with no substrate counterpart (the hardware-axis grant mints, the\n'
        printf 'syscall/mmap/io factories, the durable-Cipher writers, the spawn\n'
        printf 'factories, the async-pipeline mints).  Because their origin IS\n'
        printf 'fixy::, the "fixy re-export" column is structurally inapplicable —\n'
        printf 'but `nd cx ne rq cb` §XXI compliance and HS14 fixture coverage are\n'
        printf 'audited exactly as for substrate mints.  Names that collide with a\n'
        printf 'substrate mint (forwarders, re-exports, or distinct same-named\n'
        printf 'mints) are listed in the substrate section instead.\n\n'
        printf '| mint_name | file:line | nd | cx | ne | rq | cb | HS14 |\n'
        printf '|---|---|---|---|---|---|---|---|\n'
        while IFS=$'\t' read -r name fileline nd cx ne rq cb cv cv_rq; do
            [[ -z "$name" ]] && continue
            local fo_nd_cell="-" fo_cx_cell="-" fo_ne_cell="-" fo_rq_cell="-" fo_cb_cell="token"
            [[ "$nd" == "1" ]] && fo_nd_cell="Y"
            [[ "$cx" == "1" ]] && fo_cx_cell="Y"
            [[ "$ne" == "1" ]] && fo_ne_cell="Y"
            [[ "$rq" == "1" ]] && fo_rq_cell="Y"
            [[ "$cb" == "1" ]] && fo_cb_cell="ctx"

            # FIXY-V-021: §XXI alloc carve-out — see substrate loop.
            if [[ "$cv" == "1" && "$cx" == "0" ]]; then
                fo_cx_cell="- (alloc)"
            fi
            # FIXY-FOUND-082: §XXI rq=pre carve-out — see substrate loop.
            if [[ "$cv_rq" == "1" && "$rq" == "0" ]]; then
                fo_rq_cell="- (pre)"
            fi

            local fo_hs14 fo_hs14_cell
            fo_hs14="$(hs14_count_for "$name")"
            fo_hs14_cell="HS14: $fo_hs14"
            (( fo_hs14 < 2 )) && fo_hs14_cell="HS14: $fo_hs14 ⚠"

            printf '| `%s` | `%s` | %s | %s | %s | %s | %s | %s |\n' \
                "$name" "$fileline" "$fo_nd_cell" "$fo_cx_cell" "$fo_ne_cell" \
                "$fo_rq_cell" "$fo_cb_cell" "$fo_hs14_cell"
            fixy_origin_count=$((fixy_origin_count + 1))
        done <<<"$fixy_rows"
    fi

    printf '\n'
    printf '## Summary\n\n'
    printf -- '- Total substrate mints: %d\n' "$(wc -l <"$inventory_tmp")"
    printf -- '- Missing fixy re-export: %d\n' "$violation_count"
    printf -- '- Member-function mints: %d (separate §XXI grep-target — see above)\n' "$mf_count"
    printf -- '- fixy-origin mints: %d (declared in fixy/, no substrate counterpart — see above)\n' "$fixy_origin_count"
    printf -- '- See `test/test_fixy_umbrella_reach.cpp` for the CI-enforced reach matrix.\n'
}

case "$mode" in
    stdout)
        emit_inventory
        ;;
    write)
        mkdir -p "$root/misc"
        emit_inventory >"$root/misc/mint-inventory.md"
        printf 'gen-mint-inventory: wrote misc/mint-inventory.md (%d mints across %d trees).\n' \
            "$(wc -l <"$inventory_tmp")" "${#trees[@]}" >&2
        ;;
    check)
        head_copy="$root/misc/mint-inventory.md"
        if [[ ! -f "$head_copy" ]]; then
            printf 'gen-mint-inventory: misc/mint-inventory.md missing — run without --check to create.\n' >&2
            exit 1
        fi
        # Strip the `Snapshot generated:` timestamp line from both sides;
        # it embeds wall-clock time and would always diff between runs.
        # CI cares about CONTENT drift, not when the file was last touched.
        if diff -q \
            <(grep -v '^Snapshot generated:' "$head_copy") \
            <(emit_inventory | grep -v '^Snapshot generated:') \
            >/dev/null 2>&1; then
            printf 'gen-mint-inventory: misc/mint-inventory.md is up-to-date.\n' >&2
            exit 0
        fi
        printf 'gen-mint-inventory: DRIFT — misc/mint-inventory.md out of date.  Run scripts/gen-mint-inventory.sh to refresh.\n' >&2
        diff -u \
            <(grep -v '^Snapshot generated:' "$head_copy") \
            <(emit_inventory | grep -v '^Snapshot generated:') | \
            head -40 >&2 || true
        exit 1
        ;;
esac
