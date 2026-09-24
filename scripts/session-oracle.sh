#!/usr/bin/env bash
# session-oracle.sh — differential tests of the session relations against
# published mechanisations.
#
# The relations in include/fixy/session and in the frozen
# include/crucible/sessions/SessionGlobal.h are decision procedures.  The
# comparison in tools/session_oracle/ gives each one a reference that
# does not share its code:
#
#   projection         the computable projection of Tirore, Bengtson and
#                      Carbone (ITP 2023), github.com/Tirore96/projection.
#                      No licence.
#   subject reduction  the labelled projection and the linearity check of
#                      Tirore, Bengtson and Carbone (ECOOP 2025), branch
#                      ECOOP2025 of github.com/Tirore96/subject_reduction.
#                      MIT licence.
#   subtyping          the coinductive relation subtypeC of Ekici (ITP
#                      2025), github.com/Apiros3/smpst-sr-smer.  No
#                      licence.  coqc checks a proof or a refutation of
#                      each pair against the development's own definition.
#   liveness           the theorem liveness of Keskin, Yoshida and van
#                      Glabbeek (ITP 2026), github.com/omerskeskin/mpstlive.
#                      No licence.  coqc applies it to fixy's projected
#                      context of each multiparty case, with a certificate
#                      for its four premises (tools/session_oracle/coq).
#   implementability   Sprout(A) of Li and Wies (PLDI 2026), built natively
#                      from the pinned sources that
#                      tools/session_oracle/sprout.py names (artifact doi
#                      10.5281/zenodo.19600644, CC BY 4.0).  It decides
#                      implementability on per-pair FIFO queues, a mailbox
#                      and a bag for each receiver.  Only the verdict of the
#                      naive mode counts as evidence.  The opt mode names
#                      each query file by four state numbers with no
#                      separator, so one query can replace another, and
#                      its verdict is kept for information only.
#   crash-stop         mpstk-crash-stop of Barwell, Scalas, Yoshida and
#                      Zhou (CONCUR 2022), github.com/alcestes/mpstk-crash-stop.
#                      MIT licence.  It model-checks the context of each
#                      crash-stop projection with mCRL2.
#   runs               a breadth-first run of each typing context in
#                      tools/session_oracle/execution.py, with the label
#                      word of a keyed choice on the wire.
#
# Each oracle downloads a pinned commit as a tarball into
# ~/.cache/crucible/session_oracle and builds it there.  SESSION_ORACLE_CACHE
# changes the directory.  No part of a repository goes into this tree.
# Only our code and the verdicts go into it.  The verdicts are in
# test/session_oracle/golden.csv, with the commit of each oracle and the
# commit of the measured headers.  The C++ tests that assert them are
# emitted from that file.
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
#   --regenerate   Run the oracles and the probes, shrink every divergence,
#                  and write golden.csv and the emitted tests.  Give the
#                  compiler as the second argument.  CI does not run it.
#   --derive       Compute the liveness and implementability rows again from
#                  the multiparty rows of golden.csv, and write golden.csv.
#                  This mode needs those two toolchains only.  With the
#                  compiler as the second argument, it also measures the
#                  transition systems of Semantics.h again, at the commit
#                  that golden.csv names, and implementable_on of
#                  fixy/session/Network.h on each network, at HEAD.  CI
#                  does not run it.
#
# --self-test and --regenerate take a commit as an optional third
# argument, for example HEAD.  The tool then measures the include tree of
# that commit, which it takes with git archive into a temporary directory,
# and golden.csv names the full commit.  Without it, the tool measures the
# working tree and names HEAD and any edits in the session headers.  Give
# a commit when the working tree holds edits that do not compile.
#
# How to install the toolchains.  Do this one time, in your home
# directory.  Nothing goes into the repository.
#
#   1. The projection and the subject-reduction oracles need Coq 8.15.
#      Install opam, then make a switch with OCaml 4.14, pin Coq before
#      you install a library, and install the libraries:
#        opam switch create sessoracle ocaml-base-compiler.4.14.2
#        opam pin add -n --switch=sessoracle coq 8.15.2
#        opam repo add --switch=sessoracle coq-released https://coq.inria.fr/opam/released
#        opam install --switch=sessoracle coq coq-mathcomp-ssreflect.1.17.0 \
#            coq-equations coq-paco coq-deriving coq-mathcomp-zify
#      A later Coq cannot build the two pinned commits, and coq-deriving
#      installs the latest Coq when Coq is not pinned.
#   2. The subtyping oracle needs Coq 8.20 with mathcomp-ssreflect 2 and
#      paco.  Make a second switch:
#        opam switch create sessoracle20 ocaml-base-compiler.4.14.2
#        opam pin add -n --switch=sessoracle20 coq 8.20.1
#        opam repo add --switch=sessoracle20 coq-released https://coq.inria.fr/opam/released
#        opam install --switch=sessoracle20 coq coq-mathcomp-ssreflect.2.3.0 coq-paco
#      SESSION_ORACLE_COQC20 names a different coqc command.
#   3. The liveness oracle needs Coq 8.20.1 with mathcomp-ssreflect 2.5,
#      paco, mmaps and equations.  Make a third switch:
#        opam switch create sessoracle25 ocaml-base-compiler.4.14.2
#        opam pin add -n --switch=sessoracle25 coq 8.20.1
#        opam repo add --switch=sessoracle25 coq-released https://coq.inria.fr/opam/released
#        opam install --switch=sessoracle25 coq coq-mathcomp-ssreflect.2.5.0 coq-paco \
#            coq-mmaps coq-equations
#      The first run builds the development, which takes about ten
#      minutes.  SESSION_ORACLE_KESKIN_SWITCH names a different switch.
#   4. The implementability oracle builds Sprout(A) and its solver MuVal
#      natively, from the pinned sources that tools/session_oracle/sprout.py
#      names.  It needs a switch with OCaml 5.2.1 and the package versions
#      of the artifact's MuVal build, which the debug information of the
#      artifact's MuVal binary names:
#        opam switch create sprout ocaml-base-compiler.5.2.1 --no-switch
#        opam pin add -n --switch=sprout libsvm.0.10.0~modified \
#            git+https://github.com/hiroshi-unno/libsvm-ocaml.git#5d3b175220f865e1ce60909bef11b7e2773451d8
#        opam install --switch=sprout dune.3.18.2 menhir.20240715 \
#            base.v0.17.1 core.v0.17.1 core_kernel.v0.17.0 core_unix.v0.17.0 \
#            ppx_deriving.6.0.3 ppx_deriving_yojson.3.9.1 yojson.2.2.2 ppx_expect.v0.17.2 \
#            ocaml-compiler-libs.v0.17.0 ocamlgraph.2.1.0 zarith.1.14 z3.4.14.1 \
#            minisat.0.6 camlzip.1.13 lacaml.11.1.1 libsvm.0.10.0~modified \
#            domainslib.0.5.0 num.1.5-1 logs.0.8.0 fmt.0.10.0 stdio.v0.17.0 \
#            sexplib.v0.17.0 ppx_custom_printf.v0.17.0 ppx_compare.v0.17.0 \
#            ppx_hash.v0.17.0 ppx_sexp_conv.v0.17.0
#      The system needs the development files of gmp, mpfr, BLAS, LAPACK
#      and zstd, and a static zlib.  On Fedora, install gmp-devel,
#      mpfr-devel, blas-devel, lapack-devel, libzstd-devel and
#      zlib-ng-compat-static.  The first run downloads and builds MuVal
#      and the front end in the cache, which takes less than one minute.
#      SESSION_ORACLE_SPROUT_SWITCH names a different switch.
#   5. The crash-stop oracle needs a Java 17 runtime, sbt, and the mCRL2
#      tools mcrl22lps, lps2pbes and pbes2bool.  Install nothing by hand:
#      tools/session_oracle/toolchain.py downloads a pinned release of each
#      into the cache (Temurin JDK 17.0.20.1+1, sbt 1.10.7, mCRL2
#      202607.0), checks its SHA-256, and unpacks it there.  The build of
#      the pinned commit then asks sbt for sbt 1.6.1 and downloads it into
#      the same cache.  The host needs rpm2cpio and cpio, which unpack the
#      mCRL2 package.  To install ahead of a run:
#        python3 tools/session_oracle/session_oracle.py install-toolchain
#
# How to regenerate after a relation changes:
#
#   scripts/session-oracle.sh --regenerate "$HOME/.local/gcc16-patched/usr/bin/g++-16p" HEAD
#
# The script activates the opam switch "sessoracle" when opam is on PATH or
# in ~/.local/bin.  The subtyping oracle runs its own switch.  A run takes
# some minutes, because the shrinker makes every divergence as small as
# possible.  Each oracle keeps its answers in a cache whose file name
# carries its commit, so a second run asks only the new queries.  Read the
# diff of golden.csv before you commit it: a row that changes from
# divergence to agree is a repair, and a row that changes the other way is
# a regression.
#
# Exit status:
#   0 — success
#   1 — drift, a failed test, or a failed self-test
#   2 — bad invocation or a missing dependency

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
driver="$root/tools/session_oracle/session_oracle.py"
# The oracle writes no bytecode into the source tree.
export PYTHONDONTWRITEBYTECODE=1

usage() {
    cat >&2 <<'USAGE'
session-oracle.sh — differential tests of the session relations.

Usage:
  session-oracle.sh --check
  session-oracle.sh --self-test CXX [COMMIT]
  session-oracle.sh --emit
  session-oracle.sh --regenerate CXX [COMMIT]
  session-oracle.sh --derive [CXX]
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

# Print the --at option for an optional commit, or nothing.
at_option() {
    if [[ $# -lt 1 || -z "$1" ]]; then
        return 0
    fi
    if ! git -C "$root" rev-parse --verify --quiet "$1^{commit}" >/dev/null; then
        echo "session-oracle.sh: '$1' names no commit of this repository." >&2
        exit 2
    fi
    printf '%s\n' "--at=$1"
}

case "${1:-}" in
    --check)
        exec python3 "$driver" check ;;
    --emit)
        exec python3 "$driver" emit ;;
    --self-test)
        shift
        need_cxx "${1:-}"
        at="$(at_option "${2:-}")" || exit 2
        exec python3 "$driver" self-test --cxx "$1" ${at:+"$at"} ;;
    --regenerate)
        shift
        need_cxx "${1:-}"
        at="$(at_option "${2:-}")" || exit 2
        activate_opam
        exec python3 "$driver" regenerate --cxx "$1" ${at:+"$at"} ;;
    --derive)
        shift
        if [[ -n "${1:-}" ]]; then
            need_cxx "$1"
            exec python3 "$driver" derive --cxx "$1"
        fi
        exec python3 "$driver" derive ;;
    -h|--help)
        usage
        exit 0 ;;
    *)
        usage
        exit 2 ;;
esac
