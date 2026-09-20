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
#     The walk carries the namespace path down the recursion and prints it
#     with each member, because presence is decided by qualified name.
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
# Two indices are built over the new roots, and a symbol is judged against
# the stronger of the two that applies to it.
#
# (a) The qualified index.  A second sentinel includes every header under
#     the new roots and walks the new root namespaces, printing
#     `namespace<TAB>symbol` for each namespace-scope member.  Private
#     namespaces are INCLUDED here, unlike on the old side: the question
#     is where the new tree declares a name, and a name the port put in a
#     detail namespace is still declared.  Namespace ALIASES are not
#     recursed into, because an alias pointing at an ancestor makes the
#     walk call itself for ever at run time.
# (b) The bare index.  Every new-tree header is comment-stripped by the
#     compiler (`-fpreprocessed -dD -E -P`, which strips comments without
#     processing directives), string literals are removed, and the
#     remaining identifiers form a set.  This is the old test, kept for
#     the cases (a) cannot speak about.
#
# Comparing the two qualified names directly does not work, because the
# port relocated namespaces on purpose. crucible::safety::Linear is
# fixy::Linear, and crucible::algebra::Graded is
# foundation::algebra::Graded.  Measured over the whole surface, a direct
# comparison calls 1058 present symbols missing.  So the correspondence
# between old and new namespaces is MEASURED instead: for each old
# namespace, the new namespaces that hold at least PORT_GUARD_NS_-
# CORROBORATION (default two) of its symbols are the ones that received
# it, and one of those must declare the symbol.  One name landing
# somewhere is coincidence; two are a correspondence.
#
# What this catches that a bare name cannot: fixy::fs::flag::Path, the
# O_PATH open flag, was never carried, and the identifier Path occurs in
# the new tree as fixy::Path, the sanitized-path wrapper.  Its seven
# sibling tags went to fixy::fs::flag, which is therefore where Path had
# to go, and it is not there.  The bare test called it present for months.
#
# Where the bare test still decides, by construction:
#   - a macro has no namespace, because the preprocessor runs before the
#     language has them.  A macro is matched by (header, name) in the
#     drops file and by bare name in the new tree, and that asymmetry is
#     deliberate rather than an oversight;
#   - a re-export carries the namespace it is re-exported INTO, which the
#     brace scan does not track, so it keeps the bare test too;
#   - an old namespace with fewer than the corroboration floor of its
#     symbols in any one new namespace teaches nothing, so its symbols
#     keep the bare test.  Thirty-three surface rows are in that case;
#   - a name the text rescued into a header takes the union of the
#     namespaces reflection saw for it, because the text scan knows the
#     name is declared there and not in which namespace.
#
# A symbol declared in two namespaces of one header needs a home for
# each, not for one of them: fixy::fs::Path having a successor says
# nothing about fixy::fs::flag::Path.  Eight rows of the current surface
# are in that case.
#
# Neither index says anything about signature or semantics, and the
# qualified one says nothing about a hidden friend, which is a
# declaration members_of does not report (crucible::safety::drop became a
# hidden friend of fixy::Qtt and reads as absent).  The guard catches the
# shape it was built for, a thing that was never carried at all, plus the
# shape a shared name used to hide. It makes no stronger claim.
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
# The entry is keyed by the (header, symbol) pair.  Every declaration of
# that name inside that header is covered by the one row, so a template
# dropped with its specialisations, or a class with its forward
# declarations, takes one sentence. A name several superseded headers
# declare takes one row per header, because a bare name is not an
# identity.  Keyed by the name alone, one row silenced every header that
# declared it: three unrelated runtime_smoke_test functions in three
# namespaces shared one sentence.  An entry is checked in both
# directions, so the file drains in lockstep with the code, the way the
# syscall allowlist does:
#   - STALE:    the symbol is no longer in that old header's surface;
#   - OBSOLETE: the symbol now exists in the new tree, so the drop is no
#               longer a drop.
# Either is exit 2.  The sentence must be non-empty and end with a period.
# `--emit-drops` prints a template line for every current miss.
#
# The second half of a trait's identity: specialisation folds
# ----------------------------------------------------------
# Everything above compares NAMES.  A trait's identity is its name plus
# what it ANSWERS, and the two halves fail separately.  A trait can
# survive the port by name, lose every specialisation, and answer the same
# thing for every argument.  The enforcement it carried is then gone and
# no name is missing, so the scan above reports clean.  That is how
# fixy/Qtt.h shipped two discipline tables whose four old specialisations
# became zero, making linearity a tautology (task #193).
#
# So this guard also counts specialisations on both sides.  A
# specialisation is a class, struct or union declaration whose name is
# immediately followed by `<`; the primary is the same spelling without
# it.  Both counts read the namespace-scope text, which is the compiler's
# own output, so a comment, a string literal, a class body and a private
# namespace contribute nothing and a macro-generated specialisation has
# already expanded.
#
# A name is REQUIRED TO BE JUSTIFIED when all three hold:
#   - the old superseded surface specialises it at least once;
#   - the new tree declares a primary for it;
#   - the new tree specialises it zero times.
# Its row lives in scripts/port-folds.txt, keyed by
# `<new header>:<trait>` because the fold is a property of the NEW trait,
# and checked in both directions like a drop:
#   - STALE:    that header declares no primary for that name any more, or
#               the name is no longer one the old tree specialises;
#   - OBSOLETE: the new tree now specialises it, so no fold is left.
# `--emit-folds` prints a template line for every unjustified fold.
#
# WHY A COUNT AND NOT A COMPARISON OF ANSWERS.  Comparing what the two
# trees answer at the old argument lists is the property anyone actually
# depends on, and it is not reachable here.  P2996 offers no
# specialisations_of query and a specialisation is not a namespace member:
# a walk over a namespace holding one primary and three specialisations,
# two explicit and one partial, reports exactly ONE member, measured
# 2026-09-20.  Mapping each old argument list onto its new spelling is
# also the rename problem port-drops.txt solves by hand.  So the count is
# the trigger and the written sentence is the evidence, the same shape the
# drops channel already uses.  A sentence is a claim a person reviews, not
# a proof, and this file says so rather than implying more.
#
# Exit codes
#   0 — clean: every superseded symbol is present or has a written drop,
#       and every fold has a written sentence
#   1 — at least one missing port or unjustified fold (takes precedence
#       over 2)
#   2 — stale or obsolete drop or fold entry, malformed entry, sentinel
#       would not compile, missing dependency, or bad invocation
#
# Usage
#   check-port-completeness.sh [--quiet]     scan
#   check-port-completeness.sh --emit-drops  print template lines for misses
#   check-port-completeness.sh --emit-folds  print template lines for folds
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
#   PORT_GUARD_NEW_NS           root namespaces of the new tree, space
#                               separated (foundation fixy)
#   PORT_GUARD_NS_CORROBORATION symbols one new namespace must hold before
#                               it counts as having received an old one (2)
#   PORT_GUARD_DROPS            drops file (scripts/port-drops.txt)
#   PORT_GUARD_FOLDS            folds file (scripts/port-folds.txt)

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

old_root="${PORT_GUARD_OLD_ROOT:-include}"
old_subdir="${PORT_GUARD_OLD_SUBDIR:-crucible}"
new_roots="${PORT_GUARD_NEW_ROOTS:-include/foundation include/fixy}"
walk_ns="${PORT_GUARD_NS:-crucible}"
new_ns="${PORT_GUARD_NEW_NS:-foundation fixy}"
corroboration="${PORT_GUARD_NS_CORROBORATION:-2}"
drops_file="${PORT_GUARD_DROPS:-scripts/port-drops.txt}"
folds_file="${PORT_GUARD_FOLDS:-scripts/port-folds.txt}"
quiet=0

usage() {
    printf 'usage: %s [--quiet | --emit-drops | --emit-folds | --self-test]\n' "${BASH_SOURCE[0]}" >&2
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
        printf '#include <meta>\n#include <cstdio>\n#include <string>\n#include <string_view>\n'
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

// A namespace alias is not recursed into.  An alias whose target is an
// ancestor (namespace eff = ::foundation::effects; inside a namespace
// under effects) makes walk<A> call walk<B> and walk<B> call walk<A>:
// two instantiations, so it compiles, and unbounded recursion at run
// time, so it dies on the stack.  The entity behind the alias is reached
// through its own name anyway.
template <std::meta::info NS>
void walk(std::string const& path) {
    static constexpr auto members =
        std::define_static_array(std::meta::members_of(NS, std::meta::access_context::unchecked()));
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_namespace(member) && !std::meta::is_namespace_alias(member)) {
            if constexpr (std::meta::has_identifier(member)) {
                constexpr std::string_view ns = std::meta::identifier_of(member);
                if constexpr (!is_private_ns(ns)) walk<member>(path + "::" + std::string(ns));
            }
        } else if constexpr (!std::meta::is_namespace(member) && std::meta::has_identifier(member)) {
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
                std::printf("%s\t%.*s\t%s\t%s\n", where.file_name(), static_cast<int>(id.size()), id.data(), kind,
                            path.c_str());
            }
        }
    }
}

}  // namespace port_guard

EOF
        printf 'int main() { port_guard::walk<^^%s>("%s"); }\n' "$walk_ns" "$walk_ns"
    } >"$out"
}

# Prints `header<TAB>symbol<TAB>reflection<TAB>namespace` for every
# namespace-scope member attributed to a superseded header.  Exits 2 if
# the sentinel does not compile.
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
            print f "\t" $2 "\t" $3 "\t" $4
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
        # The namespace of the declaration travels with it, because the
        # presence test compares a qualified name.  A header takes the
        # namespaces reflection saw for the name IN that header; a header
        # the text rescued takes the union, because the text scan knows
        # the name is declared there and not which namespace it sits in.
        { [[ $first_is_superseded -eq 1 ]] && printf '%s\n' "$first"; printf '%s\n' "$claims"; } \
            | grep -v '^$' | sort -u | while IFS= read -r h; do
                local spaces
                spaces=$(awk -F'\t' -v n="$name" -v h="$h" '$2==n && $1==h {print $4}' "$refl" | sort -u)
                [[ -z "$spaces" ]] && spaces=$(awk -F'\t' -v n="$name" '$2==n {print $4}' "$refl" | sort -u)
                while IFS= read -r ns; do
                    [[ -z "$ns" ]] && continue
                    printf '%s\t%s\treflection\t%s\n' "$h" "$name" "$ns"
                done <<<"$spaces"
            done
    done
}

# Prints `header<TAB>MACRO<TAB>macro<TAB>-` for every #define the
# preprocessor attributes to a superseded header.  The fourth column is
# the namespace, and a macro has none: the preprocessor runs before the
# language has namespaces at all.  A macro therefore keeps the bare-name
# presence test, which is the asymmetry stated in the file header.
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
                print current "\t" name "\tmacro\t-"
            }' | sort -u
}

# Prints `header<TAB>symbol<TAB>reexport<TAB>-` for every `using a::b::c;`
# in a header's namespace-scope text (see namespace_scope_text), the one
# form of public surface neither compiler pass reports.  The fourth column
# is the namespace the name is re-exported INTO, and the brace scan does
# not track which namespace is open, so it is `-` and the symbol keeps the
# bare-name presence test.  Stated as a limit in the file header.
enumerate_reexports() {
    local tmp="$1" headers="$2" h
    while IFS= read -r h; do
        # A header with no using-declaration makes grep exit 1; under
        # pipefail that must not read as a failure of the scan.
        grep -oE '^[[:space:]]*using[[:space:]]+(::)?[A-Za-z_][A-Za-z0-9_]*(::[A-Za-z_][A-Za-z0-9_]*)+[[:space:]]*;' "$tmp/stripped/$h" 2>/dev/null \
            | sed -E 's/[[:space:]]*;.*//; s/.*::([A-Za-z_][A-Za-z0-9_]*)$/\1/' \
            | grep -vE '^__' | sort -u | sed "s|^|$h\t|; s|\$|\treexport\t-|" || true
    done <"$headers" | sort -u
}

# Writes the new-tree sentinel to $1 for the header list on stdin.  It
# walks the new root namespaces and prints `namespace<TAB>symbol` for
# every namespace-scope member, private namespaces INCLUDED: the question
# this side answers is where the new tree declares a name, and a name the
# port put inside a detail namespace is still declared.  The old side
# skips private namespaces for the opposite reason. A private name was
# never public surface owed a port.
write_new_sentinel() {
    local out="$1" ns
    {
        printf '#include <meta>\n#include <cstdio>\n#include <string>\n#include <string_view>\n'
        sed 's|^|#include <|; s|$|>|'
        cat <<'EOF'

namespace port_guard_new {

template <std::meta::info NS>
void walk(std::string const& path) {
    static constexpr auto members =
        std::define_static_array(std::meta::members_of(NS, std::meta::access_context::unchecked()));
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_namespace(member) && !std::meta::is_namespace_alias(member)) {
            if constexpr (std::meta::has_identifier(member)) {
                constexpr std::string_view ns = std::meta::identifier_of(member);
                walk<member>(path + "::" + std::string(ns));
            }
        } else if constexpr (!std::meta::is_namespace(member) && std::meta::has_identifier(member)) {
            constexpr std::string_view id = std::meta::identifier_of(member);
            if constexpr (!(id.size() >= 2 && id[0] == '_' && id[1] == '_')) {
                std::printf("%s\t%.*s\n", path.c_str(), static_cast<int>(id.size()), id.data());
            }
        }
    }
}

}  // namespace port_guard_new

EOF
        printf 'int main() {'
        for ns in $new_ns; do printf ' port_guard_new::walk<^^%s>("%s");' "$ns" "$ns"; done
        printf ' }\n'
    } >"$out"
}

# Builds the qualified index of the new tree into $1 as
# `namespace<TAB>symbol`.  Exits 2 if the sentinel does not compile.
build_new_pairs() {
    local cxx="$1" out="$2" tmp="$3" root
    local tu="$tmp/new_sentinel.cpp" bin="$tmp/new_sentinel" log="$tmp/new_sentinel.log"
    local list="$tmp/new_headers.txt"
    : >"$list"
    for root in $new_roots; do
        [[ -d "$root" ]] || continue
        find "$root" -type f -name '*.h' | sed -E "s|^$(dirname "$root")/||" | sort >>"$list"
    done
    if [[ ! -s "$list" ]]; then
        printf 'check-port-completeness: no headers under the new roots (%s). Nothing to compare against.\n' "$new_roots" >&2
        exit 2
    fi
    write_new_sentinel "$tu" <"$list"
    if ! "$cxx" "${sentinel_flags[@]}" $(for root in $new_roots; do printf -- '-I%s ' "$(dirname "$root")"; done) \
            -o "$bin" "$tu" >"$log" 2>&1; then
        printf 'check-port-completeness: the new-tree sentinel did not compile, so presence cannot be decided\n' >&2
        printf '  by qualified name.  A guard that cannot measure does not report green.\n' >&2
        printf '  compiler: %s\n' "$cxx" >&2
        grep -E 'error' "$log" | head -20 >&2
        exit 2
    fi
    "$bin" | sort -u >"$out"
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

# ── Presence ────────────────────────────────────────────────────────────

# Decides presence for every (header, symbol) on the surface and writes
# `header<TAB>symbol<TAB>present|absent<TAB>rule` to $1.
#
# The port relocated namespaces on purpose. crucible::safety::Linear is
# fixy::Linear and crucible::algebra::Graded is
# foundation::algebra::Graded, so comparing the two qualified names
# directly reports nearly every ported symbol missing.  The correspondence is therefore MEASURED, not
# assumed: for each old namespace, the new namespaces that received its
# symbols are the ones that hold at least $corroboration of them by name.
# A symbol of that old namespace is present when one of those receiving
# namespaces declares it.  One name landing somewhere is not a
# correspondence; two are, which is why the floor is two.
#
# What this catches that a bare name cannot: fixy::fs::flag::Path (the
# O_PATH tag) is not ported, and the identifier Path occurs in the new
# tree as fixy::Path, the sanitized-path wrapper.  The flag namespace
# ported its other seven tags to fixy::fs::flag, so that is where Path
# had to land, and it did not.
#
# Where it falls back to the bare name, by construction:
#   - a macro and a re-export carry no namespace (column four is `-`);
#   - an old namespace with fewer than $corroboration symbols in any one
#     new namespace teaches nothing, so its symbols keep the old test;
#   - a symbol reflection reports in more than one namespace, or one the
#     text rescued, is present if ANY of its namespaces accepts it.
decide_presence() {
    local out="$1" surface="$2" pairs="$3" index="$4"
    awk -F'\t' -v pairs="$pairs" -v idxfile="$index" -v floor="$corroboration" '
        FILENAME == pairs {
            np[$1 SUBSEP $2] = 1
            holders[$2] = holders[$2] " " $1
            next
        }
        FILENAME == idxfile { bare[$1] = 1; next }
        {
            key = $1 SUBSEP $2
            if (!(key in row)) { row[key] = 1; order[++n] = key; hdr[key] = $1; sym[key] = $2 }
            spaces[key] = spaces[key] " " $4
            if ($4 != "-") { oldns[$4] = 1; oldmember[$4 SUBSEP $2] = 1 }
        }
        END {
            # Learn where each old namespace sent its symbols.
            for (k in oldmember) {
                split(k, part, SUBSEP)
                o = part[1]; s = part[2]
                cnt = split(holders[s], hs, " ")
                for (i = 1; i <= cnt; i++) if (hs[i] != "") hits[o SUBSEP hs[i]]++
            }
            for (k in hits) {
                if (hits[k] < floor) continue
                split(k, part, SUBSEP)
                target[part[1]] = target[part[1]] " " part[2]
            }
            # Every namespace the symbol is declared in must have a home,
            # not just one of them: a header that declares one name twice
            # declares two things, and fixy::fs::Path (an alias for the
            # sanitized path) having a home says nothing about
            # fixy::fs::flag::Path (the O_PATH tag).  Eight pairs on the
            # current surface are declared in two namespaces.
            for (i = 1; i <= n; i++) {
                key = order[i]
                verdict = "present"; rule = "qualified"; counted = 0
                cnt = split(spaces[key], sp, " ")
                for (j = 1; j <= cnt; j++) {
                    p = sp[j]
                    if (p == "") continue
                    if (seen[key SUBSEP p]++) continue
                    counted = 1
                    here = 0
                    if (p == "-" || target[p] == "") {
                        rule = "bare"
                        if (bare[sym[key]]) here = 1
                    } else {
                        tc = split(target[p], tg, " ")
                        for (t = 1; t <= tc; t++) {
                            if (tg[t] == "") continue
                            if ((tg[t] SUBSEP sym[key]) in np) { here = 1; break }
                        }
                    }
                    if (!here) verdict = "absent"
                }
                if (!counted) {
                    rule = "bare"
                    verdict = (sym[key] in bare) ? "present" : "absent"
                }
                print hdr[key] "\t" sym[key] "\t" verdict "\t" rule
            }
        }' "$pairs" "$index" "$surface" | sort -u >"$out"
}

# Reads $2 (the verdict file) and prints present or absent for one pair.
verdict_of() {
    local verdicts="$1" header="$2" symbol="$3"
    awk -F'\t' -v h="$header" -v s="$symbol" '$1==h && $2==s {print $3; exit}' "$verdicts"
}

# ── Drops file ──────────────────────────────────────────────────────────

# Parses the drops file into $1 as `header<TAB>symbol` and validates each
# entry against the surface ($2) and the presence verdicts ($3).  Sets
# stale_rc=2 on any stale, obsolete or malformed entry.  The obsolete test
# reads the same verdict the miss loop reads, so a row can never be both
# refused as obsolete and required as a miss.
load_drops() {
    local out="$1" surface="$2" verdicts="$3" line key sentence header symbol
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
        if [[ "$(verdict_of "$verdicts" "$header" "$symbol")" == "present" ]]; then
            printf 'OBSOLETE DROP   %s  %s\n  (the symbol now exists in the new tree — it is no longer a drop; remove the entry)\n' "$header" "$symbol" >&2
            stale_rc=2; continue
        fi
        printf '%s\t%s\n' "$header" "$symbol" >>"$out"
    done <"$drops_file"
}

# ── Specialisation folds ────────────────────────────────────────────────

# The text a template count reads.
#
# Deliberately NOT namespace_scope_text.  That one strips private
# namespaces and class bodies, which is right for a PUBLIC surface and
# wrong here: a trait in a detail namespace folds exactly like a public
# one, and foundation::effects::detail::extract_admits_payload is one of
# them.  Stripping detail would hide the very shape this check exists for.
# What is removed is only what would be miscounted — comments, through the
# compiler's own -fpreprocessed pass, and string literals.
template_text() {
    local cxx="$1" src="$2"
    { "$cxx" -fpreprocessed -dD -E -P "$src" 2>/dev/null || true; } \
        | sed -E 's/"([^"\\]|\\.)*"//g'
}

# Prints `S|P<TAB>file<TAB>name` for every class-template declaration in
# the files listed on stdin, each resolved under $2 and printed under the
# spelling it was listed with.
#
# S is a specialisation: the name is immediately followed by `<`, whether
# the `template <...>` prefix sits on the same line or the line above.
# P is a primary: the name is followed by `:`, `{`, `;`, or the end of the
# line, which is where an opening brace lands after the strip.  The
# end-of-line arm is why `struct Foo {` is seen at all; without it every
# primary with an inline body would read as absent.
tally_templates() {
    local cxx="$1" root="$2" f path
    while IFS= read -r f; do
        # The old list is relative to an include root; the new list is
        # already openable and, under the self-test's planted roots,
        # absolute.  Prefixing an absolute path with the root yields a
        # path that opens nothing, and the loop then silently tallies an
        # empty tree — which is a guard reporting green because it
        # measured nothing.
        if [[ "$f" == /* ]]; then path="$f"; else path="$root/$f"; fi
        [[ -f "$path" ]] || continue
        template_text "$cxx" "$path" | awk -v file="$f" '
            {
                if (match($0, /^[[:space:]]*(template[[:space:]]*<.*>[[:space:]]*)?(struct|class|union)[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]*</)) {
                    s = substr($0, RSTART, RLENGTH)
                    sub(/^[[:space:]]*(template[[:space:]]*<.*>[[:space:]]*)?(struct|class|union)[[:space:]]+/, "", s)
                    sub(/[[:space:]]*<$/, "", s)
                    print "S\t" file "\t" s
                    next
                }
                if (match($0, /^[[:space:]]*(struct|class|union)[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]*(final[[:space:]]*)?([:{;]|$)/)) {
                    s = substr($0, RSTART, RLENGTH)
                    sub(/^[[:space:]]*(struct|class|union)[[:space:]]+/, "", s)
                    sub(/[[:space:]]*(final[[:space:]]*)?[:{;]?$/, "", s)
                    if (s != "") print "P\t" file "\t" s
                }
            }'
    done
}

# Writes `new header<TAB>trait<TAB>old specialisation count` to $1 for
# every trait that must carry a fold sentence: the old surface specialises
# it, the new tree declares a primary, and the new tree specialises it
# zero times.
#
# One row per (new header declaring the primary, name).  Two new headers
# can declare one primary — a forward declaration and its definition — and
# each is a place a reader looks for the trait, so each takes a row rather
# than one row standing for both.
fold_requirements() {
    local out="$1" old_tally="$2" new_tally="$3"
    awk -F'\t' -v oldf="$old_tally" -v newf="$new_tally" '
        BEGIN {
            while ((getline line < oldf) > 0) {
                split(line, a, "\t")
                if (a[1] == "S") old_spec[a[3]]++
            }
            while ((getline line < newf) > 0) {
                split(line, a, "\t")
                if (a[1] == "S") new_spec[a[3]] = 1
                else if (a[1] == "P") home[a[3] SUBSEP a[2]] = 1
            }
            for (k in home) {
                split(k, p, SUBSEP)
                name = p[1]; file = p[2]
                if (!(name in old_spec)) continue
                if (name in new_spec) continue
                printf "%s\t%s\t%s\n", file, name, old_spec[name]
            }
        }' </dev/null | sort -u >"$out"
}

# Parses the folds file into $1 as `header<TAB>trait` and validates each
# entry against the requirement set ($2) and the new tally ($3).  Sets
# stale_rc=2 on any stale, obsolete or malformed entry.  A row that is not
# a requirement is OBSOLETE when the new tree specialises the trait again
# and STALE otherwise, because the two ask the author for different edits.
load_folds() {
    local out="$1" reqs="$2" new_tally="$3" line key sentence header name
    : >"$out"
    [[ -f "$folds_file" ]] || return 0
    while IFS= read -r line || [[ -n "$line" ]]; do
        [[ -z "$line" || "$line" == \#* ]] && continue
        if [[ "$line" != *" — "* ]]; then
            printf 'MALFORMED FOLD  %s\n  (expected `<new header>:<trait>  — <one sentence>.`)\n' "$line" >&2
            stale_rc=2; continue
        fi
        key="${line%% — *}"; key="${key%"${key##*[![:space:]]}"}"
        sentence="${line#* — }"; sentence="${sentence#"${sentence%%[![:space:]]*}"}"
        header="${key%%:*}"; name="${key##*:}"
        if [[ -z "$header" || -z "$name" || "$key" != *:* ]]; then
            printf 'MALFORMED FOLD  %s\n  (no `<new header>:<trait>` key)\n' "$line" >&2
            stale_rc=2; continue
        fi
        if [[ -z "$sentence" || "$sentence" != *. ]]; then
            printf 'MALFORMED FOLD  %s:%s\n  (the sentence after the em dash must be non-empty and end with a period)\n' "$header" "$name" >&2
            stale_rc=2; continue
        fi
        if ! grep -qP "^\Q${header}\E\t\Q${name}\E\t" "$reqs"; then
            if grep -qP "^S\t[^\t]*\t\Q${name}\E$" "$new_tally"; then
                printf 'OBSOLETE FOLD   %s  %s\n  (the new tree specializes this trait again — there is no fold left to justify; remove the entry)\n' "$header" "$name" >&2
            else
                printf 'STALE FOLD      %s  %s\n  (that header declares no primary of this name the old tree specialized — remove the entry)\n' "$header" "$name" >&2
            fi
            stale_rc=2; continue
        fi
        printf '%s\t%s\n' "$header" "$name" >>"$out"
    done <"$folds_file"
}

# ── Scan ────────────────────────────────────────────────────────────────

stale_rc=0

scan() {
    local mode="${1:-scan}"
    local cxx tmp headers surface index pairs verdicts drops misses
    local new_headers old_tally new_tally fold_reqs folds fold_misses
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

    pairs="$tmp/new_pairs.tsv"
    build_new_pairs "$cxx" "$pairs" "$tmp"

    verdicts="$tmp/verdicts.tsv"
    decide_presence "$verdicts" "$surface" "$pairs" "$index"

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
        printf '\n== new-tree identifier index: %s distinct identifiers across %s ==\n' \
            "$(wc -l <"$index")" "$new_roots"
        printf '== new-tree qualified index: %s (namespace, symbol) pairs in %s namespaces ==\n\n' \
            "$(wc -l <"$pairs")" "$(cut -f1 "$pairs" | sort -u | wc -l)"
    fi

    drops="$tmp/drops.tsv"
    load_drops "$drops" "$surface" "$verdicts"

    # A drop is keyed by (header, symbol).  It used to be keyed by symbol
    # alone, so one entry covered every header declaring that NAME. A bare
    # name is not an identity.  Three unrelated `runtime_smoke_test`
    # functions in three namespaces shared one row, and the row for the
    # effects aliases silenced the polynomial one.  A symbol genuinely
    # declared by several headers now takes one row per header, which is
    # one row per fact rather than one row hiding several.  load_drops has
    # already required the named header to declare the symbol.
    misses="$tmp/misses.tsv"
    awk -F'\t' -v drops="$drops" '
        FILENAME == drops { dropped[$1 SUBSEP $2] = 1; next }
        $3 == "absent" && !(($1 SUBSEP $2) in dropped) { print $1 "\t" $2 }
    ' "$drops" "$verdicts" | sort -u >"$misses"

    # The other half of a trait's identity.  The two tallies read their
    # own strip rather than the one above, for the reason template_text
    # states.  The new list is repo-relative, because a fold row names a
    # file a reader opens rather than a path inside an include root.
    new_headers="$tmp/new_header_paths.txt"
    : >"$new_headers"
    local nroot
    for nroot in $new_roots; do
        [[ -d "$nroot" ]] || continue
        find "$nroot" -type f -name '*.h' | sort >>"$new_headers"
    done
    # Plain `sort`, never `sort -u`.  Every specialisation of one trait in
    # one file produces the same line, so a dedupe here would turn the
    # count into a presence bit: _Tagged.h's eighteen retag_policy
    # specialisations collapsed to one and the report said the old tree
    # specialised it twice, once per file, instead of twenty-one times.
    # The verdict would have survived that, because it only asks whether
    # the count is zero, and the number the report prints would have been
    # a lie.
    old_tally="$tmp/old_templates.tsv"
    new_tally="$tmp/new_templates.tsv"
    tally_templates "$cxx" "$old_root" <"$headers" | sort >"$old_tally"
    tally_templates "$cxx" . <"$new_headers" | sort >"$new_tally"

    fold_reqs="$tmp/fold_reqs.tsv"
    fold_requirements "$fold_reqs" "$old_tally" "$new_tally"

    folds="$tmp/folds.tsv"
    load_folds "$folds" "$fold_reqs" "$new_tally"

    fold_misses="$tmp/fold_misses.tsv"
    awk -F'\t' -v folds="$folds" '
        FILENAME == folds { written[$1 SUBSEP $2] = 1; next }
        !(($1 SUBSEP $2) in written) { print $1 "\t" $2 "\t" $3 }
    ' "$folds" "$fold_reqs" | sort -u >"$fold_misses"

    if [[ "$mode" == "emit" ]]; then
        while IFS=$'\t' read -r h s; do
            printf '%s:%s  — <why this symbol has no home in the new tree>.\n' "$h" "$s"
        done <"$misses"
        return 0
    fi

    if [[ "$mode" == "emit_folds" ]]; then
        while IFS=$'\t' read -r h s n; do
            printf '%s:%s  — <what now computes the answer the %s specializations spelled out>.\n' "$h" "$s" "$n"
        done <"$fold_misses"
        return 0
    fi

    local count fold_count
    count=$(wc -l <"$misses")
    fold_count=$(wc -l <"$fold_misses")
    if [[ $count -gt 0 ]]; then
        while IFS=$'\t' read -r h s; do
            local methods rule spaces
            methods=$(awk -F'\t' -v h="$h" -v s="$s" '$1==h && $2==s {print $3}' "$surface" | sort -u | paste -sd+ -)
            rule=$(awk -F'\t' -v h="$h" -v s="$s" '$1==h && $2==s {print $4; exit}' "$verdicts")
            spaces=$(awk -F'\t' -v h="$h" -v s="$s" '$1==h && $2==s && $4!="-" {print $4}' "$surface" | sort -u | paste -sd, -)
            if [[ "$rule" == "qualified" && -n "$spaces" ]]; then
                printf 'MISSING PORT    %s  %s  (%s; %s declares it and the new tree does not)\n' "$h" "$s" "$methods" "$spaces"
            else
                printf 'MISSING PORT    %s  %s  (%s; matched by bare name)\n' "$h" "$s" "$methods"
            fi
        done <"$misses"
        printf '\ncheck-port-completeness: %s symbol(s) from superseded headers have no home in the new tree\n' "$count" >&2
        printf '  and no entry in %s.  Port each one, or write its sentence there.\n' "$drops_file" >&2
        printf '  `%s --emit-drops` prints a template line per miss.\n' "${BASH_SOURCE[0]}" >&2
    fi
    if [[ $fold_count -gt 0 ]]; then
        while IFS=$'\t' read -r h s n; do
            printf 'UNJUSTIFIED FOLD  %s  %s  (the old tree specializes it %s time(s); the new tree, never)\n' "$h" "$s" "$n"
        done <"$fold_misses"
        printf '\ncheck-port-completeness: %s trait(s) kept their name through the port and lost\n' "$fold_count" >&2
        printf '  every specialization, with no entry in %s.  A primary that\n' "$folds_file" >&2
        printf '  answers the same thing for every argument enforces nothing, which is how\n' >&2
        printf '  the Qtt discipline tables made linearity a tautology.  Either restore the\n' >&2
        printf '  discrimination, or write the sentence naming what now computes the answer.\n' >&2
        printf '  `%s --emit-folds` prints a template line per fold.\n' "${BASH_SOURCE[0]}" >&2
    fi
    if [[ $count -gt 0 || $fold_count -gt 0 ]]; then
        return 1
    fi
    if [[ $stale_rc -ne 0 ]]; then
        printf 'check-port-completeness: no missing ports and no unjustified folds, but %s or %s\n' "$drops_file" "$folds_file" >&2
        printf '  has entries that no longer hold.\n' >&2
        return 2
    fi
    printf 'check-port-completeness: clean — every symbol of %s superseded headers is present or has a written drop (%s drops),\n' \
        "$(wc -l <"$headers")" "$(wc -l <"$drops")"
    printf '  and every trait that lost its specializations has a written fold (%s folds).\n' "$(wc -l <"$folds")"
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
    # Qualified-name control.  Three symbols in one old namespace: two
    # port into one new namespace, which is what teaches the guard where
    # that namespace went, and the third does not.  Its bare name occurs
    # in the new tree, in a different namespace, so the bare-name test
    # would call it present.
    cat >"$tmp/old/crucible/_PlantedQualified.h" <<'EOF'
#pragma once
namespace crucible::planted_family {
struct planted_sibling_one {};
struct planted_sibling_two {};
struct planted_collided_name {};
}  // namespace crucible::planted_family
EOF
    # Two headers declaring one name, in different namespaces, neither
    # ported.  One drop row must silence one of them and not the other.
    # Header A also carries a macro, for the (header, macro name) key.
    cat >"$tmp/old/crucible/_PlantedHomeA.h" <<'EOF'
#pragma once
#define PLANTED_MACRO_KEYED 1
namespace crucible::planted_home_a {
struct planted_two_homes {};
}  // namespace crucible::planted_home_a
EOF
    cat >"$tmp/old/crucible/_PlantedHomeB.h" <<'EOF'
#pragma once
namespace crucible::planted_home_b {
struct planted_two_homes {};
}  // namespace crucible::planted_home_b
EOF
    # Fold controls.  Two traits, each specialised once by the old tree
    # and each present by name in the new one.  planted_folded loses its
    # specialisation in the port and must be reported; planted_kept keeps
    # one and must never be, which is the negative control that stops the
    # fold check from degenerating into "every trait needs a sentence".
    cat >"$tmp/old/crucible/_PlantedFold.h" <<'EOF'
#pragma once
namespace crucible {
template <class T>
struct planted_folded {
    static constexpr bool value = false;
};
template <>
struct planted_folded<int> {
    static constexpr bool value = true;
};
template <class T>
struct planted_kept {
    static constexpr bool value = false;
};
template <>
struct planted_kept<int> {
    static constexpr bool value = true;
};
}  // namespace crucible
EOF
    cat >"$tmp/new/foundation/Planted.h" <<'EOF'
#pragma once
namespace planted_new {
struct planted_ported {};
struct planted_ported_base {};
namespace planted_family {
struct planted_sibling_one {};
struct planted_sibling_two {};
}  // namespace planted_family
namespace planted_elsewhere {
struct planted_collided_name {};
}  // namespace planted_elsewhere
template <class T>
struct planted_folded {
    static constexpr bool value = false;
};
template <class T>
struct planted_kept {
    static constexpr bool value = false;
};
template <>
struct planted_kept<int> {
    static constexpr bool value = true;
};
}  // namespace planted_new
EOF

    # The fold row for the planted fold.  The key is the new header as the
    # scan lists it, which under the self-test's roots is an absolute path.
    printf '%s:planted_folded  — planted by the self-test as a legitimate fold.\n' \
        "$tmp/new/foundation/Planted.h" >"$tmp/folds-ok.txt"

    run_guard() {
        PORT_GUARD_OLD_ROOT="$tmp/old" PORT_GUARD_OLD_SUBDIR=crucible \
        PORT_GUARD_NEW_ROOTS="$tmp/new/foundation" PORT_GUARD_NS=crucible \
        PORT_GUARD_NEW_NS=planted_new \
        PORT_GUARD_DROPS="$1" PORT_GUARD_FOLDS="${2:-$tmp/folds-ok.txt}" \
        bash "${BASH_SOURCE[0]}" --quiet
    }

    local fails=0
    reported() { grep -E '^MISSING PORT' "$out" | awk '{print $4}' | LC_ALL=C sort | paste -sd, -; }

    # Arm 1: no drops file — the misses are exactly the planted unported set.
    out="$tmp/arm1.out"; set +e; run_guard "$tmp/no-such-drops.txt" >"$out" 2>&1; rc=$?; set -e
    local want="PLANTED_MACRO_KEYED,PLANTED_MACRO_UNPORTED,planted_alias_unported,planted_collided_name,planted_reexport_unported,planted_template_unported,planted_two_homes,planted_two_homes,planted_unported"
    if [[ $rc -eq 1 && "$(reported)" == "$want" ]]; then
        printf 'check-port-completeness --self-test: minimal control — planted_unported reported and planted_ported not, as expected.\n'
        printf 'check-port-completeness --self-test: blind spots — template, alias-by-own-name, re-export and macro each reported; the detail member was not, as expected.\n'
        printf 'check-port-completeness --self-test: the qualified test reported planted_collided_name although the new tree holds that identifier in another namespace, as expected.\n'
        printf 'check-port-completeness --self-test: one name declared by two headers was reported for both, as expected.\n'
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
crucible/_PlantedQualified.h:planted_collided_name  — planted.
crucible/_PlantedHomeA.h:planted_two_homes  — planted.
crucible/_PlantedHomeB.h:planted_two_homes  — planted.
crucible/_PlantedHomeA.h:PLANTED_MACRO_KEYED  — planted.
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

    # Arm 6: the (header, symbol) key.  Drop the row for one of the two
    # headers that declare planted_two_homes.  The other must still be
    # reported, and it must be the one whose row is gone.
    grep -v '_PlantedHomeB.h:planted_two_homes' "$tmp/drops-ok.txt" >"$tmp/drops-one-home.txt"
    out="$tmp/arm6.out"; set +e; run_guard "$tmp/drops-one-home.txt" >"$out" 2>&1; rc=$?; set -e
    if [[ $rc -eq 1 ]] && [[ "$(reported)" == "planted_two_homes" ]] \
       && grep -q 'MISSING PORT.*_PlantedHomeB.h  planted_two_homes' "$out"; then
        printf 'check-port-completeness --self-test: a row for one header does not silence the same name in another, as expected.\n'
    else
        printf 'check-port-completeness --self-test: FAIL — a drop row keyed on one header silenced another header (exit %s reporting {%s}).\n' "$rc" "$(reported)" >&2
        sed 's/^/    /' "$out" >&2; fails=1
    fi

    # Arm 7: a macro is keyed the same way.  Move its row to a header that
    # does not define it: the row must read stale and the macro must still
    # be reported against the header that does define it.
    sed 's|_PlantedHomeA.h:PLANTED_MACRO_KEYED|_PlantedHomeB.h:PLANTED_MACRO_KEYED|' "$tmp/drops-ok.txt" >"$tmp/drops-macro-moved.txt"
    out="$tmp/arm7.out"; set +e; run_guard "$tmp/drops-macro-moved.txt" >"$out" 2>&1; rc=$?; set -e
    if [[ $rc -eq 1 ]] && grep -q 'STALE DROP.*PLANTED_MACRO_KEYED' "$out" \
       && grep -q 'MISSING PORT.*_PlantedHomeA.h  PLANTED_MACRO_KEYED' "$out"; then
        printf 'check-port-completeness --self-test: a macro row is keyed by header and name, as expected.\n'
    else
        printf 'check-port-completeness --self-test: FAIL — a macro row naming the wrong header was accepted (exit %s).\n' "$rc" >&2
        sed 's/^/    /' "$out" >&2; fails=1
    fi

    folded() { grep -E '^UNJUSTIFIED FOLD' "$out" | awk '{print $4}' | LC_ALL=C sort | paste -sd, -; }

    # Arm 8: the fold check, with its negative control in the same
    # assertion.  planted_folded lost its only specialisation and must be
    # reported; planted_kept still has one and must not be.  A check that
    # reported both would be asking every trait for a sentence, which is
    # noise rather than a gate.
    : >"$tmp/folds-empty.txt"
    out="$tmp/arm8.out"; set +e; run_guard "$tmp/drops-ok.txt" "$tmp/folds-empty.txt" >"$out" 2>&1; rc=$?; set -e
    if [[ $rc -eq 1 && "$(folded)" == "planted_folded" ]]; then
        printf 'check-port-completeness --self-test: a trait that lost every specialization was reported, and one that kept a specialization was not, as expected.\n'
    else
        printf 'check-port-completeness --self-test: FAIL — expected exit 1 reporting exactly {planted_folded}, got exit %s reporting {%s}.\n' "$rc" "$(folded)" >&2
        sed 's/^/    /' "$out" >&2; fails=1
    fi

    # Arm 9: a written fold clears it.
    out="$tmp/arm9.out"; set +e; run_guard "$tmp/drops-ok.txt" >"$out" 2>&1; rc=$?; set -e
    if [[ $rc -eq 0 ]]; then
        printf 'check-port-completeness --self-test: a written fold reported clean, as expected.\n'
    else
        printf 'check-port-completeness --self-test: FAIL — a written fold was not clean (exit %s).\n' "$rc" >&2
        sed 's/^/    /' "$out" >&2; fails=1
    fi

    # Arm 10: a row for a trait the new tree still specializes is
    # OBSOLETE, not STALE.  The two ask for different edits, so the guard
    # has to tell them apart rather than print one word for both.
    { cat "$tmp/folds-ok.txt"; printf '%s:planted_kept  — obsolete on purpose.\n' "$tmp/new/foundation/Planted.h"; } >"$tmp/folds-obsolete.txt"
    out="$tmp/arm10.out"; set +e; run_guard "$tmp/drops-ok.txt" "$tmp/folds-obsolete.txt" >"$out" 2>&1; rc=$?; set -e
    if [[ $rc -eq 2 ]] && grep -q 'OBSOLETE FOLD.*planted_kept' "$out"; then
        printf 'check-port-completeness --self-test: a fold row for a trait the new tree still specializes was reported obsolete, as expected.\n'
    else
        printf 'check-port-completeness --self-test: FAIL — an obsolete fold row was not reported as exit 2 (exit %s).\n' "$rc" >&2
        sed 's/^/    /' "$out" >&2; fails=1
    fi

    # Arm 11: a row for a name that is not a fold at all is STALE.
    { cat "$tmp/folds-ok.txt"; printf '%s:planted_never_folded  — stale on purpose.\n' "$tmp/new/foundation/Planted.h"; } >"$tmp/folds-stale.txt"
    out="$tmp/arm11.out"; set +e; run_guard "$tmp/drops-ok.txt" "$tmp/folds-stale.txt" >"$out" 2>&1; rc=$?; set -e
    if [[ $rc -eq 2 ]] && grep -q 'STALE FOLD.*planted_never_folded' "$out"; then
        printf 'check-port-completeness --self-test: a fold row naming no fold was reported stale, as expected.\n'
    else
        printf 'check-port-completeness --self-test: FAIL — a stale fold row was not reported as exit 2 (exit %s).\n' "$rc" >&2
        sed 's/^/    /' "$out" >&2; fails=1
    fi

    # Arm 12: a fold with no sentence is rejected, and the fold it was
    # meant to justify is then reported, so exit 1 takes precedence the
    # way the exit-code contract says.
    printf '%s:planted_folded  — \n' "$tmp/new/foundation/Planted.h" >"$tmp/folds-prose.txt"
    out="$tmp/arm12.out"; set +e; run_guard "$tmp/drops-ok.txt" "$tmp/folds-prose.txt" >"$out" 2>&1; rc=$?; set -e
    if [[ $rc -ne 0 ]] && grep -q 'MALFORMED FOLD.*planted_folded' "$out"; then
        printf 'check-port-completeness --self-test: sentence-less fold rejected, as expected.\n'
    else
        printf 'check-port-completeness --self-test: FAIL — a sentence-less fold was not rejected (exit %s).\n' "$rc" >&2
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
    --emit-folds)  quiet=1; scan emit_folds ;;
    --self-test)   self_test ;;
    *)             usage ;;
esac
