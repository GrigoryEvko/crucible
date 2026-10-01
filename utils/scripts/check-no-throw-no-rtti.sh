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
#   -fno-exceptions would not compile include/fixy/concurrent/Topology.h
#   at all, whose eight catch sites turn a failed sysfs read into a
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
#   __cxa_throw / __cxa_rethrow referenced  ->  nothing in the artifact
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
# WHERE A THROW STANDS.  The guard reads the relocations of the artifact
# (readelf -rW), and each one names the section that holds it.  The tree
# compiles with -ffunction-sections, so that section names the function
# that throws.  A local function that no code or data section of its own
# object references is dead: no other object can name a local symbol, and
# --gc-sections drops it from each link.  A throw in a dead function does
# not count.  An optimized build can leave such a function behind, for
# example the cold part of an allocator after each call of it was removed.
#
# THE ALLOWLIST.  utils/scripts/no-throw-no-rtti-allowlist.txt admits a
# throw or a polymorphic class that the tree cannot remove, one row each:
#
#   throw  | <artifact file name> | <text of the function name> | <reason>
#   vtable | <artifact file name> | <text of the class name>    | <reason>
#
# The text matches when the demangled name holds it.  A throw row admits
# __cxa_throw and __cxa_rethrow in a matching function, and a vtable row
# admits the typeinfo and the vtable that an artifact defines for a
# matching class.  A downcast is never admitted.  A row whose artifact the
# run scans and that matches nothing is stale and fails, so the list only
# shrinks.
#
# Exit status:
#   0  every scanned artifact holds both properties
#   1  a violation, named with its artifact and symbol, or a stale row
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
THROW_SYMBOLS=(__cxa_throw __cxa_rethrow)
DOWNCAST_SYMBOLS=(__dynamic_cast __cxa_bad_cast)

allowlist_file="$(dirname "${BASH_SOURCE[0]}")/no-throw-no-rtti-allowlist.txt"

# Total undefined symbols read, so the report can say the scan read something.
symbols_read=0

# The rows of the allowlist that a reference matched, by line number.
declare -A matched_rows=()
# The file names of the artifacts that the run scanned.
declare -A scanned_names=()

# Prints each reference of an artifact to a named symbol, one line each:
# live or dead, the symbol, the member of the archive, and the demangled
# function that holds the reference.  See WHERE A THROW STANDS in the
# header.  Exits 2 when readelf cannot read the artifact.
# Complexity: linear in the relocations and the symbols of the artifact.
live_references_program='
import subprocess
import sys

artifact, wanted = sys.argv[1], set(sys.argv[2:])
FUNCTION_PREFIXES = (".text.unlikely.", ".text.hot.", ".text.startup.", ".text.exit.", ".text.")


def listing(*args):
    done = subprocess.run(args, capture_output=True, text=True, check=False)
    if done.returncode != 0 or not done.stdout.strip():
        sys.stderr.write("readelf cannot read " + artifact + ": " + done.stderr.strip() + "\n")
        sys.exit(2)
    return done.stdout.splitlines()


def member_of(line, current):
    if not line.startswith("File: "):
        return current
    named = line[len("File: "):]
    return named[named.rfind("(") + 1:-1] if named.endswith(")") else named


local_functions = {}
member = artifact
for line in listing("readelf", "-sW", artifact):
    member = member_of(line, member)
    fields = line.split()
    if len(fields) >= 8 and fields[3] == "FUNC" and fields[4] == "LOCAL":
        local_functions.setdefault(member, set()).add(fields[7])

references = {}
hits = []
member, section = artifact, ""
for line in listing("readelf", "-rW", artifact):
    if line.startswith("File: "):
        member = member_of(line, member)
        continue
    if line.startswith("Relocation section "):
        section = line.split(chr(39))[1].removeprefix(".rela").removeprefix(".rel")
        continue
    fields = line.split()
    if len(fields) < 5 or not fields[2].startswith("R_") or section.startswith((".debug", ".eh_frame")):
        continue
    references.setdefault(member, set()).add(fields[4])
    if fields[4] in wanted:
        hits.append((fields[4], member, section))

found = []
for symbol, member, section in hits:
    function = next((section[len(prefix):] for prefix in FUNCTION_PREFIXES if section.startswith(prefix)), "")
    named = references.get(member, set())
    is_dead = (function in local_functions.get(member, set()) and function not in named and section not in named)
    found.append(("dead" if is_dead else "live", symbol, member, function or "(section " + section + ")"))
names = subprocess.run(["c++filt"], input="\n".join(entry[3] for entry in found), capture_output=True, text=True,
                       check=False).stdout.splitlines()
if len(names) != len(found):
    sys.stderr.write("c++filt did not demangle each function name of " + artifact + "\n")
    sys.exit(2)
for (state, symbol, member, _function), name in zip(found, names):
    print(state + "\t" + symbol + "\t" + member + "\t" + name)
'

# $1 = text.  Prints the text without its leading and trailing space.
trim() {
    local text="$1"
    text="${text#"${text%%[![:space:]]*}"}"
    printf '%s' "${text%"${text##*[![:space:]]}"}"
}

# $1 = artifact file name, $2 = kind (throw or vtable), $3 = demangled name.
# Prints the line number of the first allowlist row that admits the name,
# or nothing.
admitting_row() {
    local name="$1" kind="$2" subject="$3" number=0 line row_kind row_artifact row_text
    [[ -f "$allowlist_file" ]] || return 0
    while IFS= read -r line || [[ -n "$line" ]]; do
        number=$((number + 1))
        [[ -z "${line// }" || "$line" == \#* ]] && continue
        IFS='|' read -r row_kind row_artifact row_text _ <<<"$line"
        row_kind="$(trim "$row_kind")"
        row_artifact="$(trim "$row_artifact")"
        row_text="$(trim "$row_text")"
        if [[ "$row_kind" == "$kind" && "$row_artifact" == "$name" && -n "$row_text" \
              && "$subject" == *"$row_text"* ]]; then
            printf '%s\n' "$number"
            return 0
        fi
    done <"$allowlist_file"
}

# Fails when a row of the allowlist is malformed.  Returns 1 if any is.
check_allowlist_rows() {
    local number=0 line fields rc=0
    [[ -f "$allowlist_file" ]] || return 0
    while IFS= read -r line || [[ -n "$line" ]]; do
        number=$((number + 1))
        [[ -z "${line// }" || "$line" == \#* ]] && continue
        fields="${line//[^|]/}"
        if [[ "${#fields}" -ne 3 || ! "$line" =~ ^[[:space:]]*(throw|vtable)[[:space:]]*\| ]]; then
            printf 'MALFORMED: %s:%d is not `throw | artifact | text | reason` or `vtable | artifact | text | reason`.\n' \
                "$allowlist_file" "$number" >&2
            rc=1
        fi
    done <"$allowlist_file"
    return "$rc"
}

# Fails for each row whose artifact the run scanned and that no reference
# matched.  Returns 1 if any row is stale.
check_stale_rows() {
    local number=0 line row_kind row_artifact rc=0
    [[ -f "$allowlist_file" ]] || return 0
    while IFS= read -r line || [[ -n "$line" ]]; do
        number=$((number + 1))
        [[ -z "${line// }" || "$line" == \#* ]] && continue
        IFS='|' read -r row_kind row_artifact _ <<<"$line"
        row_artifact="$(trim "$row_artifact")"
        if [[ -n "${scanned_names[$row_artifact]:-}" && -z "${matched_rows[$number]:-}" ]]; then
            printf 'STALE: %s:%d admits a reference in %s, and the artifact holds none.  Remove the row.\n' \
                "$allowlist_file" "$number" "$row_artifact" >&2
            rc=1
        fi
    done <"$allowlist_file"
    return "$rc"
}

# $1 = artifact.  Prints violations to stderr, returns 1 if any.
scan_artifact() {
    local artifact="$1" rc=0 symbol name row
    if [[ ! -f "$artifact" ]]; then
        printf 'check-no-throw-no-rtti: %s does not exist\n' "$artifact" >&2
        return 2
    fi
    name="$(basename "$artifact")"
    scanned_names["$name"]=1

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

    # A throw, at each function that holds one.  Each undefined throw symbol
    # must have a relocation that the attribution reads, or the attribution
    # read nothing and the guard cannot decide.
    local state function member references attributed=""
    if ! references="$(python3 -c "$live_references_program" "$artifact" "${THROW_SYMBOLS[@]}")"; then
        printf 'check-no-throw-no-rtti: the relocations of %s cannot be read, so nothing was checked.\n' \
            "$artifact" >&2
        return 2
    fi
    while IFS=$'\t' read -r state symbol member function; do
        [[ -z "$state" ]] && continue
        attributed+=" $symbol "
        [[ "$state" == "dead" ]] && continue
        row="$(admitting_row "$name" throw "$function")"
        if [[ -n "$row" ]]; then
            matched_rows["$row"]=1
            continue
        fi
        {
            printf 'VIOLATION: %s references %s in %s, in %s.\n' "$artifact" "$symbol" "$member" "$function"
            printf '  Something in this artifact throws.  An error path in this tree is std::expected or an abort, '
            printf 'never a throw.  A throw that a library header makes and the tree cannot remove takes a row, '
            printf 'with its reason, in %s.\n' "$allowlist_file"
        } >&2
        rc=1
    done <<<"$references"
    for symbol in "${THROW_SYMBOLS[@]}"; do
        if grep -qE "[[:space:]]U[[:space:]]+_?${symbol}\$" <<<"$undefined" && [[ "$attributed" != *" $symbol "* ]]; then
            printf 'VIOLATION: %s references %s, and no relocation names the function that holds it.\n' \
                "$artifact" "$symbol" >&2
            rc=1
        fi
    done

    for symbol in "${DOWNCAST_SYMBOLS[@]}"; do
        if grep -qE "[[:space:]]U[[:space:]]+_?${symbol}\$" <<<"$undefined"; then
            {
                printf 'VIOLATION: %s references %s.\n' "$artifact" "$symbol"
                printf '  A runtime downcast reached the artifact.  This tree has no virtual '
                printf 'and no dynamic_cast: dispatch is a kind enum plus static_cast.\n'
            } >&2
            rc=1
        fi
    done

    # A defined typeinfo or vtable: a polymorphic type was compiled in.  A
    # reference to the typeinfo or the vtable of a library class is not a
    # definition, and the object that defines it holds the violation.
    local defined line subject unadmitted=""
    defined="$(nm --defined-only --demangle "$artifact" 2>/dev/null | grep -E ' (typeinfo|vtable) for ' || true)"
    while IFS= read -r line; do
        [[ -z "$line" ]] && continue
        subject="${line#* for }"
        row="$(admitting_row "$name" vtable "$subject")"
        if [[ -n "$row" ]]; then
            matched_rows["$row"]=1
        else
            unadmitted+="$line"$'\n'
        fi
    done <<<"$defined"
    if [[ -n "$unadmitted" ]]; then
        {
            printf 'VIOLATION: %s defines typeinfo or a vtable:\n%s' "$artifact" "$unadmitted"
            printf '  A polymorphic type was compiled in.  The code guide says dispatch is a kind '
            printf 'enum plus static_cast, and every virtual in the tree lives in a planning '
            printf 'document rather than in code.  A class of a library that the tree uses by design takes a row, '
            printf 'with its reason, in %s.\n' "$allowlist_file"
        } >&2
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
        # A throw that a row admits, and a throw in a local function that
        # nothing calls, which no link keeps.
        printf '%s\n' 'int admitted_throwing_entry(int input) {' \
                      '    if (input < 0) { throw input; }' \
                      '    return input;' \
                      '}' \
                      '[[gnu::used]] static int dead_throwing_entry(int input) {' \
                      '    if (input < 0) { throw input; }' \
                      '    return input;' \
                      '}' \
                      'int also_clean(int input) noexcept { return input; }' >"$tmp_root/admitted.cpp"
        # A polymorphic class that a row admits.
        printf '%s\n' 'struct Admitted { virtual ~Admitted() = default; virtual int tag() const { return 2; } };' \
                      'int admitted_class_entry() noexcept { Admitted admitted; Admitted* held = &admitted;' \
                      '    return held->tag(); }' \
                      'Admitted* admitted_make() { static Admitted one; return &one; }' >"$tmp_root/polymorphic.cpp"
        # A class whose vtable another object defines: a reference, not a definition.
        printf '%s\n' '#include <new>' \
                      'struct Elsewhere { Elsewhere() = default; virtual int tag() const; };' \
                      'Elsewhere* placed_entry(void* memory) noexcept { return new (memory) Elsewhere; }' \
                      >"$tmp_root/references.cpp"
        for unit in clean throws catches rtti admitted polymorphic references; do
            "$cxx" -std=c++17 -O1 -ffunction-sections -c "$tmp_root/$unit.cpp" -o "$tmp_root/$unit.o" 2>/dev/null \
                || fail "could not compile the $unit fixture"
            ar rcs "$tmp_root/lib$unit.a" "$tmp_root/$unit.o" 2>/dev/null \
                || fail "could not archive the $unit fixture"
        done
        allowlist_file="$tmp_root/allowlist.txt"
        printf '%s\n' '# planted' \
                      'throw | libadmitted.a | admitted_throwing_entry | a planted throw' \
                      'vtable | libpolymorphic.a | Admitted | a planted class' >"$allowlist_file"

        rc=0; scan_artifact "$tmp_root/libclean.a" 2>/dev/null || rc=$?
        [[ "$rc" -eq 0 ]] || fail "a clean artifact reported $rc"

        rc=0; out="$(scan_artifact "$tmp_root/libthrows.a" 2>&1)" || rc=$?
        [[ "$rc" -eq 1 ]] || fail "a throwing artifact reported $rc, want 1"
        grep -q '__cxa_throw in throws.o, in throwing_entry(int)\.' <<<"$out" \
            || fail "the throwing artifact was not reported against __cxa_throw in its function ($out)"

        # The negative control that matters: a catch with no throw is the
        # documented boundary and must stay silent.
        rc=0; out="$(scan_artifact "$tmp_root/libcatches.a" 2>&1)" || rc=$?
        [[ "$rc" -eq 0 ]] || fail "a catch without a throw was reported ($out)"

        rc=0; out="$(scan_artifact "$tmp_root/librtti.a" 2>&1)" || rc=$?
        [[ "$rc" -eq 1 ]] || fail "a downcasting artifact reported $rc, want 1"
        grep -qE '__dynamic_cast|typeinfo|vtable' <<<"$out" \
            || fail "the downcasting artifact was not reported against RTTI"

        rc=0; out="$(scan_artifact "$tmp_root/libadmitted.a" 2>&1)" || rc=$?
        [[ "$rc" -eq 0 ]] || fail "a throw that a row admits, or a throw in a dead local function, was reported ($out)"
        rc=0; out="$(scan_artifact "$tmp_root/libpolymorphic.a" 2>&1)" || rc=$?
        [[ "$rc" -eq 0 ]] || fail "a polymorphic class that a row admits was reported ($out)"
        rc=0; out="$(scan_artifact "$tmp_root/libreferences.a" 2>&1)" || rc=$?
        [[ "$rc" -eq 0 ]] || fail "a reference to a vtable that another object defines was reported ($out)"
        rc=0; check_stale_rows 2>/dev/null || rc=$?
        [[ "$rc" -eq 0 ]] || fail "a row that matched a reference was reported stale"

        # The same artifacts against a row that matches nothing.
        matched_rows=()
        printf '%s\n' 'throw | libadmitted.a | admitted_throwing_entry | a planted throw' \
                      'vtable | libpolymorphic.a | Admitted | a planted class' \
                      'throw | libclean.a | no_such_function | a row that matches nothing' >"$allowlist_file"
        for unit in admitted polymorphic clean; do
            scan_artifact "$tmp_root/lib$unit.a" 2>/dev/null || fail "the $unit fixture failed against the rows"
        done
        rc=0; out="$(check_stale_rows 2>&1)" || rc=$?
        [[ "$rc" -eq 1 ]] && grep -q 'allowlist.txt:3 admits a reference in libclean.a' <<<"$out" \
            || fail "a row that matches nothing was not reported stale ($out)"
        rc=0; scan_artifact "$tmp_root/libthrows.a" 2>/dev/null || rc=$?
        [[ "$rc" -eq 1 ]] || fail "a row for one artifact admitted a throw in another"

        printf '%s\n' 'throw | libadmitted.a | admitted_throwing_entry' >"$allowlist_file"
        rc=0; out="$(check_allowlist_rows 2>&1)" || rc=$?
        [[ "$rc" -eq 1 ]] && grep -q 'MALFORMED' <<<"$out" || fail "a row with no reason was not reported malformed"

        printf 'check-no-throw-no-rtti: self-test passed — a throw and a downcast are caught, a '
        printf 'catch without a throw, a clean artifact, an admitted throw or class, a dead local function and a '
        printf 'reference to a library vtable are not, and a stale or malformed row fails\n'
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

rc=0
check_allowlist_rows || rc=1
scanned=0
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
check_stale_rows || rc=1
if [[ "$rc" -ne 0 ]]; then
    exit 1
fi
printf 'check-no-throw-no-rtti: %d artifact(s) hold both properties (%d symbols read, %d allowlist row(s) used) — nothing else throws and nothing else dispatches at runtime\n' \
    "$scanned" "$symbols_read" "${#matched_rows[@]}"
