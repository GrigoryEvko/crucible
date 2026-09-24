#!/usr/bin/env bash
# check-frozen-tree.sh — the old substrate is frozen until it is deleted.
#
# The canonical substrate is being extracted into include/foundation/ and
# include/fixy/ as a sibling tree.  While the two coexist, the old one under
# include/crucible/{safety,fixy,algebra,effects,permissions,sessions,bridges,
# handles,concurrent} is frozen: a bug found in old code is fixed in the new
# tree or not at all, and nothing is added to the old tree.  Deletions pass —
# the old tree ends by deletion, and its consumers move off it one by one.
#
# The freeze base is the commit recorded below.  The scan diffs that base
# against the working tree (committed and uncommitted changes alike), and any
# Added or Modified path under a frozen prefix fails.  Renames count as an add
# of the destination when it is under a frozen prefix.
#
# One change is not a change: the superseded marking.  When a port moves an
# old header to the new tree, the old file is renamed in place with a leading
# underscore (Graded.h becomes _Graded.h) so that its status is visible in
# every include line and directory listing until it is deleted.  A
# rename of dir/Name to dir/_Name whose content is unchanged passes, and so
# does an edit to a frozen file whose only difference is that its includes of
# frozen headers gained the same underscore.  The comparison normalizes both
# sides by stripping that underscore, so any other edit still fails.
#
# A second exception is a soundness mirror.  A frozen file can hold a
# live bug that the new tree has ALREADY fixed.  The old code keeps
# hurting until the old tree is deleted, and a mirror of that fix carries no
# divergence risk, because the same fix already exists in the new tree.
# scripts/frozen-soundness-mirrors.txt admits one such edit per line, in
# the shape `path — new-tree fix reference — reason`.  A
# MODIFY under a frozen path whose path is listed is admitted, with a
# note that names the ledger and the reason.  A plain frozen file edits
# as a modify.  An already-ported file (renamed dir/Name to dir/_Name)
# edits as a rename or an add, because dir/_Name is absent from the
# freeze base, so those are admitted too when the base still holds the
# dir/Name twin.  A genuine new frozen file has no base twin, so its add
# stays rejected, and so does an untracked file: a mirror edits a file
# that is already here.
#
# The ledger is fail-closed against its own rot, the way
# check-allowlist-keys.sh is.  An entry that names a path outside every
# frozen directory, or a path that does not exist, fails the guard and
# names the entry.  A ledger that cannot admit the wrong thing is what
# lets this exception weaken the freeze without dissolving it.
#
# A third exception is a macro unification.  The two trees define some
# CRUCIBLE_ macros under the same name, and a translation unit that
# includes both definitions fails to compile, because GCC reports a
# changed macro as a plain redefinition.  A frozen file can give up its
# own definition and include the foundation header that defines the macro.
# The scan admits that change by machine and needs no ledger.  Against
# the base twin, with the superseded marking of includes ignored, the
# change may do two things only:
#   - remove the whole of a macro definition, continuation lines
#     included, whose name a header under include/foundation/ defines;
#   - add an include of a header that exists under include/foundation/.
# Any other removed, changed or added line refuses the change, and the
# refusal names the line.  The exception does not decide that the two
# definitions mean the same thing.  The author proves that before the
# change, and review holds the proof.
#
# Exit status:
#   0 — clean
#   1 — an add or modify under a frozen path, or a rotted ledger entry
#   2 — bad invocation / not a git tree / self-test failure
#   3 — the freeze base is not in this clone (shallow checkout); ctest skips

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# The freeze base: the last commit before the sibling tree existed.
FREEZE_BASE=1e0cd65e975f53d8838cb4d22e0aad9233374e35

# Frozen prefixes, relative to the repository root.  Directories end in '/',
# single files do not.
FROZEN_PATHS=(
    include/crucible/safety/
    include/crucible/fixy/
    include/crucible/algebra/
    include/crucible/effects/
    include/crucible/permissions/
    include/crucible/sessions/
    include/crucible/bridges/
    include/crucible/handles/
    include/crucible/concurrent/
    include/crucible/Fixy.h
    src/fixy/_Fs.cpp
    examples/fn/
)

# The soundness-mirror ledger, relative to the repository root.  One
# admitted edit per line, in the shape `path — new-tree fix reference —
# reason`.  A '#' line is a comment, and a blank line is skipped.
FROZEN_MIRROR_LEDGER="scripts/frozen-soundness-mirrors.txt"

usage() {
    cat >&2 <<'USAGE'
check-frozen-tree.sh — the old substrate is frozen until it is deleted.

Usage:
  check-frozen-tree.sh              # scan; exit 1 on an add/modify under a frozen path
  check-frozen-tree.sh --self-test  # build a throwaway repo, plant edits, verify catch
  check-frozen-tree.sh -h | --help  # usage
USAGE
}

# Strips the superseded marking from include lines so that a file and its
# marked twin compare equal.  Only includes of the old tree are touched.
#
# The directory part is optional.  It was `crucible/.*/`, which needs an
# intermediate directory, so an include of a header at the root of
# include/crucible/ was never normalized and a frozen file that followed
# such a marking read as an ordinary edit.  Marking include/crucible/
# Saturate.h reported three violations for one permitted include rewrite
# each.  A header sits at the root of that tree as legitimately as in a
# subdirectory, and the rule names neither.
normalize_stream() {
    local line
    while IFS= read -r line || [[ -n "$line" ]]; do
        if [[ "$line" =~ ^([[:space:]]*#[[:space:]]*include[[:space:]]*\<crucible/([^>]*/)?)_([^/>]+\>.*)$ ]]; then
            printf '%s\n' "${BASH_REMATCH[1]}${BASH_REMATCH[3]}"
        else
            printf '%s\n' "$line"
        fi
    done
}

# $1 = repo, $2 = base, $3 = path at base, $4 = path in the working tree.
# True when $4 is $3 or its underscore-marked twin, and the two contents
# differ only by the marking of frozen includes.
is_superseded_marking() {
    local repo="$1" base="$2" old="$3" new="$4"
    local old_dir old_name new_dir new_name
    old_dir="$(dirname "$old")"; old_name="$(basename "$old")"
    new_dir="$(dirname "$new")"; new_name="$(basename "$new")"
    [[ "$new_dir" == "$old_dir" ]] || return 1
    [[ "$new_name" == "$old_name" || "$new_name" == "_$old_name" ]] || return 1
    git -C "$repo" cat-file -e "${base}:${old}" 2>/dev/null || return 1
    [[ -f "$repo/$new" ]] || return 1
    diff -q <(git -C "$repo" show "${base}:${old}" | normalize_stream) \
            <(normalize_stream <"$repo/$new") >/dev/null 2>&1
}

# $1 = repo, $2 = base, $3 = path in the working tree.  True when the path
# is a superseded marking of a base file: either the same path with only
# marked includes changed, or dir/_Name whose twin dir/Name is at the base.
# Git reports a rename-plus-edit against the working tree as an add, so
# the twin is derived from the name rather than read from the status.
is_marking_of_base() {
    local repo="$1" base="$2" path="$3" name twin
    name="$(basename "$path")"
    if is_superseded_marking "$repo" "$base" "$path" "$path"; then
        return 0
    fi
    [[ "$name" == _* ]] || return 1
    twin="$(dirname "$path")/${name#_}"
    is_superseded_marking "$repo" "$base" "$twin" "$path"
}

is_frozen() {
    local path="$1" p
    for p in "${FROZEN_PATHS[@]}"; do
        case "$p" in
            */) [[ "$path" == "$p"* ]] && return 0 ;;
            *)  [[ "$path" == "$p" ]] && return 0 ;;
        esac
    done
    return 1
}

# Removes the whitespace at the two ends of $1.
trim_whitespace() {
    local s="$1"
    s="${s#"${s%%[![:space:]]*}"}"
    s="${s%"${s##*[![:space:]]}"}"
    printf '%s' "$s"
}

# Prints one "path<TAB>reason" line for each entry in the ledger of $1
# (the repository root).  The path is the field before the first
# em-dash, the reason the field after the last one.  A comment line and
# a blank line produce nothing.
mirror_ledger_entries() {
    local repo="$1" ledger="$1/$FROZEN_MIRROR_LEDGER" line path reason
    [[ -f "$ledger" ]] || return 0
    while IFS= read -r line || [[ -n "$line" ]]; do
        case "$line" in
            ''|'#'*) continue ;;
        esac
        [[ -z "${line//[[:space:]]/}" ]] && continue
        path="$(trim_whitespace "${line%%—*}")"
        reason="$(trim_whitespace "${line##*—}")"
        [[ -n "$path" ]] || continue
        printf '%s\t%s\n' "$path" "$reason"
    done <"$ledger"
}

# True when $2 (a repo-relative path) is admitted by the ledger of $1.
is_soundness_mirror() {
    local repo="$1" want="$2" path reason
    while IFS=$'\t' read -r path reason; do
        [[ "$path" == "$want" ]] && return 0
    done < <(mirror_ledger_entries "$repo")
    return 1
}

# Prints the reason the ledger of $1 gives for admitting $2.
mirror_reason() {
    local repo="$1" want="$2" path reason
    while IFS=$'\t' read -r path reason; do
        if [[ "$path" == "$want" ]]; then
            printf '%s' "$reason"
            return 0
        fi
    done < <(mirror_ledger_entries "$repo")
    return 1
}

# Fails when the ledger of $1 names a path that is not frozen, or a path
# that does not exist.  Either one admits no real edit and hides a typo,
# so the scan refuses to trust the ledger until both hold.  Prints every
# bad entry.  Returns 1 when any entry is bad.
validate_ledger() {
    local repo="$1" rc=0 path reason
    while IFS=$'\t' read -r path reason; do
        if ! is_frozen "$path"; then
            printf 'SOUNDNESS-MIRROR-LEDGER: %s — not under a frozen directory, so a mirror admission means nothing.  Name the frozen file, or remove the entry.\n' "$path" >&2
            rc=1
        elif [[ ! -f "$repo/$path" ]]; then
            printf 'SOUNDNESS-MIRROR-LEDGER: %s — does not exist, so the entry admits no edit and hides a typo.  Re-key it onto the surviving path, or prune it.\n' "$path" >&2
            rc=1
        fi
    done < <(mirror_ledger_entries "$repo")
    return "$rc"
}

# True when $3 is the superseded-marking twin of a file that exists at
# base $2.  A ported file is renamed dir/Name -> dir/_Name, so a later
# content edit to dir/_Name is reported by git as a rename or an add,
# not a modify, because dir/_Name is absent from the base.  This tells a
# soundness-mirror edit of an already-ported file apart from a genuine
# new frozen file, which has no base twin.
is_edit_of_ported_frozen() {
    local repo="$1" base="$2" path="$3" name twin
    name="$(basename "$path")"
    [[ "$name" == _* ]] || return 1
    twin="$(dirname "$path")/${name#_}"
    git -C "$repo" cat-file -e "${base}:${twin}" 2>/dev/null
}

# True when the change to frozen $3 (status $4) is an admitted soundness
# mirror: the path is listed, and the change is either a plain modify of
# a frozen file present at the base, or a content edit to an already-
# ported file (its _Name twin).  A genuine new frozen file has no base
# twin and its add is not a modify, so it is not admitted here.
mirror_admits() {
    local repo="$1" base="$2" path="$3" status="$4"
    is_soundness_mirror "$repo" "$path" || return 1
    [[ "$status" == M* ]] && return 0
    is_edit_of_ported_frozen "$repo" "$base" "$path"
}

# $1 = repo, $2 = base, $3 = path in the working tree.  Prints the base
# twin of the path: the path itself when the base holds it, or dir/Name
# when the path is dir/_Name and the base holds dir/Name.  Returns 1 when
# the base holds neither.
base_twin_of() {
    local repo="$1" base="$2" path="$3" name twin
    if git -C "$repo" cat-file -e "${base}:${path}" 2>/dev/null; then
        printf '%s' "$path"
        return 0
    fi
    name="$(basename "$path")"
    [[ "$name" == _* ]] || return 1
    twin="$(dirname "$path")/${name#_}"
    git -C "$repo" cat-file -e "${base}:${twin}" 2>/dev/null || return 1
    printf '%s' "$twin"
}

# $1 = repo, $2 = base, $3 = path in the working tree.  True when the
# change to the frozen path is a macro unification, the third exception
# the header describes.  On success it prints one line that names the
# removed macros and the added includes.  On a refusal it prints one line
# for each offending line of the change.  O(size of the file times the
# size of the change) for the line diff, plus one read of every header
# under include/foundation/.
unification_admits() {
    local repo="$1" base="$2" path="$3" twin
    twin="$(base_twin_of "$repo" "$base" "$path")" || return 1
    [[ -f "$repo/$path" ]] || return 1
    python3 - "$repo" <(git -C "$repo" show "${base}:${twin}" | normalize_stream) \
        <(normalize_stream <"$repo/$path") <<'PY'
import difflib
import re
import sys
from pathlib import Path

repo = Path(sys.argv[1])
old = Path(sys.argv[2]).read_text(errors="replace").splitlines()
new = Path(sys.argv[3]).read_text(errors="replace").splitlines()
DEFINE = re.compile(r"^\s*#\s*define\s+([A-Za-z_]\w*)")
FOUNDATION_INCLUDE = re.compile(r"^\s*#\s*include\s*<(foundation/[^>]+)>\s*$")

# Each line of a definition, continuation lines included, maps to the
# macro name and to the first and last line of that definition.
owner = {}
i = 0
while i < len(old):
    match = DEFINE.match(old[i])
    if match is None:
        i += 1
        continue
    last = i
    while old[last].rstrip().endswith("\\") and last + 1 < len(old):
        last += 1
    for k in range(i, last + 1):
        owner[k] = (match.group(1), i, last)
    i = last + 1

foundation_macros = set()
for header in sorted((repo / "include" / "foundation").rglob("*.h")):
    for line in header.read_text(errors="replace").splitlines():
        match = DEFINE.match(line)
        if match is not None:
            foundation_macros.add(match.group(1))

removed, added = set(), []
matcher = difflib.SequenceMatcher(a=old, b=new, autojunk=False)
for tag, i1, i2, j1, j2 in matcher.get_opcodes():
    if tag in ("delete", "replace"):
        removed.update(range(i1, i2))
    if tag in ("insert", "replace"):
        added.extend(new[j1:j2])

reasons, macros, includes, seen = [], [], [], set()
for k in sorted(removed):
    if k not in owner:
        reasons.append(f"line {k + 1} of the base is not part of a macro definition: {old[k].strip()}")
        continue
    name, first, last = owner[k]
    if first in seen:
        continue
    seen.add(first)
    if not set(range(first, last + 1)) <= removed:
        reasons.append(f"the definition of {name} is removed in part, not whole")
    elif name not in foundation_macros:
        reasons.append(f"{name} is removed, but no header under include/foundation/ defines it")
    else:
        macros.append(name)
for line in added:
    match = FOUNDATION_INCLUDE.match(line)
    if match is None:
        reasons.append(f"an added line is not an include of a foundation header: {line.strip()}")
    elif not (repo / "include" / match.group(1)).is_file():
        reasons.append(f"an added line includes {match.group(1)}, which does not exist")
    else:
        includes.append(match.group(1))
if not reasons and not macros and not includes:
    reasons.append("the file differs from its base in no admitted way")

if reasons:
    for reason in reasons:
        print(reason)
    sys.exit(1)
print(f"removes {', '.join(macros) or 'no macro'}, and includes {', '.join(includes) or 'no header'}")
PY
}

# Prints the first three refusal reasons of a failed unification check,
# indented under the violation line that precedes it, and the count of
# the rest.  An ordinary edit to a frozen file refuses on every line it
# touches, and three lines are enough to say why.
print_unification_reasons() {
    local reasons="$1" line shown=0 total=0
    [[ -n "$reasons" ]] || return 0
    while IFS= read -r line; do
        total=$((total + 1))
        if ((shown < 3)); then
            printf '    not a macro unification: %s\n' "$line" >&2
            shown=$((shown + 1))
        fi
    done <<<"$reasons"
    if ((total > shown)); then
        printf '    not a macro unification: %d more lines\n' "$((total - shown))" >&2
    fi
}

scan() {
    # $1 = repo root, $2 = base commit.  Prints violations to stderr.
    local repo="$1" base="$2" rc=0 status path dest verdict
    if ! git -C "$repo" cat-file -e "${base}^{commit}" 2>/dev/null; then
        # Exit 3 is "cannot decide", distinct from a violation and from a bad
        # invocation.  ctest registers it as SKIP_RETURN_CODE, so a shallow
        # clone in the build job skips this guard instead of failing it; the
        # `layers` CI job checks out full history and enforces it.
        printf 'check-frozen-tree: the freeze base %s is not in this clone (shallow checkout?).  Fetch full history (fetch-depth: 0 in CI).\n' "$base" >&2
        return 3
    fi
    # The ledger is read before the scan trusts it, so a rotted entry
    # fails the guard even when no frozen path changed.  This is the
    # fail-closed half of the soundness-mirror exception.
    validate_ledger "$repo" || rc=1
    # Committed + uncommitted, against the base.  -z keeps paths with spaces
    # intact; status letters: A M D R C T.
    while IFS= read -r -d '' status && IFS= read -r -d '' path; do
        dest="$path"
        case "$status" in
            R*|C*) IFS= read -r -d '' dest ;;
        esac
        case "$status" in
            D) continue ;;
            R*|C*)
                if is_frozen "$dest" && ! is_marking_of_base "$repo" "$base" "$dest"; then
                    # A content edit to an already-ported _Name file is
                    # reported here as a rename, because dir/_Name is
                    # absent from the base.  Admit it when the ledger
                    # lists it; otherwise it is a change into a frozen path.
                    if mirror_admits "$repo" "$base" "$dest" "$status"; then
                        printf 'check-frozen-tree: ADMITTED soundness mirror: %s — %s (per %s).\n' \
                            "$dest" "$(mirror_reason "$repo" "$dest")" "$FROZEN_MIRROR_LEDGER" >&2
                    elif verdict="$(unification_admits "$repo" "$base" "$dest")"; then
                        printf 'check-frozen-tree: ADMITTED macro unification: %s — %s.\n' "$dest" "$verdict" >&2
                    else
                        printf 'FROZEN violation: %s — %s into a frozen path (from %s).  The old substrate only shrinks, or a ported file is marked _Name with its content unchanged.\n' \
                            "$dest" "$status" "$path" >&2
                        print_unification_reasons "$verdict"
                        rc=1
                    fi
                fi
                ;;
            *)
                if is_frozen "$path" && ! is_marking_of_base "$repo" "$base" "$path"; then
                    # Only a modify, or a content edit to an already-ported
                    # _Name file, and only a listed one.  An unlisted change,
                    # a type change or a genuine new frozen file falls through
                    # to the violation.
                    if mirror_admits "$repo" "$base" "$path" "$status"; then
                        printf 'check-frozen-tree: ADMITTED soundness mirror: %s — %s (per %s).\n' \
                            "$path" "$(mirror_reason "$repo" "$path")" "$FROZEN_MIRROR_LEDGER" >&2
                    elif verdict="$(unification_admits "$repo" "$base" "$path")"; then
                        printf 'check-frozen-tree: ADMITTED macro unification: %s — %s.\n' "$path" "$verdict" >&2
                    else
                        printf 'FROZEN violation: %s — %s under a frozen path.  The old substrate only shrinks; put the change in the new tree.  The permitted edits are an include of a frozen header gaining the _ marking, a modify listed in %s, and a macro unification.\n' \
                            "$path" "$status" "$FROZEN_MIRROR_LEDGER" >&2
                        print_unification_reasons "$verdict"
                        rc=1
                    fi
                fi
                ;;
        esac
    done < <(git -C "$repo" diff --name-status -z "$base" -- 2>/dev/null)
    # Untracked files are not in the diff; an untracked file under a frozen
    # path is an add that has not been staged yet.
    while IFS= read -r -d '' path; do
        if is_frozen "$path" && ! is_marking_of_base "$repo" "$base" "$path"; then
            printf 'FROZEN violation: %s — untracked file under a frozen path.\n' "$path" >&2
            rc=1
        fi
    done < <(git -C "$repo" ls-files --others --exclude-standard -z 2>/dev/null)
    return "$rc"
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test)
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        git -C "$tmp_root" init -q
        git -C "$tmp_root" -c user.name=selftest -c user.email=selftest@invalid config commit.gpgsign false
        mkdir -p "$tmp_root/include/crucible/safety" "$tmp_root/include/crucible/fixy" \
                 "$tmp_root/include/foundation" "$tmp_root/src/fixy" "$tmp_root/examples/fn"
        # Distinct contents, or git's rename detection pairs the deleted file
        # with the renamed one and the fixture stops testing what it names.
        printf '// old\n' >"$tmp_root/include/crucible/safety/Old.h"
        printf '// gone\n' >"$tmp_root/include/crucible/fixy/Gone.h"
        printf '// renamed\n' >"$tmp_root/include/crucible/fixy/Renamed.h"
        printf '// fs\n' >"$tmp_root/src/fixy/_Fs.cpp"
        printf '// keep\n' >"$tmp_root/examples/fn/keep.cpp"
        printf '#pragma once\n#include <crucible/safety/Twin.h>\n// ported\n' >"$tmp_root/include/crucible/safety/Ported.h"
        printf '#pragma once\n// twin\n' >"$tmp_root/include/crucible/safety/Twin.h"
        printf '#pragma once\n#include <crucible/safety/Twin.h>\n// still frozen\n' >"$tmp_root/include/crucible/safety/Includer.h"
        printf '#pragma once\n// tampered\n' >"$tmp_root/include/crucible/safety/Tampered.h"
        printf '#pragma once\n// mirror base\n' >"$tmp_root/include/crucible/safety/Mirror.h"
        printf '#pragma once\n// mirror2 base ported\n' >"$tmp_root/include/crucible/safety/Mirror2.h"
        # A header at the root of include/crucible/, and a frozen file that
        # includes it.  Every other fixture here includes through a
        # subdirectory, which is why a normalizer that required one went
        # unnoticed.
        printf '#pragma once\n// root\n' >"$tmp_root/include/crucible/Root.h"
        printf '#pragma once\n#include <crucible/Root.h>\n// roots includer\n' >"$tmp_root/include/crucible/safety/RootIncluder.h"
        git -C "$tmp_root" add -A
        git -C "$tmp_root" -c user.name=selftest -c user.email=selftest@invalid commit -q -m base
        base="$(git -C "$tmp_root" rev-parse HEAD)"

        # Clean tree reads clean.
        out="$(mktemp)"
        rc=0; scan "$tmp_root" "$base" 2>"$out" || rc=$?
        fail() {
            printf 'check-frozen-tree: SELF-TEST FAILED — %s\n' "$1" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' "$(cat "$out")" >&2
            rm -f "$out"; exit 2
        }
        [[ "$rc" -eq 0 ]] || fail "clean tree reported $rc"

        # Plant: a modify (frozen), a delete (frozen, allowed), an untracked add
        # (frozen), a rename into a frozen path, an add in the new tree
        # (allowed), a modify of the frozen single file.
        printf '// edited\n' >"$tmp_root/include/crucible/safety/Old.h"
        git -C "$tmp_root" rm -q "include/crucible/fixy/Gone.h"
        printf '// new\n' >"$tmp_root/include/crucible/safety/New.h"
        git -C "$tmp_root" mv "include/crucible/fixy/Renamed.h" "include/crucible/fixy/Moved.h"
        printf '// new layer\n' >"$tmp_root/include/foundation/Fine.h"
        printf '// edited\n' >"$tmp_root/src/fixy/_Fs.cpp"
        # The superseded marking: Twin.h is marked, Ported.h is marked and its
        # include follows, Includer.h stays but its include follows.  All three
        # pass.  Tampered.h is marked AND edited, which is not a marking.
        git -C "$tmp_root" mv "include/crucible/safety/Twin.h" "include/crucible/safety/_Twin.h"
        git -C "$tmp_root" mv "include/crucible/safety/Ported.h" "include/crucible/safety/_Ported.h"
        printf '#pragma once\n#include <crucible/safety/_Twin.h>\n// ported\n' >"$tmp_root/include/crucible/safety/_Ported.h"
        printf '#pragma once\n#include <crucible/safety/_Twin.h>\n// still frozen\n' >"$tmp_root/include/crucible/safety/Includer.h"
        git -C "$tmp_root" mv "include/crucible/safety/Tampered.h" "include/crucible/safety/_Tampered.h"
        printf '#pragma once\n// tampered, and edited\n' >"$tmp_root/include/crucible/safety/_Tampered.h"
        # The root-level marking: Root.h is not frozen and may be marked
        # freely, and the frozen file that includes it follows.  That is the
        # permitted edit, on a header with no directory between it and
        # crucible/.
        git -C "$tmp_root" mv "include/crucible/Root.h" "include/crucible/_Root.h"
        printf '#pragma once\n#include <crucible/_Root.h>\n// roots includer\n' >"$tmp_root/include/crucible/safety/RootIncluder.h"
        # A soundness mirror on a PLAIN frozen file: a content edit the
        # ledger admits.  Git reports it as a modify.  It must pass where
        # the unledgered edit to Old.h above fails.
        printf '#pragma once\n// mirror edited\n' >"$tmp_root/include/crucible/safety/Mirror.h"
        # A soundness mirror on an ALREADY-PORTED file: the base held
        # Mirror2.h, the port renamed it to _Mirror2.h, and now it takes a
        # content edit.  Git reports this as a rename-plus-edit, not a
        # modify, and the ledger must admit it just the same.  Tampered.h
        # above is the negative twin: marked, edited, and NOT listed.
        git -C "$tmp_root" mv "include/crucible/safety/Mirror2.h" "include/crucible/safety/_Mirror2.h"
        printf '#pragma once\n// mirror2 ported, and soundness-edited\n' \
            >"$tmp_root/include/crucible/safety/_Mirror2.h"
        # The ledger holds only these two valid entries, so the planted
        # scan also proves a good ledger passes validation.
        mkdir -p "$tmp_root/scripts"
        {
            printf '# self-test ledger\n'
            printf 'include/crucible/safety/Mirror.h — include/foundation/safety/Mirror.h — a live bug the new tree already fixed\n'
            printf 'include/crucible/safety/_Mirror2.h — include/foundation/safety/Mirror2.h — a live bug the new tree already fixed in the ported file\n'
        } >"$tmp_root/scripts/frozen-soundness-mirrors.txt"
        git -C "$tmp_root" add -A
        rc=0; scan "$tmp_root" "$base" 2>"$out" || rc=$?
        [[ "$rc" -eq 1 ]] || fail "planted tree reported $rc, want 1"
        grep -qF 'include/crucible/safety/Old.h' "$out" || fail "modify under a frozen dir not caught"
        grep -qF 'include/crucible/safety/New.h' "$out" || fail "untracked add under a frozen dir not caught"
        grep -qF 'include/crucible/fixy/Moved.h' "$out" || fail "rename into a frozen dir not caught"
        grep -qF 'src/fixy/_Fs.cpp' "$out" || fail "modify of a frozen single file not caught"
        if grep -qF 'violation: include/crucible/fixy/Gone.h' "$out"; then fail "a deletion was flagged"; fi
        if grep -qF 'include/foundation/Fine.h' "$out"; then fail "an add in the new tree was flagged"; fi
        if grep -qF '_Twin.h' "$out"; then fail "a plain superseded marking was flagged"; fi
        if grep -qF '_Ported.h' "$out"; then fail "a superseded marking whose include followed was flagged"; fi
        if grep -qF 'safety/Includer.h' "$out"; then fail "an include-only edit following a marking was flagged"; fi
        if grep -qF 'RootIncluder.h' "$out"; then fail "an include-only edit following the marking of a root-level header was flagged"; fi
        grep -qF '_Tampered.h' "$out" || fail "a marking that also edits content was not caught"
        # The ledgered mirror passes where the unledgered Old.h fails, and
        # a valid ledger raises no rot of its own during the scan.
        grep -qF 'ADMITTED soundness mirror: include/crucible/safety/Mirror.h' "$out" \
            || fail "a ledgered modify was not admitted"
        if grep -qF 'violation: include/crucible/safety/Mirror.h' "$out"; then
            fail "a ledgered modify was flagged as a violation"
        fi
        grep -qF 'ADMITTED soundness mirror: include/crucible/safety/_Mirror2.h' "$out" \
            || fail "a ledgered edit to an already-ported file (rename) was not admitted"
        if grep -qF 'violation: include/crucible/safety/_Mirror2.h' "$out"; then
            fail "a ledgered edit to an already-ported file was flagged as a violation"
        fi
        if grep -qF 'SOUNDNESS-MIRROR-LEDGER:' "$out"; then
            fail "a valid ledger entry was reported as rot"
        fi
        # Exactly five edits were planted to violate.  Naming each of the
        # five and clearing each of the five permitted shapes still lets a
        # sixth report through on a path the arms above never look at, so
        # pin the total as well.
        violation_count="$(grep -c 'FROZEN violation:' "$out" || true)"
        [[ "$violation_count" -eq 5 ]] || fail "expected exactly 5 violations, got $violation_count"
        rm -f "$out"

        # Fail-closed on the ledger's own rot.  validate_ledger is the
        # guard's ledger check, and the scan runs it first, so a bad entry
        # fails the whole guard.  Two negative controls, tested directly so
        # the planted violations above cannot mask the result.
        out2="$(mktemp)"
        fail2() {
            printf 'check-frozen-tree: SELF-TEST FAILED — %s\n' "$1" >&2
            printf '── ledger stderr ───\n%s\n────────────────────\n' "$(cat "$out2")" >&2
            rm -f "$out2"; exit 2
        }
        # A ledger entry naming a path that does not exist.
        printf 'include/crucible/safety/Ghost.h — ref — names a file that is not here\n' \
            >"$tmp_root/scripts/frozen-soundness-mirrors.txt"
        rc=0; validate_ledger "$tmp_root" 2>"$out2" || rc=$?
        [[ "$rc" -ne 0 ]] || fail2 "a ledger entry for a nonexistent path did not fail"
        grep -qF 'SOUNDNESS-MIRROR-LEDGER: include/crucible/safety/Ghost.h' "$out2" \
            || fail2 "the nonexistent-path ledger entry was not named"
        # A ledger entry naming a real file that is not under a frozen dir.
        printf 'include/foundation/Fine.h — ref — a real file, but not frozen\n' \
            >"$tmp_root/scripts/frozen-soundness-mirrors.txt"
        rc=0; validate_ledger "$tmp_root" 2>"$out2" || rc=$?
        [[ "$rc" -ne 0 ]] || fail2 "a ledger entry for a non-frozen path did not fail"
        grep -qF 'SOUNDNESS-MIRROR-LEDGER: include/foundation/Fine.h' "$out2" \
            || fail2 "the non-frozen ledger entry was not named"
        rm -f "$out2"

        # The macro unification.  The planted tree above becomes a new
        # base, so the second scan sees only the edits below.  Two
        # admitted shapes and five refused ones, one refused shape per
        # rule of the exception.
        printf '# self-test ledger\n' >"$tmp_root/scripts/frozen-soundness-mirrors.txt"
        mkdir -p "$tmp_root/include/foundation/contracts"
        printf '#pragma once\n#define UNI_ONE(c) ((void)(c))\n#define UNI_TWO(c) \\\n    ((void)(c))\n' \
            >"$tmp_root/include/foundation/contracts/Uni.h"
        uni_base='#pragma once\n#include <crucible/safety/Old.h>\n#define UNI_ONE(c) ((void)0)\n#define UNI_TWO(c) \\\n    ((void)0)\nint uni = 1;\n'
        for name in UniPlain UniPorted UniLacks UniNonMacro UniBadInclude UniMissing UniPartial; do
            # shellcheck disable=SC2059
            printf "$uni_base" >"$tmp_root/include/crucible/safety/$name.h"
            printf '// %s\n' "$name" >>"$tmp_root/include/crucible/safety/$name.h"
        done
        printf '#pragma once\n#define UNI_OLD_ONLY 1\nint uni = 1;\n// UniLacks\n' \
            >"$tmp_root/include/crucible/safety/UniLacks.h"
        git -C "$tmp_root" add -A
        git -C "$tmp_root" -c user.name=selftest -c user.email=selftest@invalid commit -q -m unification-base
        base3="$(git -C "$tmp_root" rev-parse HEAD)"

        # Admitted: two whole definitions go and a foundation include
        # comes in, on a plain file and on a file that is marked _Name in
        # the same change.
        uni_edit='#pragma once\n#include <crucible/safety/Old.h>\n#include <foundation/contracts/Uni.h>\nint uni = 1;\n'
        # shellcheck disable=SC2059
        printf "$uni_edit" >"$tmp_root/include/crucible/safety/UniPlain.h"
        printf '// UniPlain\n' >>"$tmp_root/include/crucible/safety/UniPlain.h"
        git -C "$tmp_root" mv "include/crucible/safety/UniPorted.h" "include/crucible/safety/_UniPorted.h"
        # shellcheck disable=SC2059
        printf "$uni_edit" >"$tmp_root/include/crucible/safety/_UniPorted.h"
        printf '// UniPorted\n' >>"$tmp_root/include/crucible/safety/_UniPorted.h"
        # Refused: the removed macro has no foundation definition.
        printf '#pragma once\nint uni = 1;\n// UniLacks\n' >"$tmp_root/include/crucible/safety/UniLacks.h"
        # Refused: a non-macro line changes beside an admitted removal.
        printf '#pragma once\n#include <crucible/safety/Old.h>\n#define UNI_TWO(c) \\\n    ((void)0)\nint uni = 2;\n// UniNonMacro\n' \
            >"$tmp_root/include/crucible/safety/UniNonMacro.h"
        # Refused: the added include is not a foundation header.
        printf '#pragma once\n#include <crucible/safety/Old.h>\n#include <crucible/safety/Twin.h>\n#define UNI_TWO(c) \\\n    ((void)0)\nint uni = 1;\n// UniBadInclude\n' \
            >"$tmp_root/include/crucible/safety/UniBadInclude.h"
        # Refused: the added foundation include names no file.
        printf '#pragma once\n#include <crucible/safety/Old.h>\n#include <foundation/contracts/Missing.h>\n#define UNI_TWO(c) \\\n    ((void)0)\nint uni = 1;\n// UniMissing\n' \
            >"$tmp_root/include/crucible/safety/UniMissing.h"
        # Refused: the first line of a definition goes and its
        # continuation line stays.
        printf '#pragma once\n#include <crucible/safety/Old.h>\n#define UNI_ONE(c) ((void)0)\n    ((void)0)\nint uni = 1;\n// UniPartial\n' \
            >"$tmp_root/include/crucible/safety/UniPartial.h"
        git -C "$tmp_root" add -A
        out3="$(mktemp)"
        fail3() {
            printf 'check-frozen-tree: SELF-TEST FAILED — %s\n' "$1" >&2
            printf '── unification stderr ───\n%s\n────────────────────\n' "$(cat "$out3")" >&2
            rm -f "$out3"; exit 2
        }
        rc=0; scan "$tmp_root" "$base3" 2>"$out3" || rc=$?
        [[ "$rc" -eq 1 ]] || fail3 "the unification tree reported $rc, want 1"
        grep -qF 'ADMITTED macro unification: include/crucible/safety/UniPlain.h — removes UNI_ONE, UNI_TWO, and includes foundation/contracts/Uni.h' "$out3" \
            || fail3 "a unification of a plain file was not admitted"
        grep -qF 'ADMITTED macro unification: include/crucible/safety/_UniPorted.h' "$out3" \
            || fail3 "a unification of a file marked in the same change was not admitted"
        grep -qF 'violation: include/crucible/safety/UniLacks.h' "$out3" \
            || fail3 "the removal of a macro the new tree lacks was not refused"
        grep -qF 'UNI_OLD_ONLY is removed, but no header under include/foundation/ defines it' "$out3" \
            || fail3 "the refusal of UniLacks.h did not name the macro"
        grep -qF 'violation: include/crucible/safety/UniNonMacro.h' "$out3" \
            || fail3 "a changed non-macro line was not refused"
        grep -qF 'not part of a macro definition: int uni = 1;' "$out3" \
            || fail3 "the refusal of UniNonMacro.h did not name the line"
        grep -qF 'violation: include/crucible/safety/UniBadInclude.h' "$out3" \
            || fail3 "an added include outside foundation was not refused"
        grep -qF 'not an include of a foundation header: #include <crucible/safety/Twin.h>' "$out3" \
            || fail3 "the refusal of UniBadInclude.h did not name the include"
        grep -qF 'violation: include/crucible/safety/UniMissing.h' "$out3" \
            || fail3 "an added include of a missing foundation header was not refused"
        grep -qF 'violation: include/crucible/safety/UniPartial.h' "$out3" \
            || fail3 "a definition removed in part was not refused"
        grep -qF 'the definition of UNI_TWO is removed in part, not whole' "$out3" \
            || fail3 "the refusal of UniPartial.h did not name the macro"
        violation_count="$(grep -c 'FROZEN violation:' "$out3" || true)"
        [[ "$violation_count" -eq 5 ]] || fail3 "expected exactly 5 unification refusals, got $violation_count"
        admitted_count="$(grep -c 'ADMITTED macro unification:' "$out3" || true)"
        [[ "$admitted_count" -eq 2 ]] || fail3 "expected exactly 2 admitted unifications, got $admitted_count"
        rm -f "$out3"

        printf 'check-frozen-tree: self-test passed — modify, add, rename-into, single-file and tampered-marking edits caught, five in total; deletion, new-tree adds and superseded markings clean; a ledgered soundness mirror on a plain file and on an already-ported file both admitted, and a dead or non-frozen ledger entry rejected; a macro unification admitted on a plain and a marked file, and refused for a macro the new tree lacks, a changed non-macro line, a non-foundation include, a missing foundation include and a definition removed in part.\n' >&2
        exit 0
        ;;
    "") ;;
    *) printf 'check-frozen-tree: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

if ! command -v git >/dev/null 2>&1; then
    printf 'check-frozen-tree: git is required\n' >&2
    exit 2
fi

scan_root="${CRUCIBLE_FROZEN_TEST_ROOT:-$root}"
base="${CRUCIBLE_FROZEN_TEST_BASE:-$FREEZE_BASE}"
rc=0
scan "$scan_root" "$base" || rc=$?

if [[ "$rc" -eq 1 ]]; then
    cat >&2 <<'HINT'

check-frozen-tree: the old substrate changed.  It is frozen until it is
deleted.  A fix belongs in include/foundation/ or include/fixy/; a
consumer that still needs the old tree moves to the new tree, and the old
tree is not patched.  Deleting old files is always allowed.
HINT
fi
exit "$rc"
