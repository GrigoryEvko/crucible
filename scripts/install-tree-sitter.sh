#!/usr/bin/env bash
# install-tree-sitter.sh — install the pinned C++ tree-sitter kit into the repo.
#
# The kit is a welded pair: the tree-sitter CLI from GrigoryEvko/tree-sitter and
# the C++ grammar from GrigoryEvko/tree-sitter-cpp.  Both are forks.  The
# grammar parses this tree's C++ correctly, where the upstream grammar reports
# an error on roughly one construct in three.  The runtime fork adds
# `ts_set_allocation_cap` and the allocation counters, and it declares a
# grammar ABI of 1019.  No upstream runtime loads an ABI-1019 grammar, and the
# Python bindings carry an upstream runtime.  This CLI is the only consumer that
# can read the grammar, so every AST gate calls it.
#
# The kit lands in .tools/ under the repo root.  The directory is gitignored.
# Crucible holds its own binary, and CI installs the same bytes from the same
# release.  Nothing reaches for a tree-sitter on the developer PATH, because a
# PATH binary is an unpinned input.
#
# FAIL-CLOSED PINNING.  The tarball must hash to the SHA256 written below.  The
# release also publishes a SHA256SUMS file, and this script does not read it: a
# re-uploaded asset carries a re-uploaded SHA256SUMS, so the file proves only
# self-consistency.  A hardcoded digest is the content address.  After the
# unpack the MANIFEST.txt lineage must match the pinned grammar commit, grammar
# ABI, runtime commit and runtime tag.  Then the CLI must parse a fixture that
# only the fork parses.  A failure at any step leaves nothing installed.
#
# MODES
#   (none)        Install when absent or off the pin.  Downloads.
#   --check       Verify the installed kit against the pin.  No network.
#   --print-kit   Print the kit directory.  No network.
#   --print-bin   Print the CLI path.  No network.
#   --self-test   Exercise the verification steps, positive and negative.
#
# EXIT CODES
#   0  success
#   2  verification failure — the pin was not met, and nothing was installed
#   3  the kit is absent, and the mode does not install it
#   4  the platform has no pinned kit, or a prerequisite tool is missing

set -Eeuo pipefail

REPO_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
INSTALL_ROOT="$REPO_ROOT/.tools/tree-sitter"

# ── The pin ────────────────────────────────────────────────────────────────
# Bump every field together.  A kit is one coherent lineage, and a partial bump
# installs a grammar the runtime does not admit.
KIT_TAG='cpp-fork-v4'
KIT_REPO='GrigoryEvko/tree-sitter-cpp'
PIN_GRAMMAR_COMMIT='c19c6bdfeb1398b70ad035d2d8ef94687f1286ce'
PIN_GRAMMAR_ABI='1019'
PIN_RUNTIME_COMMIT='31a914d29f8e909125df50f82512bf6477097568'
PIN_RUNTIME_TAG='v0.27.0-cpp-fork.8'

# SHA256 of each release tarball, read from the cpp-fork-v4 release on
# 2026-09-21 and verified against a downloaded asset.
#
# v4 carries two grammar fixes over v3, both for an attribute macro in a
# position the grammar did not admit: one in a parenthesized reference
# declarator (`T (&array CRUCIBLE_LIFETIMEBOUND)[N]`) and one among the
# function specifiers.  The fixes take this tree from 15 files with a parse
# error to 6.  The runtime did not move, so the runtime pin below still reads
# tag 8.
PIN_SHA_LINUX_X64='228eb401ee03c9fef8657ae7705d1b1bab78b1996f42308b9d16b5c7c3a65e67'
PIN_SHA_LINUX_ARM64='aa27cdbc2895dff50f2b46c9dd0e5c759daaf2dc172474a6c99f3e79e13c7fe5'
PIN_SHA_MACOS_ARM64='a2651a3e82d82bb62fedf4eb54688f9057d8e5f1abea2960992d85c05763c7ae'

# ── Platform ───────────────────────────────────────────────────────────────

# Print the pinned platform slug for an operating system and a machine name.
# Both arguments default to the running host.  Exit 4 when no kit is pinned.
platform_slug() {
    local os="${1:-$(uname -s)}" machine="${2:-$(uname -m)}"
    case "$os/$machine" in
        Linux/x86_64)          printf 'linux-x64\n' ;;
        Linux/aarch64|Linux/arm64) printf 'linux-arm64\n' ;;
        Darwin/arm64)          printf 'macos-arm64\n' ;;
        *)
            printf 'install-tree-sitter: no pinned kit for %s/%s.  Pinned: Linux/x86_64, Linux/aarch64, Darwin/arm64.\n' \
                "$os" "$machine" >&2
            return 4
            ;;
    esac
}

# Print the pinned SHA256 for a platform slug.
pinned_sha() {
    case "$1" in
        linux-x64)   printf '%s\n' "$PIN_SHA_LINUX_X64" ;;
        linux-arm64) printf '%s\n' "$PIN_SHA_LINUX_ARM64" ;;
        macos-arm64) printf '%s\n' "$PIN_SHA_MACOS_ARM64" ;;
        *) printf 'install-tree-sitter: no pinned SHA256 for platform %s.\n' "$1" >&2; return 4 ;;
    esac
}

# Print the SHA256 of a file in lowercase hexadecimal.
sha256_of() {
    local file="$1"
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum -- "$file" | cut -d' ' -f1
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 -- "$file" | cut -d' ' -f1
    elif command -v openssl >/dev/null 2>&1; then
        openssl dgst -sha256 -- "$file" | cut -d' ' -f2
    else
        printf 'install-tree-sitter: no SHA256 tool found.  Install coreutils, perl-Digest-SHA or openssl.\n' >&2
        return 4
    fi
}

# ── Verification ───────────────────────────────────────────────────────────

# Print the value of one MANIFEST.txt key, or the empty string.
manifest_value() {
    local manifest="$1" want="$2" key rest
    [[ -r "$manifest" ]] || return 0
    while read -r key rest; do
        if [[ "$key" == "$want" ]]; then
            printf '%s\n' "$rest"
            return 0
        fi
    done < "$manifest"
}

# Check a kit directory's MANIFEST.txt against the pinned lineage.
verify_lineage() {
    local kit="$1" manifest="$1/MANIFEST.txt" bad=0 got
    if [[ ! -r "$manifest" ]]; then
        printf 'install-tree-sitter: %s has no readable MANIFEST.txt.\n' "$kit" >&2
        return 2
    fi
    local pairs=(
        "grammar_commit:$PIN_GRAMMAR_COMMIT"
        "grammar_abi:$PIN_GRAMMAR_ABI"
        "runtime_commit:$PIN_RUNTIME_COMMIT"
        "runtime_tag:$PIN_RUNTIME_TAG"
    )
    local pair key want
    for pair in "${pairs[@]}"; do
        key="${pair%%:*}"
        want="${pair#*:}"
        got="$(manifest_value "$manifest" "$key")"
        if [[ "$got" != "$want" ]]; then
            printf 'install-tree-sitter: lineage mismatch — %s is %s, the pin says %s.\n' \
                "$key" "${got:-<absent>}" "$want" >&2
            bad=1
        fi
    done
    [[ "$bad" -eq 0 ]] || return 2
}

# Check that parse output carries the fork's own result for the fixture below.
# The upstream grammar reports an error inside the argument list.  The fork
# reports the conditional.  A version string cannot tell the two apart, and
# this can.
assert_fork_parse() {
    local out="$1"
    if [[ "$out" == *'ERROR'* || "$out" == *'MISSING'* ]]; then
        printf 'install-tree-sitter: the fixture parse reports an error node.  This CLI is not the fork.\n' >&2
        return 2
    fi
    if [[ "$out" != *'preproc_ifdef'* || "$out" != *'argument_list'* ]]; then
        printf 'install-tree-sitter: the fixture parse lacks the conditional inside the argument list.  This CLI is not the fork.\n' >&2
        return 2
    fi
}

# Parse the capability fixture with a kit and check the result.
verify_parse() {
    local kit="$1" work out rc=0
    work="$(mktemp -d)"
    cat > "$work/fixture.cpp" <<'FIXTURE'
void g(int);
void f() {
    g(
#ifdef A
        1
#else
        2
#endif
    );
}
FIXTURE
    if ! out="$(env -i HOME=/nonexistent "$kit/bin/tree-sitter" parse \
            --lib-path "$kit/lib/cpp.so" --lang-name cpp "$work/fixture.cpp" 2>&1)"; then
        printf 'install-tree-sitter: the CLI failed to run the fixture parse.\n%s\n' "$out" >&2
        rm -rf -- "$work"
        return 2
    fi
    assert_fork_parse "$out" || rc=$?
    rm -rf -- "$work"
    return "$rc"
}

# Print the kit directory for the pinned tag on this platform.
kit_dir() {
    local slug
    slug="$(platform_slug)" || return 4
    printf '%s/tree-sitter-cpp-fork-%s-%s\n' "$INSTALL_ROOT" "$KIT_TAG" "$slug"
}

# Verify an installed kit end to end: stamp, lineage, executable, parse.
verify_installed() {
    local kit="$1" slug want
    slug="$(platform_slug)" || return 4
    want="$(pinned_sha "$slug")" || return 4
    if [[ ! -d "$kit" ]]; then
        printf 'install-tree-sitter: the kit is absent from %s.\n' "$kit" >&2
        return 3
    fi
    local stamp="$kit/.crucible-pin"
    if [[ ! -r "$stamp" ]]; then
        printf 'install-tree-sitter: %s has no .crucible-pin stamp.  The install did not complete.\n' "$kit" >&2
        return 2
    fi
    local got_tag got_sha
    got_tag="$(manifest_value "$stamp" 'tag')"
    got_sha="$(manifest_value "$stamp" 'sha256')"
    if [[ "$got_tag" != "$KIT_TAG" || "$got_sha" != "$want" ]]; then
        printf 'install-tree-sitter: the stamp reads tag %s sha256 %s, the pin says tag %s sha256 %s.\n' \
            "${got_tag:-<absent>}" "${got_sha:-<absent>}" "$KIT_TAG" "$want" >&2
        return 2
    fi
    if [[ ! -x "$kit/bin/tree-sitter" ]]; then
        printf 'install-tree-sitter: %s/bin/tree-sitter is missing or not executable.\n' "$kit" >&2
        return 2
    fi
    verify_lineage "$kit" || return 2
    verify_parse "$kit" || return 2
}

# ── Install ────────────────────────────────────────────────────────────────

# Remove every kit directory that the pin does not name.  A kit directory
# carries its tag, so a tag bump otherwise leaves the previous kit on disk at
# 43 MB.  The glob stays inside the install root and matches only a kit
# directory, so nothing else can be reached.
prune_other_kits() {
    local keep="$1" stale
    for stale in "$INSTALL_ROOT"/tree-sitter-cpp-fork-*; do
        [[ -d "$stale" ]] || continue
        [[ "$stale" != "$keep" ]] || continue
        rm -rf -- "$stale"
        printf 'install-tree-sitter: removed the kit left by an earlier pin at %s\n' "$stale" >&2
    done
}

# Append the kit's bin directory to the GitHub Actions PATH file when one is set.
export_gha_path() {
    local kit="$1"
    if [[ -n "${GITHUB_PATH:-}" ]]; then
        printf '%s/bin\n' "$kit" >> "$GITHUB_PATH"
        printf 'install-tree-sitter: appended %s/bin to GITHUB_PATH.\n' "$kit" >&2
    fi
}

install_kit() {
    local kit slug want url work tarball got name
    kit="$(kit_dir)" || return 4
    slug="$(platform_slug)" || return 4
    want="$(pinned_sha "$slug")" || return 4

    if verify_installed "$kit" 2>/dev/null; then
        printf 'install-tree-sitter: already at the pin (%s, %s).\n' "$KIT_TAG" "$slug" >&2
        prune_other_kits "$kit"
        export_gha_path "$kit"
        return 0
    fi

    name="tree-sitter-cpp-fork-$KIT_TAG-$slug"
    url="https://github.com/$KIT_REPO/releases/download/$KIT_TAG/$name.tar.gz"

    mkdir -p -- "$INSTALL_ROOT"
    # The staging directory sits under the install root so the final move stays
    # on one filesystem and is atomic.
    work="$(mktemp -d -- "$INSTALL_ROOT/.staging.XXXXXX")"
    # shellcheck disable=SC2064
    trap "rm -rf -- '$work'" EXIT

    tarball="$work/$name.tar.gz"
    printf 'install-tree-sitter: download %s\n' "$url" >&2
    if command -v curl >/dev/null 2>&1; then
        curl -sSfL --retry 3 --max-time 300 -o "$tarball" -- "$url"
    elif command -v wget >/dev/null 2>&1; then
        wget -q -O "$tarball" -- "$url"
    else
        printf 'install-tree-sitter: no curl and no wget.\n' >&2
        return 4
    fi

    got="$(sha256_of "$tarball")" || return 4
    if [[ "$got" != "$want" ]]; then
        printf 'install-tree-sitter: SHA256 mismatch for %s.\n  got  %s\n  want %s\nNothing was installed.\n' \
            "$name.tar.gz" "$got" "$want" >&2
        return 2
    fi
    printf 'install-tree-sitter: SHA256 matches the pin.\n' >&2

    tar xzf "$tarball" -C "$work"
    if [[ ! -d "$work/$name" ]]; then
        printf 'install-tree-sitter: the tarball did not unpack to %s.\n' "$name" >&2
        return 2
    fi

    verify_lineage "$work/$name" || return 2
    verify_parse "$work/$name" || return 2

    {
        printf 'tag                %s\n' "$KIT_TAG"
        printf 'platform           %s\n' "$slug"
        printf 'sha256             %s\n' "$want"
        printf 'grammar_commit     %s\n' "$PIN_GRAMMAR_COMMIT"
        printf 'grammar_abi        %s\n' "$PIN_GRAMMAR_ABI"
        printf 'runtime_commit     %s\n' "$PIN_RUNTIME_COMMIT"
        printf 'runtime_tag        %s\n' "$PIN_RUNTIME_TAG"
        printf 'installed_by       scripts/install-tree-sitter.sh\n'
    } > "$work/$name/.crucible-pin"

    rm -rf -- "$kit"
    mv -- "$work/$name" "$kit"
    printf 'install-tree-sitter: installed %s at %s\n' "$KIT_TAG" "$kit" >&2

    prune_other_kits "$kit"
    export_gha_path "$kit"
}

# ── Self-test ──────────────────────────────────────────────────────────────

SELF_TEST_FAILED=0

self_test_case() {
    local name="$1" want="$2"
    shift 2
    local rc=0
    "$@" >/dev/null 2>&1 || rc=$?
    if [[ "$want" == 'pass' && "$rc" -eq 0 ]]; then
        printf '  ok   %s\n' "$name" >&2
    elif [[ "$want" == 'fail' && "$rc" -ne 0 ]]; then
        printf '  ok   %s (rejected, exit %d)\n' "$name" "$rc" >&2
    else
        printf '  FAIL %s (wanted %s, exit %d)\n' "$name" "$want" "$rc" >&2
        SELF_TEST_FAILED=1
    fi
}

# True when a file's SHA256 equals an expected digest.  The self-test needs a
# named function, because self_test_case runs its argument list as a command.
sha_equals() {
    [[ "$(sha256_of "$1")" == "$2" ]]
}

write_manifest() {
    local dir="$1" grammar="$2"
    mkdir -p -- "$dir"
    {
        printf 'kit                synthetic\n'
        printf 'grammar_commit     %s\n' "$grammar"
        printf 'grammar_abi        %s\n' "$PIN_GRAMMAR_ABI"
        printf 'runtime_commit     %s\n' "$PIN_RUNTIME_COMMIT"
        printf 'runtime_tag        %s\n' "$PIN_RUNTIME_TAG"
    } > "$dir/MANIFEST.txt"
}

self_test() {
    printf 'install-tree-sitter --self-test\n' >&2
    local work
    work="$(mktemp -d)"
    # shellcheck disable=SC2064
    trap "rm -rf -- '$work'" EXIT

    # Positive control: the SHA256 helper agrees with a known digest.
    printf 'abc' > "$work/abc"
    local known='ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad'
    self_test_case 'sha256_of computes the known digest of "abc"' pass \
        sha_equals "$work/abc" "$known"
    # Negative control: a wrong digest must not compare equal.
    self_test_case 'sha256_of rejects a wrong digest' fail \
        sha_equals "$work/abc" 'deadbeef'

    # Positive control: a MANIFEST on the pin passes the lineage check.
    write_manifest "$work/good" "$PIN_GRAMMAR_COMMIT"
    self_test_case 'verify_lineage accepts the pinned lineage' pass verify_lineage "$work/good"
    # Negative control: one wrong field fails it.
    write_manifest "$work/wrong" '0000000000000000000000000000000000000000'
    self_test_case 'verify_lineage rejects a wrong grammar commit' fail verify_lineage "$work/wrong"
    # Negative control: no MANIFEST at all fails it.
    mkdir -p -- "$work/empty"
    self_test_case 'verify_lineage rejects a kit with no MANIFEST' fail verify_lineage "$work/empty"

    # Positive control: fork-shaped parse output passes.
    self_test_case 'assert_fork_parse accepts the conditional in the argument list' pass \
        assert_fork_parse '(argument_list (preproc_ifdef (preproc_else)))'
    # Negative control: an error node fails.
    self_test_case 'assert_fork_parse rejects an ERROR node' fail \
        assert_fork_parse '(argument_list (preproc_ifdef) (ERROR))'
    # Negative control: upstream-shaped output, no conditional, fails.
    self_test_case 'assert_fork_parse rejects output with no conditional' fail \
        assert_fork_parse '(argument_list (number_literal))'

    # Positive control: a pinned platform resolves.
    self_test_case 'platform_slug resolves Linux/x86_64' pass platform_slug Linux x86_64
    # Negative control: an unpinned platform does not.
    self_test_case 'platform_slug rejects Windows/i686' fail platform_slug Windows i686

    if [[ "$SELF_TEST_FAILED" -ne 0 ]]; then
        printf 'install-tree-sitter --self-test: FAILED\n' >&2
        return 2
    fi
    printf 'install-tree-sitter --self-test: 10 cases pass, 6 of them negative controls.\n' >&2
}

# ── Entry ──────────────────────────────────────────────────────────────────

main() {
    case "${1:-install}" in
        install)
            install_kit
            ;;
        --check)
            local kit
            kit="$(kit_dir)" || return 4
            verify_installed "$kit" || return $?
            printf 'install-tree-sitter --check: %s is at the pin (%s).\n' "$kit" "$KIT_TAG" >&2
            ;;
        --print-kit|--print-bin)
            local kit rc=0
            kit="$(kit_dir)" || return 4
            verify_installed "$kit" 2>/dev/null || rc=$?
            if [[ "$rc" -ne 0 ]]; then
                printf 'install-tree-sitter: the pinned kit is not installed.  Run: bash scripts/install-tree-sitter.sh\n' >&2
                return 3
            fi
            if [[ "$1" == '--print-kit' ]]; then printf '%s\n' "$kit"; else printf '%s/bin/tree-sitter\n' "$kit"; fi
            ;;
        --self-test)
            self_test
            ;;
        -h|--help)
            printf 'usage: install-tree-sitter.sh [install|--check|--print-kit|--print-bin|--self-test]\n' >&2
            ;;
        *)
            printf 'install-tree-sitter: unknown mode %s.  Run --help.\n' "$1" >&2
            return 4
            ;;
    esac
}

main "$@"
