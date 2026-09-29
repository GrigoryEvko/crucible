#!/usr/bin/env bash
# Build the patched GCC that Crucible needs, and install it into a prefix.
#
# Local development and CI use this script.  It downloads one upstream commit
# (the BASE file names it), applies each file in patches/ in name order, and
# builds GCC with the configuration below.  CI keys its cache of the install on
# a hash of BASE, the patches and this script, so a change to one of them
# builds the compiler again.
#
# Usage: utils/toolchain/gcc/build.sh PREFIX [JOBS]
#   PREFIX  The install prefix.  The compiler is PREFIX/usr/bin/g++-16p.
#           Give PREFIX to CMake as CRUCIBLE_GCC16_PREFIX, or give the
#           compiler path as CRUCIBLE_CXX.
#   JOBS    The number of make jobs.  The preset value is the output of nproc.
#
# The source tree and the object tree go in a temporary directory below
# TMPDIR, or below /tmp if TMPDIR is not set.  The script deletes that
# directory when it stops.  The two trees need approximately 6 GB.  With
# 8 jobs the build takes approximately 8 minutes.
#
# The build needs git, tar, make, flex, a C and C++ compiler, and the
# development files of GMP, MPFR, MPC, ISL, zlib and zstd.  On Fedora:
#   dnf install gcc-c++ make flex git tar gmp-devel mpfr-devel libmpc-devel \
#       isl-devel zlib-devel libzstd-devel
set -euo pipefail
export LC_ALL=C

die() {
    printf 'utils/toolchain/gcc/build.sh: %s\n' "$*" >&2
    exit 1
}

[[ $# -ge 1 && $# -le 2 && -n $1 ]] || {
    printf 'usage: %s PREFIX [JOBS]\n' "$0" >&2
    exit 2
}
prefix=$(realpath -m -- "$1")
jobs=${2:-$(nproc)}
[[ $jobs =~ ^[1-9][0-9]*$ ]] || die "JOBS must be a positive integer, not '$jobs'"

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

url='' commit=''
while read -r key value _; do
    case $key in
        url) url=$value ;;
        commit) commit=$value ;;
    esac
done < "$here/BASE"
[[ -n $url ]] || die "$here/BASE names no url"
[[ $commit =~ ^[0-9a-f]{40}$ ]] || die "$here/BASE must name the commit with its full 40-digit hash"

shopt -s nullglob
patches=("$here"/patches/*.patch)
shopt -u nullglob
(( ${#patches[@]} > 0 )) || die "$here/patches holds no .patch file"

for tool in git tar make flex gcc g++; do
    command -v "$tool" > /dev/null || die "'$tool' is not on PATH.  Install it, then run the script again."
done
printf '#include <gmp.h>\n#include <mpfr.h>\n#include <mpc.h>\n#include <isl/version.h>\n#include <zlib.h>\n#include <zstd.h>\n' \
    | g++ -E -x c++ - > /dev/null \
    || die "a development header is missing.  Install the GMP, MPFR, MPC, ISL, zlib and zstd development packages."

work=$(mktemp -d "${TMPDIR:-/tmp}/crucible-gcc.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
src=$work/src
obj=$work/obj

printf 'build.sh: download %s at %s\n' "$url" "$commit"
# A repository variable in the caller's environment wins over `git -C`.  With
# GIT_DIR exported, the init, fetch and apply below would write to the
# repository that GIT_DIR names.  Clear each variable, then stop unless git
# resolves the new repository itself.
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY \
      GIT_ALTERNATE_OBJECT_DIRECTORIES GIT_COMMON_DIR GIT_NAMESPACE GIT_CEILING_DIRECTORIES
git init -q "$src"
[[ $(realpath -- "$(git -C "$src" rev-parse --absolute-git-dir)") == $(realpath -- "$src/.git") ]] \
    || die "git resolves a repository other than $src/.git"
git -C "$src" fetch -q --depth 1 "$url" "$commit"
[[ $(git -C "$src" rev-parse FETCH_HEAD) == "$commit" ]] || die "the download is not commit $commit"
git -C "$src" archive FETCH_HEAD | tar -x -C "$src"

for patch in "${patches[@]}"; do
    printf 'build.sh: apply %s\n' "${patch##*/}"
    git -C "$src" apply "$patch"
done

mkdir "$obj"
cd "$obj"
"$src/configure" \
    --prefix="$prefix/usr" \
    --program-suffix=-16p \
    --enable-languages=c,c++,lto \
    --disable-multilib \
    --disable-bootstrap \
    --enable-checking=release \
    --with-system-zlib
make -j"$jobs"
# install-strip removes the debug information from the compiler binaries.
# cc1plus then goes from approximately 410 MB to 46 MB, and the install from
# 1.9 GB to 254 MB, so the CI cache of the install stays small.
make install-strip

"$prefix/usr/bin/g++-16p" -v
printf 'build.sh: installed %s\n' "$prefix/usr/bin/g++-16p"
