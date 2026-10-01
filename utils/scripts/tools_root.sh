# tools_root.sh — set TOOLS_ROOT to the directory of the pinned tools, and
# give the functions that each install script uses on it.
#
# An install script in utils/scripts sources this file:
#   . "$(dirname -- "${BASH_SOURCE[0]}")/tools_root.sh"
#
# THE ROOT
#   The pinned tools live in the tools directory of the shared cache root of
#   utils/scripts/cache_dir.py: $XDG_CACHE_HOME/crucible/tools, else
#   ~/.cache/crucible/tools.  The root is outside each checkout, so each
#   checkout, each work tree and each export of a guard run uses one install.
#   $CRUCIBLE_CACHE_DIR does not move the tools.  A self-test points that
#   variable at a scratch directory, and "off" turns each cache off.  Neither
#   may hide a pinned tool.
#
# THE DIRECTORIES
#   The name of a tool directory carries the pinned version and the first 16
#   hexadecimal digits of the pinned SHA-256, so two pins never share one
#   directory.  An install moves a verified directory into place with one
#   rename, and a rename never replaces a directory that holds a tool, so a
#   process that uses a tool never sees it change.
#
# THE BOUND
#   A run that uses a tool touches .last-use in its directory when the last
#   touch is more than one hour old.  An install removes each other directory
#   of the same tool that no run used for 14 days, so a checkout on an older
#   pin keeps its tool.  It also removes a staging directory that a stopped
#   install left for more than one day.

_tools_home=~
TOOLS_ROOT="${XDG_CACHE_HOME:-$_tools_home/.cache}/crucible/tools"
unset _tools_home

# The days of no use after which an install removes the directory of a tool.
TOOLS_UNUSED_DAYS=14

# Print the part of a SHA-256 digest that a tool directory carries in its name.
tool_digest_tag() {
    printf '%s\n' "${1:0:16}"
}

# Move a verified staging directory to its final path with one rename.  The
# rename fails when another install put a tool there first, and then the
# function returns 1 and changes nothing.
publish_tool_dir() {
    python3 -c 'import os, sys; os.rename(sys.argv[1], sys.argv[2])' "$1" "$2" 2>/dev/null
}

# Move a directory that fails verification out of the final path, so that an
# install can put a verified one there.  Returns 1 when the directory is gone
# already, because another install moved it.
retire_tool_dir() {
    local dir="$1" aside
    aside="$(mktemp -d -- "$TOOLS_ROOT/.retired.XXXXXX")" || return 1
    if python3 -c 'import os, sys; os.rename(sys.argv[1], sys.argv[2])' "$dir" "$aside" 2>/dev/null; then
        rm -rf -- "$aside"
        return 0
    fi
    rmdir -- "$aside"
    return 1
}

# Touch the .last-use file of a tool directory when the last touch is more
# than one hour old.  A tool directory that the user cannot write keeps no
# record, and the call still succeeds.
mark_tool_used() {
    local marker="$1/.last-use"
    if [[ ! -e "$marker" || -n "$(find "$marker" -mmin +60 2>/dev/null)" ]]; then
        touch -- "$marker" 2>/dev/null || true
    fi
}

# Remove each directory of one tool, other than the one to keep, that no run
# used for TOOLS_UNUSED_DAYS days.  Then remove each staging and retired
# directory older than one day.  Each glob stays inside TOOLS_ROOT.
#
# Complexity: one stat of each directory under TOOLS_ROOT.
prune_unused_tools() {
    local prefix="$1" keep="$2" stale marker
    for stale in "$TOOLS_ROOT/$prefix"*; do
        [[ -d "$stale" && "$stale" != "$keep" ]] || continue
        marker="$stale/.last-use"
        [[ -e "$marker" ]] || marker="$stale"
        # find counts whole days, so +N matches an age of N + 1 days or more.
        if [[ -n "$(find "$marker" -maxdepth 0 -mtime "+$((TOOLS_UNUSED_DAYS - 1))" 2>/dev/null)" ]]; then
            rm -rf -- "$stale"
            printf 'tools_root: removed %s, which no run used for %s days\n' "$stale" "$TOOLS_UNUSED_DAYS" >&2
        fi
    done
    for stale in "$TOOLS_ROOT"/.staging.* "$TOOLS_ROOT"/.retired.*; do
        [[ -d "$stale" ]] || continue
        if [[ -n "$(find "$stale" -maxdepth 0 -mtime +0 2>/dev/null)" ]]; then
            rm -rf -- "$stale"
        fi
    done
}

# ── Self-test ──────────────────────────────────────────────────────────────
#
# Each install script calls tools_root_self_test from its --self-test.  A
# check of a property also runs against a stand-in for the behavior that the
# property forbids, and the check must reject that stand-in.

# Print TOOLS_ROOT as this file sets it in the environment that the
# arguments give, with no XDG_CACHE_HOME and no CRUCIBLE_CACHE_DIR before.
tools_root_in() {
    env -u XDG_CACHE_HOME -u CRUCIBLE_CACHE_DIR "$@" bash -c '. "$1"; printf %s "$TOOLS_ROOT"' tools-root \
        "${BASH_SOURCE[0]}"
}

# Set the modification time of a file to a number of days ago, and make the
# file when it is absent.
set_age_days() {
    touch -- "$1"
    python3 -c 'import os, sys, time; t = time.time() - float(sys.argv[2]) * 86400; os.utime(sys.argv[1], (t, t))' \
        "$1" "$2"
}

# Print the modification time of a file in whole seconds.
mtime_of() {
    python3 -c 'import os, sys; print(int(os.stat(sys.argv[1]).st_mtime))' "$1"
}

# A stand-in for a publish that replaces the directory at the final path.
replacing_publish() {
    rm -rf -- "$2"
    mv -- "$1" "$2"
}

# A stand-in for a prune that removes each other directory of the tool.
prune_every_other_kit() {
    local stale
    for stale in "$TOOLS_ROOT/$1"*; do
        [[ -d "$stale" && "$stale" != "$2" ]] && rm -rf -- "$stale"
    done
    return 0
}

# Publish with the function $1 onto a path that holds a kit, in the fresh
# directory $2.  True when the kit at that path stays, and the publish
# reports the refusal.
check_publish_keeps_kit() {
    local publish="$1" root="$2"
    mkdir -p -- "$root/staged" "$root/taken"
    printf 'new\n' > "$root/staged/file"
    printf 'old\n' > "$root/taken/file"
    ! "$publish" "$root/staged" "$root/taken" && [[ "$(< "$root/taken/file")" == old ]]
}

# Publish with publish_tool_dir onto a free path, in the fresh directory $1.
check_publish_to_free_path() {
    local root="$1"
    mkdir -p -- "$root/staged"
    printf 'new\n' > "$root/staged/file"
    publish_tool_dir "$root/staged" "$root/free" && [[ "$(< "$root/free/file")" == new && ! -e "$root/staged" ]]
}

# Prune with the function $1 in the fresh directory $2.  True when the
# prune removes the kit of 15 days and the staging directory of 2 days, and
# keeps the kit of 13 days, the pinned kit, the kit of another tool and the
# staging directory of today.
check_prune() {
    local prune="$1" TOOLS_ROOT="$2" kit
    for kit in kit-old kit-recent kit-pinned other-old; do
        mkdir -p -- "$TOOLS_ROOT/$kit"
    done
    mkdir -p -- "$TOOLS_ROOT/.staging.old" "$TOOLS_ROOT/.staging.new"
    set_age_days "$TOOLS_ROOT/kit-old/.last-use" 15
    set_age_days "$TOOLS_ROOT/kit-recent/.last-use" 13
    set_age_days "$TOOLS_ROOT/kit-pinned/.last-use" 30
    set_age_days "$TOOLS_ROOT/other-old/.last-use" 30
    set_age_days "$TOOLS_ROOT/.staging.old" 2
    "$prune" kit- "$TOOLS_ROOT/kit-pinned" 2>/dev/null
    [[ ! -e "$TOOLS_ROOT/kit-old" && -d "$TOOLS_ROOT/kit-recent" && -d "$TOOLS_ROOT/kit-pinned" \
        && -d "$TOOLS_ROOT/other-old" && ! -e "$TOOLS_ROOT/.staging.old" && -d "$TOOLS_ROOT/.staging.new" ]]
}

# Mark the use of a tool in the fresh directory $1.  True when the call
# touches a marker of two hours, and keeps a marker of 30 minutes.
check_mark_used() {
    local dir="$1" fresh_before
    mkdir -p -- "$dir/stale" "$dir/fresh"
    set_age_days "$dir/stale/.last-use" 0.084
    set_age_days "$dir/fresh/.last-use" 0.021
    fresh_before="$(mtime_of "$dir/fresh/.last-use")"
    mark_tool_used "$dir/stale"
    mark_tool_used "$dir/fresh"
    [[ -z "$(find "$dir/stale/.last-use" -mmin +5)" && "$(mtime_of "$dir/fresh/.last-use")" == "$fresh_before" ]]
}

TOOLS_SELF_TEST_FAILED=0
TOOLS_SELF_TEST_TOTAL=0
TOOLS_SELF_TEST_NEGATIVES=0

# Run one case.  The second argument is pass or fail, the exit status that
# the command after it must give.
run_tools_case() {
    local name="$1" want="$2" rc=0
    shift 2
    "$@" >/dev/null 2>&1 || rc=$?
    TOOLS_SELF_TEST_TOTAL=$((TOOLS_SELF_TEST_TOTAL + 1))
    [[ "$want" == fail ]] && TOOLS_SELF_TEST_NEGATIVES=$((TOOLS_SELF_TEST_NEGATIVES + 1))
    if [[ ("$want" == pass && "$rc" -eq 0) || ("$want" == fail && "$rc" -ne 0) ]]; then
        printf '  ok   tools_root: %s\n' "$name" >&2
    else
        printf '  FAIL tools_root: %s (wanted %s, exit %d)\n' "$name" "$want" "$rc" >&2
        TOOLS_SELF_TEST_FAILED=1
    fi
}

# Run each case of this file.  Returns 2 when a case fails.
tools_root_self_test() {
    local work
    work="$(mktemp -d)"
    run_tools_case 'the root follows XDG_CACHE_HOME' pass \
        test "$(tools_root_in XDG_CACHE_HOME="$work/xdg" HOME="$work/home")" == "$work/xdg/crucible/tools"
    run_tools_case 'the root falls back to the .cache directory of HOME' pass \
        test "$(tools_root_in HOME="$work/home")" == "$work/home/.cache/crucible/tools"
    run_tools_case 'a scratch CRUCIBLE_CACHE_DIR does not move the root' pass \
        test "$(tools_root_in HOME="$work/home" CRUCIBLE_CACHE_DIR="$work/scratch")" == "$work/home/.cache/crucible/tools"
    run_tools_case 'CRUCIBLE_CACHE_DIR=off does not hide the root' pass \
        test "$(tools_root_in HOME="$work/home" CRUCIBLE_CACHE_DIR=off)" == "$work/home/.cache/crucible/tools"
    run_tools_case 'the root comparison rejects another root' fail \
        test "$(tools_root_in HOME="$work/home")" == "$work/elsewhere/.cache/crucible/tools"
    run_tools_case 'the digest tag is the first 16 digits' pass \
        test "$(tool_digest_tag 0123456789abcdef0123)" == 0123456789abcdef
    run_tools_case 'a publish moves a staged kit to a free path' pass check_publish_to_free_path "$work/free"
    run_tools_case 'a publish keeps a kit that another install placed' pass \
        check_publish_keeps_kit publish_tool_dir "$work/taken"
    run_tools_case 'the publish check rejects a publish that replaces the kit' fail \
        check_publish_keeps_kit replacing_publish "$work/replaced"
    run_tools_case 'a prune removes only what no run used' pass check_prune prune_unused_tools "$work/prune"
    run_tools_case 'the prune check rejects a prune of every other kit' fail \
        check_prune prune_every_other_kit "$work/prune-all"
    run_tools_case 'a use touches a stale marker and keeps a fresh one' pass check_mark_used "$work/mark"
    rm -rf -- "$work"
    if [[ "$TOOLS_SELF_TEST_FAILED" -ne 0 ]]; then
        printf 'tools_root self-test: FAILED\n' >&2
        return 2
    fi
    printf 'tools_root self-test: %d cases pass, %d of them negative controls.\n' \
        "$TOOLS_SELF_TEST_TOTAL" "$TOOLS_SELF_TEST_NEGATIVES" >&2
}
