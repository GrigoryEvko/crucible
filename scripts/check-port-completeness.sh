#!/usr/bin/env bash
# check-port-completeness.sh — every symbol a superseded header declares
# has a home in the new tree, or a written reason for having none.
#
# Three completed Stage A tasks were incomplete — A2.3, A9, A11.2 — and
# each was found by downstream work or by a one-off sweep, never by
# review.  This guard is the permanent form of that sweep.
#
# The input set is exactly the headers the project CLAIMS to have ported:
# every `_Foo.h` under include/crucible/, because the leading underscore
# is the superseded marking.  For each one, every public symbol it
# declares must exist by name somewhere under include/foundation/ or
# include/fixy/, or be listed in scripts/port-drops.txt with one sentence
# saying why it was not carried.  Anything else is a missing port, and the
# guard names the header and the symbol.
#
# How the old surface is enumerated — three methods, unioned
# ----------------------------------------------------------
# (1) Reflection.  A sentinel translation unit is generated that includes
#     every superseded header and walks std::meta::members_of over the
#     root namespace, recursing into public sub-namespaces.  Each member
#     with an identifier is attributed to its declaring file through
#     std::meta::source_location_of.  This is exact for types, templates,
#     aliases, functions, variables and concepts at namespace scope.
#     Three things members_of does NOT do, each covered elsewhere:
#       - it does not report names introduced by a using-declaration, so
#         a header whose surface is re-exports reflects as empty — method
#         (3) exists for that;
#       - it does not see through an alias to its target, which is the
#         behaviour wanted here: the alias is reported under its own
#         name and never dealiased, so a renamed alias is a miss;
#       - it reports a parametric template as a template, not a class,
#         so the walk tests has_identifier and nothing about kind.
#     Namespaces named detail or self_test, or ending in _self_test,
#     _smoke, _test or _layout, are the tree's private convention and are
#     not walked.  Identifiers beginning with two underscores are dropped:
#     they are compiler-synthesised (deduction guides) and never API.
#     A fourth blind spot, found while building this: source_location_of
#     reports the FIRST declaration in translation order, so a symbol
#     forward-declared by an earlier header is attributed there, and one
#     first declared by an unported header would leave the surface
#     silently.  Reflection therefore supplies the name set only; a
#     superseded header claims a name when its comment-stripped text
#     declares it, and every such rescue is printed as a NOTE line.
# (2) The preprocessor.  `-dD -E` over the same include list prints every
#     #define interleaved with line markers, so each macro is attributed
#     to its file exactly.  A regex over the source cannot tell a live
#     #define from one inside a false #if branch; the preprocessor can.
# (3) A brace-depth scan for `using a::b::c;` at namespace scope, the one
#     form of public surface neither compiler pass reports.
#
# A header whose reflection count is zero has its method stated as
# "macros+reexports only" in the per-header line; that is the macro-soup
# case (safety/_Pre.h, safety/_Post.h) and it is named rather than hidden.
# A header the sentinel cannot compile is not skipped: the guard exits 2
# with the compiler's diagnostic, because a guard that cannot measure must
# not report green.
#
# The full enumerated surface is printed before comparison (the input-set
# rule).  Pass --quiet to suppress it in scripted runs.
#
# How presence in the new tree is decided
# ---------------------------------------
# Every header under the new roots is comment-stripped by the compiler
# (`-fpreprocessed -dD -E -P`, which strips comments without processing
# directives), string literals are removed, and the remaining identifiers
# form the index.  A symbol is present if its exact identifier occurs
# anywhere in that index.  This is deliberately weak: presence BY NAME
# says nothing about signature, namespace or semantics, and a common name
# passes trivially.  The guard catches the A2.3 and A11.2 shape — a thing
# that was never carried at all — and makes no stronger claim.
#
# What is out of scope, stated so nobody infers it is covered
# -----------------------------------------------------------
#   - Member functions and nested members of a class are not enumerated.
#     A wrapper whose member surface shrank (A10.2 deleted value_mut on
#     purpose) is a different question from a symbol that vanished.
#   - Headers WITHOUT the underscore are not measured at all.  The guard
#     measures the claimed set; an unmarked header that was partly ported
#     is invisible here until it is marked (include/crucible/Saturate.h
#     was, from A2.2 until #179).  Marking is what puts a header under
#     this guard.
#
# scripts/port-drops.txt
# ----------------------
# One entry per line: `<old header path>:<symbol>  — <one sentence>.`
# The entry is keyed by symbol: it names any one superseded header that
# declares the symbol and covers every header that declares it, so a
# template dropped with its specialisations, or a class with its forward
# declarations, takes one sentence.  An entry is checked in both
# directions, so the file drains in lockstep with the code, the way the
# syscall allowlist does:
#   - STALE:    the symbol is no longer in that old header's surface;
#   - OBSOLETE: the symbol now exists in the new tree, so the drop is no
#               longer a drop.
# Either is exit 2.  The sentence must be non-empty and end with a period.
# `--emit-drops` prints a template line for every current miss.
#
# Exit codes
#   0 — clean: every superseded symbol is present or has a written drop
#   1 — at least one missing port (takes precedence over 2)
#   2 — stale or obsolete drop entry, malformed entry, sentinel would not
#       compile, missing dependency, or bad invocation
#
# Usage
#   check-port-completeness.sh [--quiet]     scan
#   check-port-completeness.sh --emit-drops  print template lines for misses
#   check-port-completeness.sh --self-test   plant controls and prove each
#                                            direction of failure
#
# Environment
#   CXX / CRUCIBLE_CXX          compiler for the sentinel (a GCC 16 with
#                               -freflection); CMake passes CXX
#   PORT_GUARD_OLD_ROOT         include root holding the old tree (include)
#   PORT_GUARD_OLD_SUBDIR       old subtree under that root (crucible)
#   PORT_GUARD_NEW_ROOTS        space-separated new roots
#                               (include/foundation include/fixy)
#   PORT_GUARD_NS               root namespace to walk (crucible)
#   PORT_GUARD_DROPS            drops file (scripts/port-drops.txt)

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

old_root="${PORT_GUARD_OLD_ROOT:-include}"
old_subdir="${PORT_GUARD_OLD_SUBDIR:-crucible}"
new_roots="${PORT_GUARD_NEW_ROOTS:-include/foundation include/fixy}"
walk_ns="${PORT_GUARD_NS:-crucible}"
drops_file="${PORT_GUARD_DROPS:-scripts/port-drops.txt}"
quiet=0

usage() {
    printf 'usage: %s [--quiet | --emit-drops | --self-test]\n' "${BASH_SOURCE[0]}" >&2
    exit 2
}

find_cxx() {
    if [[ -n "${CXX:-}" ]]; then
        printf '%s' "$CXX"
    elif [[ -n "${CRUCIBLE_CXX:-}" && -x "${CRUCIBLE_CXX}" ]]; then
        printf '%s' "${CRUCIBLE_CXX}"
    elif command -v g++ >/dev/null 2>&1; then
        printf '%s' g++
    else
        printf 'check-port-completeness: no compiler.  Set CXX or CRUCIBLE_CXX to a GCC 16.\n' >&2
        exit 2
    fi
}

# Flags the frozen tree compiles under.  No -Werror: the old tree's
# warnings are not this guard's business.  No -fno-exceptions: with
# contracts and reflection together it trips a known GCC 16 ICE in
# cp_fold, and the sentinel needs neither.
sentinel_flags=(-std=c++26 -freflection -fcontracts -w
                -DCRUCIBLE_FIXY_STRICT=1 -DCRUCIBLE_FP_STRICT_FLOOR=1)

# ── Enumeration ─────────────────────────────────────────────────────────

# Lists the superseded headers relative to $old_root, sorted.
list_superseded() {
    find "$old_root/$old_subdir" -type f -name '_*.h' | sed "s|^$old_root/||" | sort
}

# Writes the sentinel TU to $1 for the header list on stdin.
write_sentinel() {
    local out="$1"
    {
        printf '#include <meta>\n#include <cstdio>\n#include <string_view>\n'
        sed 's|^|#include <|; s|$|>|'
        cat <<'EOF'

namespace port_guard {

consteval bool is_private_ns(std::string_view id) noexcept {
    if (id == "detail" || id == "self_test") return true;
    for (std::string_view suffix : {"_self_test", "_smoke", "_test", "_layout"}) {
        if (id.size() > suffix.size() && id.substr(id.size() - suffix.size()) == suffix) return true;
    }
    return false;
}

consteval bool is_compiler_synthesised(std::string_view id) noexcept {
    return id.size() >= 2 && id[0] == '_' && id[1] == '_';
}

template <std::meta::info NS>
void walk() {
    static constexpr auto members =
        std::define_static_array(std::meta::members_of(NS, std::meta::access_context::unchecked()));
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_namespace(member)) {
            if constexpr (std::meta::has_identifier(member)) {
                if constexpr (!is_private_ns(std::meta::identifier_of(member))) walk<member>();
            }
        } else if constexpr (std::meta::has_identifier(member)) {
            constexpr std::string_view id = std::meta::identifier_of(member);
            if constexpr (!is_compiler_synthesised(id)) {
                constexpr auto where = std::meta::source_location_of(member);
                const char* kind = std::meta::is_template(member)   ? "template"
                                   : std::meta::is_type_alias(member) ? "alias"
                                   : std::meta::is_type(member)       ? "type"
                                   : std::meta::is_function(member)   ? "function"
                                   : std::meta::is_variable(member)   ? "variable"
                                   : std::meta::is_concept(member)    ? "concept"
                                                                      : "other";
                std::printf("%s\t%.*s\t%s\n", where.file_name(), static_cast<int>(id.size()), id.data(), kind);
            }
        }
    }
}

}  // namespace port_guard

EOF
        printf 'int main() { port_guard::walk<^^%s>(); }\n' "$walk_ns"
    } >"$out"
}

# Prints `header<TAB>symbol<TAB>reflection` for every namespace-scope
# member attributed to a superseded header.  Exits 2 if the sentinel
# does not compile.
enumerate_reflection() {
    local cxx="$1" tmp="$2" headers="$3"
    local tu="$tmp/sentinel.cpp" bin="$tmp/sentinel" log="$tmp/sentinel.log"
    write_sentinel "$tu" <"$headers"
    if ! "$cxx" "${sentinel_flags[@]}" -I"$old_root" -o "$bin" "$tu" >"$log" 2>&1; then
        printf 'check-port-completeness: the reflection sentinel did not compile, so the old surface\n' >&2
        printf '  cannot be enumerated.  A guard that cannot measure does not report green.\n' >&2
        printf '  compiler: %s\n' "$cxx" >&2
        grep -E 'error' "$log" | head -20 >&2
        exit 2
    fi
    "$bin" | awk -F'\t' -v root="$old_root/" '
        {
            f = $1
            if (index(f, root) == 1) f = substr(f, length(root) + 1)
            print f "\t" $2 "\t" $3
        }' | sort -u
}

# Reduces a header to the text reflection would consider: comments,
# string and character literals and preprocessor lines removed, then
# only the characters at namespace depth — outside every class body,
# function body and initialiser — and outside every private namespace
# (detail, self_test, an anonymous one, or a name ending in _self_test,
# _smoke, _test or _layout).  A brace opened by `namespace X {` keeps
# the depth; any other brace closes the text until its match.  The
# using-declaration scan and the attribution below both run over this,
# so a private smoke test, a class member or a call inside a body can
# never claim a public name.
namespace_scope_text() {
    local cxx="$1" src="$2"
    { "$cxx" -fpreprocessed -dD -E -P "$src" 2>/dev/null || true; } \
        | sed -E 's/"([^"\\]|\\.)*"//g; s/'"'"'([^'"'"'\\]|\\.)'"'"'//g' \
        | awk '
            /^[[:space:]]*#/ { if ($0 ~ /\\$/) cont = 1; next }
            cont == 1 { if ($0 !~ /\\$/) cont = 0; next }
            {
                line = $0; out = ""
                for (i = 1; i <= length(line); i++) {
                    c = substr(line, i, 1)
                    if (c == "{") {
                        before = substr(line, 1, i - 1)
                        if (match(before, /namespace[ \t]+[A-Za-z_:]+[ \t]*$/)) {
                            name = substr(before, RSTART, RLENGTH)
                            sub(/^namespace[ \t]+/, "", name); sub(/[ \t]*$/, "", name)
                            n = split(name, seg, "::"); last = seg[n]; priv = 0
                            for (k = 1; k <= n; k++) if (seg[k] == "detail") priv = 1
                            if (last == "self_test" || last ~ /(_self_test|_smoke|_test|_layout)$/) priv = 1
                            stack = stack (priv ? "p" : "n")
                        } else if (match(before, /namespace[ \t]*$/)) {
                            stack = stack "p"
                        } else {
                            stack = stack "b"
                        }
                        continue
                    }
                    if (c == "}") { stack = substr(stack, 1, length(stack) - 1); continue }
                    if (index(stack, "b") == 0 && index(stack, "p") == 0) out = out c
                }
                print out
            }'
}

# Attributes each reflected name to the superseded headers that declare
# it.  source_location_of reports the FIRST declaration in translation
# order, so a symbol forward-declared by an earlier header — including an
# unported one — is attributed there, and a symbol whose first declaration
# lives in an unported file would silently leave the surface.  The name
# set therefore comes from reflection and the attribution from the text:
# a superseded header claims a name when its namespace-scope text declares
# it (a type, alias, concept or variable declaration keyword followed by
# the name, or the name followed by an argument list).  The first-
# declaring file is kept as a claim too when it is itself superseded.  A
# name reflection first saw in an unported file and the text rescued is
# printed as a NOTE, so the fallback is visible.
#
# Reads `file<TAB>name<TAB>kind` on stdin; prints
# `header<TAB>name<TAB>reflection` and the NOTE lines to stderr.
attribute_reflection() {
    local tmp="$1" headers="$2"
    local stripped="$tmp/stripped"
    local refl="$tmp/refl_all.tsv"
    cat >"$refl"
    cut -f2 "$refl" | sort -u | while IFS= read -r name; do
        local first claims
        first=$(awk -F'\t' -v n="$name" '$2==n {print $1; exit}' "$refl")
        claims=$(grep -lE \
            "^[[:space:]]*(template[[:space:]]*<[^>]*>[[:space:]]*)?(struct|class|union|enum([[:space:]]+(class|struct))?|concept)[[:space:]]+(\[\[[^]]*\]\][[:space:]]*)?${name}\b|^[[:space:]]*using[[:space:]]+${name}[[:space:]]*=|\b(constexpr|consteval|constinit|inline)\b[^;=(]*\b${name}[[:space:]]*(=|\{|;)|\b${name}[[:space:]]*\(" \
            $(sed "s|^|$stripped/|" "$headers") 2>/dev/null | sed "s|^$stripped/||" || true)
        local first_is_superseded=0
        if grep -qxF "$first" "$headers"; then first_is_superseded=1; fi
        if [[ -z "$claims" && $first_is_superseded -eq 0 ]]; then
            continue  # declared only by unported files: out of scope
        fi
        if [[ -n "$claims" && $first_is_superseded -eq 0 ]]; then
            printf 'NOTE  %s is first declared in %s (not superseded); attributed by definition text to: %s\n' \
                "$name" "$first" "$(printf '%s' "$claims" | paste -sd, -)" >&2
        fi
        { [[ $first_is_superseded -eq 1 ]] && printf '%s\n' "$first"; printf '%s\n' "$claims"; } \
            | grep -v '^$' | sort -u | while IFS= read -r h; do printf '%s\t%s\treflection\n' "$h" "$name"; done
    done
}

# Prints `header<TAB>MACRO<TAB>macro` for every #define the preprocessor
# attributes to a superseded header.
enumerate_macros() {
    local cxx="$1" tmp="$2" headers="$3"
    local tu="$tmp/macros.cpp"
    sed 's|^|#include <|; s|$|>|' "$headers" >"$tu"
    { "$cxx" "${sentinel_flags[@]}" -I"$old_root" -dD -E "$tu" 2>/dev/null || true; } \
        | awk -v root="$old_root/" -v subdir="$old_subdir/" '
            /^# [0-9]+ "/ {
                f = $3; gsub(/"/, "", f)
                if (index(f, root) == 1) f = substr(f, length(root) + 1)
                current = f
                next
            }
            /^#define / {
                if (index(current, subdir) != 1) next
                n = split(current, parts, "/")
                if (parts[n] !~ /^_[A-Za-z0-9]+\.h$/) next
                name = $2; sub(/\(.*/, "", name)
                if (name ~ /^__/) next
                print current "\t" name "\tmacro"
            }' | sort -u
}

# Prints `header<TAB>symbol<TAB>reexport` for every `using a::b::c;` in
# a header's namespace-scope text (see namespace_scope_text), the one
# form of public surface neither compiler pass reports.
enumerate_reexports() {
    local tmp="$1" headers="$2" h
    while IFS= read -r h; do
        # A header with no using-declaration makes grep exit 1; under
        # pipefail that must not read as a failure of the scan.
        grep -oE '^[[:space:]]*using[[:space:]]+(::)?[A-Za-z_][A-Za-z0-9_]*(::[A-Za-z_][A-Za-z0-9_]*)+[[:space:]]*;' "$tmp/stripped/$h" 2>/dev/null \
            | sed -E 's/[[:space:]]*;.*//; s/.*::([A-Za-z_][A-Za-z0-9_]*)$/\1/' \
            | grep -vE '^__' | sort -u | sed "s|^|$h\t|; s|\$|\treexport|" || true
    done <"$headers" | sort -u
}

# Builds the identifier index of the new tree into $1.
build_new_index() {
    local cxx="$1" out="$2" root f
    : >"$out.raw"
    for root in $new_roots; do
        [[ -d "$root" ]] || continue
        while IFS= read -r f; do
            "$cxx" -fpreprocessed -dD -E -P "$f" 2>/dev/null >>"$out.raw" || true
        done < <(find "$root" -type f -name '*.h' | sort)
    done
    sed -E 's/"([^"\\]|\\.)*"//g' "$out.raw" \
        | grep -oE '\b[A-Za-z_][A-Za-z0-9_]*\b' | sort -u >"$out"
    rm -f "$out.raw"
}

# ── Drops file ──────────────────────────────────────────────────────────

# Parses the drops file into $1 as `header<TAB>symbol` and validates each
# entry against the surface ($2) and the new index ($3).  Sets stale_rc=2
# on any stale, obsolete or malformed entry.
load_drops() {
    local out="$1" surface="$2" index="$3" line key sentence header symbol
    : >"$out"
    [[ -f "$drops_file" ]] || return 0
    while IFS= read -r line || [[ -n "$line" ]]; do
        [[ -z "$line" || "$line" == \#* ]] && continue
        if [[ "$line" != *" — "* ]]; then
            printf 'MALFORMED DROP  %s\n  (expected `<header>:<symbol>  — <one sentence>.`)\n' "$line" >&2
            stale_rc=2; continue
        fi
        key="${line%% — *}"; key="${key%"${key##*[![:space:]]}"}"
        sentence="${line#* — }"; sentence="${sentence#"${sentence%%[![:space:]]*}"}"
        header="${key%%:*}"; symbol="${key#*:}"
        if [[ -z "$header" || -z "$symbol" || "$key" != *:* ]]; then
            printf 'MALFORMED DROP  %s\n  (no `<header>:<symbol>` key)\n' "$line" >&2
            stale_rc=2; continue
        fi
        if [[ -z "$sentence" || "$sentence" != *. ]]; then
            printf 'MALFORMED DROP  %s:%s\n  (the sentence after the em dash must be non-empty and end with a period)\n' "$header" "$symbol" >&2
            stale_rc=2; continue
        fi
        if ! grep -qP "^\Q${header}\E\t\Q${symbol}\E\t" "$surface"; then
            printf 'STALE DROP      %s  %s\n  (that header no longer declares this symbol — remove the entry)\n' "$header" "$symbol" >&2
            stale_rc=2; continue
        fi
        if grep -qxF "$symbol" "$index"; then
            printf 'OBSOLETE DROP   %s  %s\n  (the symbol now exists in the new tree — it is no longer a drop; remove the entry)\n' "$header" "$symbol" >&2
            stale_rc=2; continue
        fi
        printf '%s\t%s\n' "$header" "$symbol" >>"$out"
    done <"$drops_file"
}

# ── Scan ────────────────────────────────────────────────────────────────

stale_rc=0

scan() {
    local mode="${1:-scan}"
    local cxx tmp headers surface index drops misses
    cxx="$(find_cxx)"
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' RETURN
    headers="$tmp/headers.txt"
    list_superseded >"$headers"
    if [[ ! -s "$headers" ]]; then
        printf 'check-port-completeness: no superseded headers under %s/%s — nothing to measure.\n' "$old_root" "$old_subdir" >&2
        exit 2
    fi

    # The namespace-scope text of every superseded header, built once and
    # read by both the attribution and the re-export scan.
    local h
    while IFS= read -r h; do
        mkdir -p "$tmp/stripped/$(dirname "$h")"
        namespace_scope_text "$cxx" "$old_root/$h" >"$tmp/stripped/$h" || true
    done <"$headers"

    surface="$tmp/surface.tsv"
    {
        enumerate_reflection "$cxx" "$tmp" "$headers" | attribute_reflection "$tmp" "$headers"
        enumerate_macros "$cxx" "$tmp" "$headers"
        enumerate_reexports "$tmp" "$headers"
    } | sort -u >"$surface"

    index="$tmp/new_index.txt"
    build_new_index "$cxx" "$index"

    if [[ "$mode" == "scan" && $quiet -eq 0 ]]; then
        printf '== enumerated surface: %s superseded headers, %s (header, symbol, method) rows ==\n' \
            "$(wc -l <"$headers")" "$(wc -l <"$surface")"
        cat "$surface"
        printf '\n== method per header ==\n'
        while IFS= read -r h; do
            local r m x
            r=$(awk -F'\t' -v h="$h" '$1==h && $3=="reflection"' "$surface" | wc -l)
            m=$(awk -F'\t' -v h="$h" '$1==h && $3=="macro"' "$surface" | wc -l)
            x=$(awk -F'\t' -v h="$h" '$1==h && $3=="reexport"' "$surface" | wc -l)
            if [[ $r -eq 0 && $m -eq 0 && $x -eq 0 ]]; then
                printf '%s  reflection=0 macros=0 reexports=0  (no public surface: nothing to check)\n' "$h"
            elif [[ $r -eq 0 ]]; then
                printf '%s  reflection=0 macros=%s reexports=%s  (macros+reexports only: reflection found no namespace-scope members)\n' "$h" "$m" "$x"
            else
                printf '%s  reflection=%s macros=%s reexports=%s\n' "$h" "$r" "$m" "$x"
            fi
        done <"$headers"
        printf '\n== new-tree identifier index: %s distinct identifiers across %s ==\n\n' \
            "$(wc -l <"$index")" "$new_roots"
    fi

    drops="$tmp/drops.tsv"
    load_drops "$drops" "$surface" "$index"

    # A drop is keyed by symbol: one entry, naming any one header that
    # declares the symbol, covers every header that declares it (a
    # template and its specialisations, a class and its forward
    # declarations).  load_drops has already required the named header
    # to be one of the declarers.
    misses="$tmp/misses.tsv"
    cut -f1,2 "$surface" | sort -u | while IFS=$'\t' read -r h s; do
        grep -qxF "$s" "$index" && continue
        grep -qP "\t\Q${s}\E$" "$drops" && continue
        printf '%s\t%s\n' "$h" "$s"
    done >"$misses"

    if [[ "$mode" == "emit" ]]; then
        while IFS=$'\t' read -r h s; do
            printf '%s:%s  — <why this symbol has no home in the new tree>.\n' "$h" "$s"
        done <"$misses"
        return 0
    fi

    local count
    count=$(wc -l <"$misses")
    if [[ $count -gt 0 ]]; then
        while IFS=$'\t' read -r h s; do
            local methods
            methods=$(awk -F'\t' -v h="$h" -v s="$s" '$1==h && $2==s {print $3}' "$surface" | sort -u | paste -sd+ -)
            printf 'MISSING PORT    %s  %s  (%s)\n' "$h" "$s" "$methods"
        done <"$misses"
        printf '\ncheck-port-completeness: %s symbol(s) from superseded headers have no home in the new tree\n' "$count" >&2
        printf '  and no entry in %s.  Port each one, or write its sentence there.\n' "$drops_file" >&2
        printf '  `%s --emit-drops` prints a template line per miss.\n' "${BASH_SOURCE[0]}" >&2
        return 1
    fi
    if [[ $stale_rc -ne 0 ]]; then
        printf 'check-port-completeness: no missing ports, but %s has entries that no longer hold.\n' "$drops_file" >&2
        return 2
    fi
    printf 'check-port-completeness: clean — every symbol of %s superseded headers is present or has a written drop (%s drops).\n' \
        "$(wc -l <"$headers")" "$(wc -l <"$drops")"
    return 0
}

# ── Self-test ───────────────────────────────────────────────────────────

self_test() {
    local tmp rc out
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' RETURN
    mkdir -p "$tmp/old/crucible" "$tmp/new/foundation"

    # Minimal control: one ported symbol, one unported.  The guard must
    # report exactly the second.
    cat >"$tmp/old/crucible/_PlantedMinimal.h" <<'EOF'
#pragma once
namespace crucible {
struct planted_ported {};
struct planted_unported {};
}  // namespace crucible
EOF
    # Blind-spot controls, one per thing members_of does not report or
    # this guard must not mis-handle: a template, an alias that must be
    # reported under its own name, a re-export from a private namespace
    # that only the using-declaration scan can see, a macro, and a
    # private-namespace member that must NOT be reported.
    cat >"$tmp/old/crucible/_PlantedBlindSpots.h" <<'EOF'
#pragma once
#define PLANTED_MACRO_UNPORTED 1
namespace crucible {
struct planted_ported_base {};
template <class T> struct planted_template_unported {};
using planted_alias_unported = planted_ported_base;
namespace detail {
struct planted_reexport_unported {};
struct planted_private {};
}  // namespace detail
using detail::planted_reexport_unported;
}  // namespace crucible
EOF
    cat >"$tmp/new/foundation/Planted.h" <<'EOF'
#pragma once
struct planted_ported {};
struct planted_ported_base {};
EOF

    run_guard() {
        PORT_GUARD_OLD_ROOT="$tmp/old" PORT_GUARD_OLD_SUBDIR=crucible \
        PORT_GUARD_NEW_ROOTS="$tmp/new/foundation" PORT_GUARD_NS=crucible \
        PORT_GUARD_DROPS="$1" bash "${BASH_SOURCE[0]}" --quiet
    }

    local fails=0
    reported() { grep -E '^MISSING PORT' "$out" | awk '{print $4}' | LC_ALL=C sort | paste -sd, -; }

    # Arm 1: no drops file — the misses are exactly the planted unported set.
    out="$tmp/arm1.out"; set +e; run_guard "$tmp/no-such-drops.txt" >"$out" 2>&1; rc=$?; set -e
    local want="PLANTED_MACRO_UNPORTED,planted_alias_unported,planted_reexport_unported,planted_template_unported,planted_unported"
    if [[ $rc -eq 1 && "$(reported)" == "$want" ]]; then
        printf 'check-port-completeness --self-test: minimal control — planted_unported reported and planted_ported not, as expected.\n'
        printf 'check-port-completeness --self-test: blind spots — template, alias-by-own-name, re-export and macro each reported; the detail member was not, as expected.\n'
    else
        printf 'check-port-completeness --self-test: FAIL — expected exit 1 reporting {%s}, got exit %s reporting {%s}.\n' "$want" "$rc" "$(reported)" >&2
        sed 's/^/    /' "$out" >&2; fails=1
    fi

    # Arm 2: every miss has a written drop — clean.
    cat >"$tmp/drops-ok.txt" <<'EOF'
# planted
crucible/_PlantedMinimal.h:planted_unported  — planted by the self-test as a legitimate drop.
crucible/_PlantedBlindSpots.h:planted_template_unported  — planted.
crucible/_PlantedBlindSpots.h:planted_alias_unported  — planted.
crucible/_PlantedBlindSpots.h:planted_reexport_unported  — planted.
crucible/_PlantedBlindSpots.h:PLANTED_MACRO_UNPORTED  — planted.
EOF
    out="$tmp/arm2.out"; set +e; run_guard "$tmp/drops-ok.txt" >"$out" 2>&1; rc=$?; set -e
    if [[ $rc -eq 0 ]]; then
        printf 'check-port-completeness --self-test: written drops reported clean, as expected.\n'
    else
        printf 'check-port-completeness --self-test: FAIL — a fully dropped set was not clean (exit %s).\n' "$rc" >&2
        sed 's/^/    /' "$out" >&2; fails=1
    fi

    # Arm 3: a stale drop (symbol the header never declared) is exit 2.
    { cat "$tmp/drops-ok.txt"; printf 'crucible/_PlantedMinimal.h:planted_never_existed  — stale on purpose.\n'; } >"$tmp/drops-stale.txt"
    out="$tmp/arm3.out"; set +e; run_guard "$tmp/drops-stale.txt" >"$out" 2>&1; rc=$?; set -e
    if [[ $rc -eq 2 ]] && grep -q 'STALE DROP.*planted_never_existed' "$out"; then
        printf 'check-port-completeness --self-test: stale drop reported, as expected.\n'
    else
        printf 'check-port-completeness --self-test: FAIL — a stale drop was not reported as exit 2 (exit %s).\n' "$rc" >&2
        sed 's/^/    /' "$out" >&2; fails=1
    fi

    # Arm 4: an obsolete drop (symbol now present in the new tree) is exit 2.
    { cat "$tmp/drops-ok.txt"; printf 'crucible/_PlantedMinimal.h:planted_ported  — obsolete on purpose.\n'; } >"$tmp/drops-obsolete.txt"
    out="$tmp/arm4.out"; set +e; run_guard "$tmp/drops-obsolete.txt" >"$out" 2>&1; rc=$?; set -e
    if [[ $rc -eq 2 ]] && grep -q 'OBSOLETE DROP.*planted_ported' "$out"; then
        printf 'check-port-completeness --self-test: obsolete drop reported, as expected.\n'
    else
        printf 'check-port-completeness --self-test: FAIL — an obsolete drop was not reported as exit 2 (exit %s).\n' "$rc" >&2
        sed 's/^/    /' "$out" >&2; fails=1
    fi

    # Arm 5: a drop with no sentence is rejected.  The entry is not loaded,
    # so its symbol is then genuinely missing and exit 1 takes precedence
    # over 2, as the exit-code contract says; the arm asserts the MALFORMED
    # line fired and the run did not pass.
    { sed '$d' "$tmp/drops-ok.txt"; printf 'crucible/_PlantedBlindSpots.h:PLANTED_MACRO_UNPORTED  — \n'; } >"$tmp/drops-prose.txt"
    out="$tmp/arm5.out"; set +e; run_guard "$tmp/drops-prose.txt" >"$out" 2>&1; rc=$?; set -e
    if [[ $rc -ne 0 ]] && grep -q 'MALFORMED DROP.*PLANTED_MACRO_UNPORTED' "$out"; then
        printf 'check-port-completeness --self-test: sentence-less drop rejected, as expected.\n'
    else
        printf 'check-port-completeness --self-test: FAIL — a sentence-less drop was not rejected (exit %s).\n' "$rc" >&2
        sed 's/^/    /' "$out" >&2; fails=1
    fi

    if [[ $fails -ne 0 ]]; then
        printf 'check-port-completeness --self-test: FAIL.\n' >&2
        return 1
    fi
    printf 'check-port-completeness --self-test: PASS.\n'
}

# ── Entry ───────────────────────────────────────────────────────────────

case "${1:-}" in
    "")            scan ;;
    --quiet)       quiet=1; scan ;;
    --emit-drops)  quiet=1; scan emit ;;
    --self-test)   self_test ;;
    *)             usage ;;
esac
