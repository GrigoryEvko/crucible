#!/usr/bin/env bash
# session-oracle.sh — differential tests of the session relations against
# a published mechanisation.
#
# The relations in include/fixy/session/Protocol.h and in the frozen
# include/crucible/sessions/SessionGlobal.h are decision procedures.  This
# script compares their answers with the computable projection of
# Tirore, Bengtson and Carbone (ITP 2023).  The authors proved it sound
# and complete in Coq.  The comparison is in tools/session_oracle/.  Its
# results are in test/session_oracle/golden.csv, and the C++ tests that
# assert them are emitted from that file.
#
# Modes:
#
#   --check        Emit the tests from golden.csv in memory and compare
#                  them with the committed tests.  This mode needs no
#                  oracle and no compiler.  CI runs it.
#   --self-test    Compile the emitted tests, which must pass.  Then
#                  compile a copy with one wrong row planted in each
#                  test, which must fail and name the planted case.
#                  Then make sure that the drift check sees a one-line
#                  change.  Give the compiler as the second argument.
#   --emit         Write the emitted tests from golden.csv.  Use it after
#                  a note in golden.csv changes.
#   --regenerate   Run the oracle and the probes, shrink every divergence,
#                  and write golden.csv and the emitted tests.  Give the
#                  compiler as the second argument.  CI does not run it.
#
# How to install the oracle toolchain.  Do this one time, in your home
# directory.  Nothing goes into the repository.
#
#   1. Install opam, then make a switch with OCaml 4.14:
#        opam switch create sessoracle ocaml-base-compiler.4.14.2
#   2. Pin Coq to 8.15.2 before you install a library.  A later Coq
#      cannot build the pinned oracle, and coq-deriving installs the
#      latest Coq when Coq is not pinned:
#        opam pin add -n --switch=sessoracle coq 8.15.2
#   3. Add the Coq repository and install the libraries:
#        opam repo add --switch=sessoracle coq-released https://coq.inria.fr/opam/released
#        opam install --switch=sessoracle coq coq-mathcomp-ssreflect.1.17.0 \
#            coq-equations coq-paco coq-deriving coq-mathcomp-zify
#
# How to download the oracle.  --regenerate does it.  It downloads the pinned
# commit of github.com/Tirore96/projection as a tarball into
# ~/.cache/crucible/session_oracle, and it compiles the development there.
# SESSION_ORACLE_CACHE changes the directory.  The repository has no
# licence, so no part of it goes into this tree.  Only our code and the
# verdicts go into the tree.
#
# How to regenerate after a relation changes:
#
#   scripts/session-oracle.sh --regenerate "$HOME/.local/gcc16-patched/usr/bin/g++-16p"
#
# The script activates the opam switch "sessoracle" when opam is on PATH or
# in ~/.local/bin.  A run takes some minutes, because the shrinker makes
# every divergence as small as possible.  Read the diff of golden.csv
# before you commit it: a row that changes from divergence to agree is a
# repair, and a row that changes the other way is a regression.
#
# Exit status:
#   0 — success
#   1 — drift, a failed test, or a failed self-test
#   2 — bad invocation or a missing dependency

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
driver="$root/tools/session_oracle/session_oracle.py"

usage() {
    cat >&2 <<'USAGE'
session-oracle.sh — differential tests of the session relations.

Usage:
  session-oracle.sh --check
  session-oracle.sh --self-test CXX
  session-oracle.sh --emit
  session-oracle.sh --regenerate CXX
  session-oracle.sh -h | --help
USAGE
}

if ! command -v python3 >/dev/null 2>&1; then
    echo "session-oracle.sh: python3 is not on PATH.  Install Python 3.11 or a later version." >&2
    exit 2
fi

activate_opam() {
    local opam
    opam="$(command -v opam || true)"
    if [[ -z "$opam" && -x "$HOME/.local/bin/opam" ]]; then
        opam="$HOME/.local/bin/opam"
    fi
    if [[ -n "$opam" ]] && "$opam" switch list --short 2>/dev/null | grep -qx sessoracle; then
        eval "$("$opam" env --switch=sessoracle)"
    fi
}

need_cxx() {
    if [[ $# -lt 1 || -z "$1" ]]; then
        echo "session-oracle.sh: give the project compiler as the second argument." >&2
        usage
        exit 2
    fi
    if [[ ! -x "$1" ]] && ! command -v "$1" >/dev/null 2>&1; then
        echo "session-oracle.sh: the compiler '$1' is not an executable." >&2
        exit 2
    fi
}

case "${1:-}" in
    --check)
        exec python3 "$driver" check ;;
    --emit)
        exec python3 "$driver" emit ;;
    --self-test)
        shift
        need_cxx "${1:-}"
        exec python3 "$driver" self-test --cxx "$1" ;;
    --regenerate)
        shift
        need_cxx "${1:-}"
        activate_opam
        exec python3 "$driver" regenerate --cxx "$1" ;;
    -h|--help)
        usage
        exit 0 ;;
    *)
        usage
        exit 2 ;;
esac
