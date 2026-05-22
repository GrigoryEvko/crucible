// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-220 HS14 fixture #1 of 2 for
// fixy::contract::MemberMintCtxRequired: WRONG-CTX rejection — a
// registered (Class, MintName) pair whose `admits<Ctx>()` predicate
// returns FALSE must reject via the concept's requires-expression.
//
// Violation: `MemberMintCtxRequired<Cipher, mint_name::open_view, Ctx>`
// is satisfied iff
//
//     member_mint_required_ctx<Cipher, open_view>::admits<Ctx>()
//
// returns true.  The Cipher::mint_open_view specialization admits
// only `IsBgCtx<Ctx>` (the OpenView mediates writes from
// BackgroundThread::run / ::flush; calling from the FG dispatch hot
// path would race the FG-owned MetaLog tail).  Passing HotFgCtx
// here MUST red the concept at the static_assert.
//
// Distinct from fixture #2 (unregistered-pair rejection):
//   * Fixture #1 — REGISTERED pair, WRONG ctx.  The spec exists; its
//     `admits<Ctx>()` predicate returns false; the requires-expression
//     evaluates `requires false`; concept = unsatisfied.
//   * Fixture #2 — UNREGISTERED pair (e.g. `int` as Class, or a
//     mint_name tag the host class does not declare).  The primary
//     template `member_mint_required_ctx<Class, MintName>` is
//     incomplete for the pair, so the inner `::template admits<Ctx>()`
//     substitution is ill-formed; requires-expression SFINAE-rejects;
//     concept = unsatisfied.
// Two distinct rejection axes ⇒ HS14 floor satisfied.
//
// Background (Agent 8 Part 10 #13).  Per CLAUDE.md §XXI, Crucible's 8
// member-function mints (mint_* methods on host classes) CANNOT be
// re-exported through namespace using-declarations (member functions
// are not namespace-scope entities).  V-220 closes the discipline gap
// with a constexpr registration table mapping each (Class, MintName)
// pair to a `consteval bool admits<Ctx>()` predicate; production call
// sites opt in via `static_assert(MemberMintCtxRequired<...>)` above
// the mint invocation.  This fixture pins the WRONG-CTX axis — a call
// site that drops the static_assert with the wrong ctx tier reds at
// compile time rather than racing in production.
//
// Expected diagnostic: static assertion failed mentioning
// MemberMintCtxRequired / Cipher / open_view (the concept is
// unsatisfied because IsBgCtx<HotFgCtx> = false).

#include <crucible/fixy/Contract.h>

int main() {
    // HotFgCtx is the canonical Fg hot-path ctx (Fg cap, L1 resident,
    // Terminating).  Cipher::mint_open_view requires IsBgCtx — the
    // OpenView writes must come from BackgroundThread, not FG.  The
    // concept evaluates `member_mint_required_ctx<Cipher,
    // open_view>::admits<HotFgCtx>() == false`, the requires-clause
    // fails, the concept rejects, the static_assert fires.  If the
    // concept ever drifted to admit FG ctxs (e.g. via a stale
    // `IsExecCtx<Ctx>` clause that always returns true), this fixture
    // would silently compile and a FG dispatch site could install a
    // Cipher write from the hot path.
    static_assert(::crucible::fixy::contract::MemberMintCtxRequired<
                      ::crucible::Cipher,
                      ::crucible::fixy::contract::mint_name::open_view,
                      ::crucible::effects::HotFgCtx>,
        "FIXY-V-220 fixture #1: Cipher::mint_open_view requires IsBgCtx — "
        "MemberMintCtxRequired must reject HotFgCtx via admits<Ctx> = false.");
    return 0;
}
