#!/usr/bin/env bash
# The two properties that -fno-exceptions and -fno-rtti stood for, checked
# on the artifact instead of asserted by a flag.
#
# WHY NOT THE FLAGS.  Measured 2026-09-20: neither flag is in
# CMakeLists.txt and neither is on any compile line, in any preset, and the
# code guide asserted both as common flags for as long as the list has
# existed.  Both properties nevertheless hold.  Adding the flags is the
# wrong repair:
#
#   -fno-exceptions would not compile include/crucible/concurrent/Topology.h
#   at all, whose sixteen catch sites turn a failed sysfs read into a
#   conservative topology instead of a crash.  Where it did compile it would
#   replace a handled library failure with a bare std::terminate.
#
#   -fno-rtti is a no-op on this tree, which is exactly why nobody noticed
#   it missing: there is no dynamic_cast, no typeid, no std::type_info and
#   no virtual outside a planning document.
#
# So the flags are not the guarantee.  The guarantee is the artifact, and
# this is what checks it:
#
#   __cxa_throw / __cxa_rethrow undefined  ->  nothing in the artifact
#                                              throws.  Error paths are
#                                              std::expected or abort.
#   __dynamic_cast / __cxa_bad_cast        ->  no runtime downcast.
#   typeinfo / vtable definitions          ->  no polymorphic type.
#
# This is a stronger claim than either flag made, because it catches a
# throw or a downcast introduced anywhere, including inside a library
# header this tree instantiates, which a flag on our own TUs would not see.
#
# A CATCH IS NOT A VIOLATION.  Catching a library throw and converting it
# to an abort or a fallback is the documented boundary, and it leaves
# __cxa_begin_catch and __cxa_end_catch behind.  Those are deliberately not
# in the scanned set.  Only a throw is a violation.
#
# Exit status:
#   0  every scanned artifact holds both properties
#   1  a violation, named with its artifact and symbol
#   2  bad invocation, or the self-test failed
#   3  cannot decide: no artifact to scan
set -euo pipefail

usage() {
    cat <<'USAGE'
  check-no-throw-no-rtti.sh <artifact> [<artifact> ...]  # scan named artifacts
  check-no-throw-no-rtti.sh --self-test                  # plant a throw and a
                                                         # downcast, verify catch
  check-no-throw-no-rtti.sh -h | --help
USAGE
}

# A throw, and the two runtime entries a downcast needs.  __cxa_begin_catch
# and __cxa_end_catch are absent on purpose: see the header.
FORBIDDEN_UNDEFINED=(__cxa_throw __cxa_rethrow __dynamic_cast __cxa_bad_cast)

# Total undefined symbols read, so the report can say the scan read something.
symbols_read=0

# $1 = artifact.  Prints violations to stderr, returns 1 if any.
scan_artifact() {
    local artifact="$1" rc=0 symbol
    if [[ ! -f "$artifact" ]]; then
        printf 'check-no-throw-no-rtti: %s does not exist\n' "$artifact" >&2
        return 2
    fi

    # Undefined references: what the artifact calls but does not define.
    # An empty read is not a pass.  nm failing, or an archive with no
    # symbols, would otherwise satisfy every check below by producing
    # nothing to match — the shape of a guard that cannot fail.
    local undefined
    undefined="$(nm --undefined-only --no-demangle "$artifact" 2>/dev/null || true)"
    local count
    count="$(grep -c '' <<<"$undefined" || true)"
    if [[ -z "$undefined" ]]; then
        printf 'check-no-throw-no-rtti: nm read no undefined symbols from %s, so nothing was '\
'checked. An artifact that calls nothing is not an artifact that throws nothing.\n' "$artifact" >&2
        return 2
    fi
    symbols_read=$((symbols_read + count))
    for symbol in "${FORBIDDEN_UNDEFINED[@]}"; do
        if grep -qE "[[:space:]]U[[:space:]]+_?${symbol}\$" <<<"$undefined"; then
            printf 'VIOLATION: %s references %s.\n' "$artifact" "$symbol" >&2
            case "$symbol" in
                __cxa_throw|__cxa_rethrow)
                    printf '  Something in this artifact throws.  An error path in this tree is '
                    printf 'std::expected or crucible_abort, never a throw.  Find it with: '
                    printf 'nm -uC %s | grep %s, then the object that needs it with: '
                    printf 'for o in $(ar t %s); do ar p %s $o > /tmp/$o && nm -u /tmp/$o | '
                    printf 'grep -q %s && echo $o; done\n' "$artifact" "$symbol" "$artifact" "$artifact" "$symbol" >&2
                    ;;
                *)
                    printf '  A runtime downcast reached the artifact.  This tree has no virtual '
                    printf 'and no dynamic_cast: dispatch is a kind enum plus static_cast.\n' >&2
                    ;;
            esac
            rc=1
        fi
    done

    # Defined typeinfo or vtable: a polymorphic type was compiled in.
    local defined
    defined="$(nm --demangle "$artifact" 2>/dev/null | grep -E '(typeinfo|vtable) for ' || true)"
    if [[ -n "$defined" ]]; then
        printf 'VIOLATION: %s defines typeinfo or a vtable:\n%s\n' "$artifact" "$defined" >&2
        printf '  A polymorphic type was compiled in.  The code guide says dispatch is a kind '
        printf 'enum plus static_cast, and every virtual in the tree lives in a planning '
        printf 'document rather than in code.\n' >&2
        rc=1
    fi

    return "$rc"
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test)
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        cxx="${CRUCIBLE_CXX:-${CXX:-g++}}"
        if ! command -v "$cxx" >/dev/null 2>&1; then
            printf 'check-no-throw-no-rtti: SELF-TEST cannot run: no compiler at %s\n' "$cxx" >&2
            exit 3
        fi

        fail() {
            printf 'check-no-throw-no-rtti: SELF-TEST FAILED — %s\n' "$1" >&2
            exit 2
        }

        # Clean: an error path that returns rather than throws.  Must pass.
        cat >"$tmp_root/clean.cpp" <<'EOF'
struct Plain { int value = 0; };
int clean_entry(int input) noexcept {
    Plain plain{input};
    return plain.value;
}
EOF
        # A throw.  Must be caught by the scanner.
        cat >"$tmp_root/throws.cpp" <<'EOF'
int throwing_entry(int input) {
    if (input < 0) { throw input; }
    return input;
}
EOF
        # A catch WITHOUT a throw.  Must NOT be reported: this is the
        # Topology.h shape, and a scanner that flagged it would forbid the
        # documented boundary.
        cat >"$tmp_root/catches.cpp" <<'EOF'
extern int may_fail(int);
int catching_entry(int input) noexcept {
    try {
        return may_fail(input);
    } catch (...) {
        return -1;
    }
}
EOF
        # A polymorphic type plus a downcast.  Must be caught.
        cat >"$tmp_root/rtti.cpp" <<'EOF'
struct Base { virtual ~Base() = default; virtual int tag() const { return 0; } };
struct Derived final : Base { int tag() const override { return 1; } };
int downcast_entry(Base* base) noexcept {
    auto* derived = dynamic_cast<Derived*>(base);
    return derived ? derived->tag() : -1;
}
EOF
        for unit in clean throws catches rtti; do
            "$cxx" -std=c++17 -O1 -c "$tmp_root/$unit.cpp" -o "$tmp_root/$unit.o" 2>/dev/null \
                || fail "could not compile the $unit fixture"
            ar rcs "$tmp_root/lib$unit.a" "$tmp_root/$unit.o" 2>/dev/null \
                || fail "could not archive the $unit fixture"
        done

        rc=0; scan_artifact "$tmp_root/libclean.a" 2>/dev/null || rc=$?
        [[ "$rc" -eq 0 ]] || fail "a clean artifact reported $rc"

        rc=0; out="$(scan_artifact "$tmp_root/libthrows.a" 2>&1)" || rc=$?
        [[ "$rc" -eq 1 ]] || fail "a throwing artifact reported $rc, want 1"
        grep -q '__cxa_throw' <<<"$out" || fail "the throwing artifact was not reported against __cxa_throw"

        # The negative control that matters: a catch with no throw is the
        # documented boundary and must stay silent.
        rc=0; out="$(scan_artifact "$tmp_root/libcatches.a" 2>&1)" || rc=$?
        [[ "$rc" -eq 0 ]] || fail "a catch without a throw was reported ($out)"

        rc=0; out="$(scan_artifact "$tmp_root/librtti.a" 2>&1)" || rc=$?
        [[ "$rc" -eq 1 ]] || fail "a downcasting artifact reported $rc, want 1"
        grep -qE '__dynamic_cast|typeinfo|vtable' <<<"$out" \
            || fail "the downcasting artifact was not reported against RTTI"

        printf 'check-no-throw-no-rtti: self-test passed — a throw and a downcast are caught, a '
        printf 'catch without a throw and a clean artifact are not\n'
        exit 0
        ;;
    "")
        usage >&2
        exit 2
        ;;
esac

# The input set, printed before the verdict.
printf 'check-no-throw-no-rtti: %d artifact(s) to scan\n' "$#"
printf '  %s\n' "$@"

scanned=0
rc=0
for artifact in "$@"; do
    if [[ ! -f "$artifact" ]]; then
        printf 'check-no-throw-no-rtti: %s is not built, skipping\n' "$artifact" >&2
        continue
    fi
    scanned=$((scanned + 1))
    scan_artifact "$artifact" || rc=1
done

if [[ "$scanned" -eq 0 ]]; then
    printf 'check-no-throw-no-rtti: none of the named artifacts is built, so nothing was checked.\n' >&2
    exit 3
fi
if [[ "$rc" -ne 0 ]]; then
    exit 1
fi
printf 'check-no-throw-no-rtti: %d artifact(s) hold both properties (%d symbols read) — nothing throws and nothing dispatches at runtime\n' \
    "$scanned" "$symbols_read"
