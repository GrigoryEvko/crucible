#!/usr/bin/env bash
# install-cmake.sh — install the pinned CMake and ctest.
#
# utils/toolchain/cmake/requirements.txt pins one version of CMake and ctest,
# with the SHA256 of the manylinux wheel of CMake on PyPI for each machine.
# This script downloads the wheel of the host from PyPI and makes sure that
# it has the pinned SHA256.  Then it unpacks the CMake prefix of the wheel
# (bin, share, doc) into the tools directory of the shared cache root
# (utils/scripts/tools_root.sh).  A wheel is a zip file, so the script needs
# no pip: python3 unpacks it.  The directory name carries the pinned version,
# the machine and the start of the pinned SHA256.
#
# Each CI job that configures or tests runs this script through
# .github/actions/pinned-cmake, which keeps the install directory in the
# Actions cache.  On a CI runner, the script puts the bin directory first in
# PATH for the steps after it.  The configure step of the tree rejects a CMake
# of another version (cmake/CMakePin.cmake).  The test cmake_pin rejects a
# ctest of another version (utils/scripts/cmake_pin.py).
#
# FAIL-CLOSED PINNING.  The wheel must hash to the pinned SHA256.  After the
# unpack, cmake and ctest must give the pinned version.  A failure at each step
# leaves nothing installed.
#
# MODES
#   (none)        Install when absent or off the pin.  Downloads.
#   --check       Verify the install against the pin.  No network.
#   --print-bin   Print the bin directory of the install.  No network.
#   --print-dir   Print the install directory, also when it is absent.  No network.
#   --self-test   Do a test of the verification steps, positive and negative.
#
# EXIT CODES
#   0  success
#   2  verification failure: the pin was not met, and nothing was installed
#   3  the install is absent, and the mode does not install it
#   4  the machine has no pinned wheel, or a prerequisite tool is missing

set -Eeuo pipefail

SCRIPTS="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
. "$SCRIPTS/tools_root.sh"
INSTALL_PREFIX='cmake-'
PYPI_FILES='https://files.pythonhosted.org/packages/py3/c/cmake'

# ── The pin ────────────────────────────────────────────────────────────────

# Print the machine of the host as the pin names it.  Exit 4 for a machine
# that the tree does not build on.
host_machine() {
    local machine="${1:-$(uname -m)}"
    case "$machine" in
        x86_64)        printf 'x86_64\n' ;;
        aarch64|arm64) printf 'aarch64\n' ;;
        *)
            printf 'install-cmake: no pinned wheel for the machine %s.  Pinned: x86_64, aarch64.\n' "$machine" >&2
            return 4
            ;;
    esac
}

# Set PIN_VERSION and PIN_SHA to the pinned version and the SHA256 of the
# wheel of a machine.  utils/scripts/cmake_pin.py reads the pin file.
read_pin() {
    local machine="$1" fields
    command -v python3 >/dev/null 2>&1 || { printf 'install-cmake: python3 is required.\n' >&2; return 4; }
    fields="$(python3 "$SCRIPTS/cmake_pin.py" --print-pin "$machine")" || return 4
    PIN_VERSION="${fields%% *}"
    PIN_SHA="${fields##* }"
}

# Print the file name of the manylinux wheel of a version and a machine.
wheel_name() {
    printf 'cmake-%s-py3-none-manylinux2014_%s.manylinux_2_17_%s.whl\n' "$1" "$2" "$2"
}

# Print the SHA256 of a file in lowercase hexadecimal.
sha256_of() {
    local file="$1"
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum -- "$file" | cut -d' ' -f1
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 -- "$file" | cut -d' ' -f1
    else
        printf 'install-cmake: no SHA256 tool found.  Install coreutils or perl-Digest-SHA.\n' >&2
        return 4
    fi
}

# ── Verification ───────────────────────────────────────────────────────────

# Check that the first line of `PROGRAM --version` gives the pinned version.
assert_version() {
    local name="$1" line="$2"
    if [[ "$line" != "$name version $PIN_VERSION" ]]; then
        printf 'install-cmake: %s gives "%s", the pin says %s version %s.\n' "$name" "$line" "$name" "$PIN_VERSION" >&2
        return 2
    fi
}

# Check that the cmake and the ctest of a bin directory give the pinned version.
verify_programs() {
    local bin="$1" name line
    for name in cmake ctest; do
        if [[ ! -x "$bin/$name" ]]; then
            printf 'install-cmake: %s/%s is missing or not executable.\n' "$bin" "$name" >&2
            return 2
        fi
        line="$("$bin/$name" --version 2>/dev/null)" || {
            printf 'install-cmake: %s/%s --version failed.\n' "$bin" "$name" >&2
            return 2
        }
        assert_version "$name" "${line%%$'\n'*}" || return 2
    done
}

# Print the install directory of the pin for the machine of the host.
install_dir() {
    local machine
    machine="$(host_machine)" || return 4
    read_pin "$machine" || return 4
    printf '%s/%s%s-%s-%s\n' "$TOOLS_ROOT" "$INSTALL_PREFIX" "$PIN_VERSION" "$machine" "$(tool_digest_tag "$PIN_SHA")"
}

# Verify an install end to end: the stamp, then the two programs.
verify_installed() {
    local dir="$1" machine got
    machine="$(host_machine)" || return 4
    read_pin "$machine" || return 4
    if [[ ! -d "$dir" ]]; then
        printf 'install-cmake: the install is absent from %s.\n' "$dir" >&2
        return 3
    fi
    if [[ ! -r "$dir/.crucible-pin" ]]; then
        printf 'install-cmake: %s has no .crucible-pin stamp.  The install did not complete.\n' "$dir" >&2
        return 2
    fi
    got="$(< "$dir/.crucible-pin")"
    if [[ "$got" != "$PIN_VERSION $PIN_SHA" ]]; then
        printf 'install-cmake: the stamp reads "%s", the pin says "%s %s".\n' "$got" "$PIN_VERSION" "$PIN_SHA" >&2
        return 2
    fi
    verify_programs "$dir/bin"
}

# Put the bin directory first in PATH for the steps after this one, on a CI runner.
export_bin() {
    if [[ -n "${GITHUB_PATH:-}" ]]; then
        printf '%s/bin\n' "$1" >> "$GITHUB_PATH"
        printf 'install-cmake: appended %s/bin to GITHUB_PATH.\n' "$1" >&2
    fi
}

# ── Install ────────────────────────────────────────────────────────────────

install_cmake() {
    local dir machine wheel url work got
    dir="$(install_dir)" || return 4
    machine="$(host_machine)" || return 4
    read_pin "$machine" || return 4

    if verify_installed "$dir" 2>/dev/null; then
        printf 'install-cmake: already at the pin (%s, %s) at %s.\n' "$PIN_VERSION" "$machine" "$dir" >&2
        mark_tool_used "$dir"
        prune_unused_tools "$INSTALL_PREFIX" "$dir"
        export_bin "$dir"
        return 0
    fi
    command -v curl >/dev/null 2>&1 || { printf 'install-cmake: curl is required.\n' >&2; return 4; }

    wheel="$(wheel_name "$PIN_VERSION" "$machine")"
    url="$PYPI_FILES/$wheel"
    mkdir -p -- "$TOOLS_ROOT"
    # The staging directory sits under the tools root, so the final rename
    # stays on one filesystem.
    work="$(mktemp -d -- "$TOOLS_ROOT/.staging.XXXXXX")"
    # shellcheck disable=SC2064
    trap "rm -rf -- '$work'" EXIT

    printf 'install-cmake: download %s\n' "$url" >&2
    curl -sSfL --retry 3 --max-time 300 -o "$work/$wheel" -- "$url"
    got="$(sha256_of "$work/$wheel")" || return 4
    if [[ "$got" != "$PIN_SHA" ]]; then
        printf 'install-cmake: SHA256 mismatch for %s.\n  got  %s\n  want %s\nNothing was installed.\n' \
            "$wheel" "$got" "$PIN_SHA" >&2
        return 2
    fi
    printf 'install-cmake: SHA256 matches the pin.\n' >&2

    # The CMake prefix of the wheel is cmake/data.  A zip file keeps no mode
    # bits that python3 restores, so the programs get theirs here.
    python3 -m zipfile -e "$work/$wheel" "$work/unpacked"
    mv -- "$work/unpacked/cmake/data" "$work/out"
    chmod +x -- "$work/out/bin/"*
    verify_programs "$work/out/bin" || return 2

    printf '%s %s\n' "$PIN_VERSION" "$PIN_SHA" > "$work/out/.crucible-pin"
    # A directory at the install path failed the verification above, so it
    # is damaged or incomplete.
    if [[ -e "$dir" ]]; then
        retire_tool_dir "$dir" || true
    fi
    if publish_tool_dir "$work/out" "$dir"; then
        printf 'install-cmake: installed CMake %s at %s\n' "$PIN_VERSION" "$dir" >&2
    elif verify_installed "$dir" 2>/dev/null; then
        printf 'install-cmake: another install put CMake at %s first.\n' "$dir" >&2
    else
        printf 'install-cmake: the verified install did not move to %s.  Nothing was installed.\n' "$dir" >&2
        return 2
    fi
    mark_tool_used "$dir"
    prune_unused_tools "$INSTALL_PREFIX" "$dir"
    export_bin "$dir"
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

# True when a command prints exactly the expected text.
prints() {
    local want="$1"
    shift
    [[ "$("$@")" == "$want" ]]
}

# True when the install directory sits in the tools root, and its name
# carries the pinned version, the machine and the first 16 digits of the
# pinned SHA256 of the machine.
install_dir_names_the_pin() {
    local dir machine
    dir="$(install_dir)" && machine="$(host_machine)" && read_pin "$machine" || return 1
    [[ "$(dirname -- "$dir")" == "$TOOLS_ROOT" && "$dir" == *"/$INSTALL_PREFIX$PIN_VERSION-$machine-${PIN_SHA:0:16}" ]]
}

# Write a stand-in install in DIR whose cmake and ctest give VERSION, with a
# stamp of STAMP.  An empty STAMP writes no stamp.
write_stand_in() {
    local dir="$1" version="$2" stamp="$3" name
    mkdir -p -- "$dir/bin"
    for name in cmake ctest; do
        printf '#!/bin/sh\necho "%s version %s"\necho\necho "CMake suite maintained and supported by Kitware."\n' \
            "$name" "$version" > "$dir/bin/$name"
        chmod +x -- "$dir/bin/$name"
    done
    if [[ -n "$stamp" ]]; then
        printf '%s\n' "$stamp" > "$dir/.crucible-pin"
    fi
}

self_test() {
    printf 'install-cmake --self-test\n' >&2
    local work machine
    work="$(mktemp -d)"
    # shellcheck disable=SC2064
    trap "rm -rf -- '$work'" EXIT
    machine="$(host_machine)"
    read_pin "$machine"

    printf 'abc' > "$work/abc"
    self_test_case 'sha256_of computes the known digest of "abc"' pass \
        sha_equals "$work/abc" 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad'
    self_test_case 'sha256_of rejects a wrong digest' fail sha_equals "$work/abc" 'deadbeef'

    self_test_case 'assert_version accepts the pinned version' pass assert_version cmake "cmake version $PIN_VERSION"
    self_test_case 'assert_version rejects another version' fail assert_version ctest 'ctest version 4.3.0'
    self_test_case 'assert_version rejects a version with a suffix' fail \
        assert_version cmake "cmake version $PIN_VERSION-rc1"
    self_test_case 'assert_version rejects the line of the other program' fail \
        assert_version ctest "cmake version $PIN_VERSION"

    self_test_case 'host_machine resolves x86_64' pass prints x86_64 host_machine x86_64
    self_test_case 'host_machine resolves arm64 to aarch64' pass prints aarch64 host_machine arm64
    self_test_case 'host_machine rejects riscv64' fail host_machine riscv64
    self_test_case 'the wheel of x86_64 is the manylinux2014 wheel' pass \
        prints 'cmake-4.4.2-py3-none-manylinux2014_x86_64.manylinux_2_17_x86_64.whl' wheel_name 4.4.2 x86_64

    write_stand_in "$work/pinned" "$PIN_VERSION" "$PIN_VERSION $PIN_SHA"
    write_stand_in "$work/other" '4.3.0' "$PIN_VERSION $PIN_SHA"
    write_stand_in "$work/unstamped" "$PIN_VERSION" ''
    write_stand_in "$work/stale" "$PIN_VERSION" "4.3.0 $PIN_SHA"
    self_test_case 'verify_installed accepts an install at the pin' pass verify_installed "$work/pinned"
    self_test_case 'verify_installed rejects programs of another version' fail verify_installed "$work/other"
    self_test_case 'verify_installed rejects an install with no stamp' fail verify_installed "$work/unstamped"
    self_test_case 'verify_installed rejects the stamp of another pin' fail verify_installed "$work/stale"
    self_test_case 'verify_installed rejects an absent install' fail verify_installed "$work/absent"
    rm -- "$work/pinned/bin/ctest"
    self_test_case 'verify_installed rejects an install with no ctest' fail verify_installed "$work/pinned"

    self_test_case 'install_dir names the pinned version, the machine and the SHA256 under the tools root' pass \
        install_dir_names_the_pin
    tools_root_self_test || SELF_TEST_FAILED=1

    if [[ "$SELF_TEST_FAILED" -ne 0 ]]; then
        printf 'install-cmake --self-test: FAILED\n' >&2
        return 2
    fi
    printf 'install-cmake --self-test: every case passes, %d of them negative controls.\n' "$SELF_TEST_NEGATIVES" >&2
}

# ── Entry ──────────────────────────────────────────────────────────────────

main() {
    case "${1:-install}" in
        install)
            install_cmake
            ;;
        --check)
            local dir
            dir="$(install_dir)" || return 4
            verify_installed "$dir" || return $?
            printf 'install-cmake --check: %s is at the pin (%s).\n' "$dir" "$PIN_VERSION" >&2
            ;;
        --print-bin)
            local dir rc=0
            dir="$(install_dir)" || return 4
            verify_installed "$dir" 2>/dev/null || rc=$?
            if [[ "$rc" -ne 0 ]]; then
                printf 'install-cmake: the pinned CMake is not installed.  Run: bash utils/scripts/install-cmake.sh\n' >&2
                return 3
            fi
            mark_tool_used "$dir"
            printf '%s/bin\n' "$dir"
            ;;
        --print-dir)
            install_dir
            ;;
        --self-test)
            self_test
            ;;
        -h|--help)
            printf 'usage: install-cmake.sh [install|--check|--print-bin|--print-dir|--self-test]\n' >&2
            ;;
        *)
            printf 'install-cmake: unknown mode %s.  Run --help.\n' "$1" >&2
            return 4
            ;;
    esac
}

main "$@"
