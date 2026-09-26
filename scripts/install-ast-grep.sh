#!/usr/bin/env bash
# install-ast-grep.sh — install the pinned ast-grep binary into the repo.
#
# The guards read shell scripts with ast-grep's bash grammar, so a git call
# inside a heredoc or a comment is not a call.  The C++ guards do not use it:
# they read C++ with the tree-sitter kit (scripts/install-tree-sitter.sh),
# whose grammar is a C++26 fork.
#
# The binary lands in .tools/ under the repo root, which is gitignored.  CI
# installs the same bytes from the same release, and nothing reaches for an
# ast-grep on the PATH, because a PATH binary is an unpinned input.
#
# FAIL-CLOSED PINNING.  The release zip must hash to the SHA256 written below.
# After the unpack the binary must report the pinned version, and it must read
# a fixture the way the guards need: a git command in the script counts, and
# the same text in a heredoc body or a comment does not.  A failure at any step
# leaves nothing installed.
#
# MODES
#   (none)        Install when absent or off the pin.  Downloads.
#   --check       Verify the installed binary against the pin.  No network.
#   --print-bin   Print the binary path.  No network.
#   --self-test   Exercise the verification steps, positive and negative.
#
# EXIT CODES
#   0  success
#   2  verification failure: the pin was not met, and nothing was installed
#   3  the binary is absent, and the mode does not install it
#   4  the platform has no pinned binary, or a prerequisite tool is missing

set -Eeuo pipefail

REPO_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
INSTALL_ROOT="$REPO_ROOT/.tools/ast-grep"

# ── The pin ────────────────────────────────────────────────────────────────
# SHA256 of each release zip, read from the 0.45.3 release on 2026-09-26 by a
# download of each asset.
PIN_VERSION='0.45.3'
PIN_REPO='ast-grep/ast-grep'
PIN_SHA_LINUX_X64='f8ac830881339d1edee6b2652f54798c0f4da5a827f2db38a08ee31117783ce8'
PIN_SHA_LINUX_ARM64='b39cfbc58da4b869a88b8a4bc57bd5deb0d24541e704cf7c257da7b53ec81c8f'
PIN_SHA_MACOS_ARM64='6d2279dea5bea2ad79c66ea93f5fe54ba926e398a8a26de76c56db68fe59eac6'

# ── Platform ───────────────────────────────────────────────────────────────

# Print the release target triple for an operating system and a machine name.
# Both arguments default to the running host.  Exit 4 when no binary is pinned.
platform_triple() {
    local os="${1:-$(uname -s)}" machine="${2:-$(uname -m)}"
    case "$os/$machine" in
        Linux/x86_64)              printf 'x86_64-unknown-linux-gnu\n' ;;
        Linux/aarch64|Linux/arm64) printf 'aarch64-unknown-linux-gnu\n' ;;
        Darwin/arm64)              printf 'aarch64-apple-darwin\n' ;;
        *)
            printf 'install-ast-grep: no pinned binary for %s/%s.  Pinned: Linux/x86_64, Linux/aarch64, Darwin/arm64.\n' \
                "$os" "$machine" >&2
            return 4
            ;;
    esac
}

# Print the pinned SHA256 for a target triple.
pinned_sha() {
    case "$1" in
        x86_64-unknown-linux-gnu)  printf '%s\n' "$PIN_SHA_LINUX_X64" ;;
        aarch64-unknown-linux-gnu) printf '%s\n' "$PIN_SHA_LINUX_ARM64" ;;
        aarch64-apple-darwin)      printf '%s\n' "$PIN_SHA_MACOS_ARM64" ;;
        *) printf 'install-ast-grep: no pinned SHA256 for %s.\n' "$1" >&2; return 4 ;;
    esac
}

# Print the SHA256 of a file in lowercase hexadecimal.
sha256_of() {
    local file="$1"
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum -- "$file" | cut -d' ' -f1
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 -- "$file" | cut -d' ' -f1
    else
        printf 'install-ast-grep: no SHA256 tool found.  Install coreutils or perl-Digest-SHA.\n' >&2
        return 4
    fi
}

# ── Verification ───────────────────────────────────────────────────────────

# Check that the output of `ast-grep --version` names the pinned version.
assert_version() {
    if [[ "$1" != "ast-grep $PIN_VERSION" ]]; then
        printf 'install-ast-grep: the binary reports "%s", the pin says ast-grep %s.\n' "$1" "$PIN_VERSION" >&2
        return 2
    fi
}

# Check the matches of the fixture rule: exactly the command on line 0.  A
# match on line 2 is the heredoc body, and a match on line 4 is the comment.
assert_fixture_lines() {
    if [[ "$1" != '0' ]]; then
        printf 'install-ast-grep: the fixture matched on lines [%s], the guards need exactly [0].\n' "$1" >&2
        return 2
    fi
}

# Run a binary over the capability fixture and check the result.
verify_binary() {
    local bin="$1" work out lines rc=0
    out="$("$bin" --version 2>&1)" || { printf 'install-ast-grep: %s --version failed.\n' "$bin" >&2; return 2; }
    assert_version "$out" || return 2
    work="$(mktemp -d)"
    printf '%s\n' 'git -C "$tmp" commit -q -m x' 'cat > note <<EOF' 'git commit -m fixture' 'EOF' \
        '# git commit -m example' > "$work/fixture.sh"
    if ! out="$("$bin" run --lang bash -p 'git $$$ARGS' --json=compact "$work/fixture.sh" 2>&1)"; then
        printf 'install-ast-grep: the fixture run failed.\n%s\n' "$out" >&2
        rm -rf -- "$work"
        return 2
    fi
    rm -rf -- "$work"
    lines="$(printf '%s' "$out" | python3 -c \
        'import json, sys; print(",".join(str(m["range"]["start"]["line"]) for m in json.load(sys.stdin)))')" || rc=2
    [[ "$rc" -eq 0 ]] || { printf 'install-ast-grep: the fixture output is not JSON.\n' >&2; return 2; }
    assert_fixture_lines "$lines"
}

# Print the install directory for the pinned version on this platform.
install_dir() {
    local triple
    triple="$(platform_triple)" || return 4
    printf '%s/ast-grep-%s-%s\n' "$INSTALL_ROOT" "$PIN_VERSION" "$triple"
}

# Verify an installed binary end to end: stamp, executable, version, fixture.
verify_installed() {
    local dir="$1" triple want got
    triple="$(platform_triple)" || return 4
    want="$(pinned_sha "$triple")" || return 4
    if [[ ! -d "$dir" ]]; then
        printf 'install-ast-grep: the binary is absent from %s.\n' "$dir" >&2
        return 3
    fi
    if [[ ! -r "$dir/.crucible-pin" ]]; then
        printf 'install-ast-grep: %s has no .crucible-pin stamp.  The install did not complete.\n' "$dir" >&2
        return 2
    fi
    got="$(< "$dir/.crucible-pin")"
    if [[ "$got" != "$PIN_VERSION $want" ]]; then
        printf 'install-ast-grep: the stamp reads "%s", the pin says "%s %s".\n' "$got" "$PIN_VERSION" "$want" >&2
        return 2
    fi
    if [[ ! -x "$dir/ast-grep" ]]; then
        printf 'install-ast-grep: %s/ast-grep is missing or not executable.\n' "$dir" >&2
        return 2
    fi
    verify_binary "$dir/ast-grep"
}

# ── Install ────────────────────────────────────────────────────────────────

# Remove every install directory that the pin does not name.
prune_other_versions() {
    local keep="$1" stale
    for stale in "$INSTALL_ROOT"/ast-grep-*; do
        [[ -d "$stale" ]] || continue
        [[ "$stale" != "$keep" ]] || continue
        rm -rf -- "$stale"
        printf 'install-ast-grep: removed the binary left by an earlier pin at %s\n' "$stale" >&2
    done
}

install_binary() {
    local dir triple want url work zip got
    dir="$(install_dir)" || return 4
    triple="$(platform_triple)" || return 4
    want="$(pinned_sha "$triple")" || return 4

    if verify_installed "$dir" 2>/dev/null; then
        printf 'install-ast-grep: already at the pin (%s, %s).\n' "$PIN_VERSION" "$triple" >&2
        prune_other_versions "$dir"
        return 0
    fi
    command -v python3 >/dev/null 2>&1 || { printf 'install-ast-grep: python3 is required.\n' >&2; return 4; }

    url="https://github.com/$PIN_REPO/releases/download/$PIN_VERSION/app-$triple.zip"
    mkdir -p -- "$INSTALL_ROOT"
    # The staging directory sits under the install root, so the final move
    # stays on one filesystem and is atomic.
    work="$(mktemp -d -- "$INSTALL_ROOT/.staging.XXXXXX")"
    # shellcheck disable=SC2064
    trap "rm -rf -- '$work'" EXIT

    zip="$work/app.zip"
    printf 'install-ast-grep: download %s\n' "$url" >&2
    if command -v curl >/dev/null 2>&1; then
        curl -sSfL --retry 3 --max-time 300 -o "$zip" -- "$url"
    elif command -v wget >/dev/null 2>&1; then
        wget -q -O "$zip" -- "$url"
    else
        printf 'install-ast-grep: no curl and no wget.\n' >&2
        return 4
    fi

    got="$(sha256_of "$zip")" || return 4
    if [[ "$got" != "$want" ]]; then
        printf 'install-ast-grep: SHA256 mismatch for app-%s.zip.\n  got  %s\n  want %s\nNothing was installed.\n' \
            "$triple" "$got" "$want" >&2
        return 2
    fi
    printf 'install-ast-grep: SHA256 matches the pin.\n' >&2

    mkdir -- "$work/out"
    python3 -m zipfile -e "$zip" "$work/out"
    chmod +x -- "$work/out/ast-grep"
    verify_binary "$work/out/ast-grep" || return 2

    printf '%s %s\n' "$PIN_VERSION" "$want" > "$work/out/.crucible-pin"
    rm -rf -- "$dir"
    mv -- "$work/out" "$dir"
    printf 'install-ast-grep: installed ast-grep %s at %s\n' "$PIN_VERSION" "$dir" >&2
    prune_other_versions "$dir"
}

# ── Self-test ──────────────────────────────────────────────────────────────

SELF_TEST_FAILED=0
SELF_TEST_NEGATIVES=0

self_test_case() {
    local name="$1" want="$2"
    shift 2
    local rc=0
    "$@" >/dev/null 2>&1 || rc=$?
    if [[ "$want" == 'fail' ]]; then
        SELF_TEST_NEGATIVES=$((SELF_TEST_NEGATIVES + 1))
    fi
    if [[ "$want" == 'pass' && "$rc" -eq 0 ]]; then
        printf '  ok   %s\n' "$name" >&2
    elif [[ "$want" == 'fail' && "$rc" -ne 0 ]]; then
        printf '  ok   %s (rejected, exit %d)\n' "$name" "$rc" >&2
    else
        printf '  FAIL %s (wanted %s, exit %d)\n' "$name" "$want" "$rc" >&2
        SELF_TEST_FAILED=1
    fi
}

# True when a file's SHA256 equals an expected digest.
sha_equals() {
    [[ "$(sha256_of "$1")" == "$2" ]]
}

self_test() {
    printf 'install-ast-grep --self-test\n' >&2
    local work
    work="$(mktemp -d)"
    # shellcheck disable=SC2064
    trap "rm -rf -- '$work'" EXIT

    printf 'abc' > "$work/abc"
    self_test_case 'sha256_of computes the known digest of "abc"' pass \
        sha_equals "$work/abc" 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad'
    self_test_case 'sha256_of rejects a wrong digest' fail sha_equals "$work/abc" 'deadbeef'

    self_test_case 'assert_version accepts the pinned version' pass assert_version "ast-grep $PIN_VERSION"
    self_test_case 'assert_version rejects another version' fail assert_version 'ast-grep 0.1.0'

    self_test_case 'assert_fixture_lines accepts the one real command' pass assert_fixture_lines '0'
    self_test_case 'assert_fixture_lines rejects a match in the heredoc body' fail assert_fixture_lines '0,2'
    self_test_case 'assert_fixture_lines rejects a match in the comment' fail assert_fixture_lines '0,4'
    self_test_case 'assert_fixture_lines rejects no match' fail assert_fixture_lines ''

    self_test_case 'platform_triple resolves Linux/x86_64' pass platform_triple Linux x86_64
    self_test_case 'platform_triple rejects Windows/i686' fail platform_triple Windows i686

    if [[ "$SELF_TEST_FAILED" -ne 0 ]]; then
        printf 'install-ast-grep --self-test: FAILED\n' >&2
        return 2
    fi
    printf 'install-ast-grep --self-test: every case passes, %d of them negative controls.\n' \
        "$SELF_TEST_NEGATIVES" >&2
}

# ── Entry ──────────────────────────────────────────────────────────────────

main() {
    case "${1:-install}" in
        install)
            install_binary
            ;;
        --check)
            local dir
            dir="$(install_dir)" || return 4
            verify_installed "$dir" || return $?
            printf 'install-ast-grep --check: %s is at the pin (%s).\n' "$dir" "$PIN_VERSION" >&2
            ;;
        --print-bin)
            local dir rc=0
            dir="$(install_dir)" || return 4
            verify_installed "$dir" 2>/dev/null || rc=$?
            if [[ "$rc" -ne 0 ]]; then
                printf 'install-ast-grep: the pinned binary is not installed.  Run: bash scripts/install-ast-grep.sh\n' >&2
                return 3
            fi
            printf '%s/ast-grep\n' "$dir"
            ;;
        --self-test)
            self_test
            ;;
        -h|--help)
            printf 'usage: install-ast-grep.sh [install|--check|--print-bin|--self-test]\n' >&2
            ;;
        *)
            printf 'install-ast-grep: unknown mode %s.  Run --help.\n' "$1" >&2
            return 4
            ;;
    esac
}

main "$@"
