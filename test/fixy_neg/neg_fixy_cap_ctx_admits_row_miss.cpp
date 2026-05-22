// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-217 HS14 fixture #2 of 2 for fixy::cap::CtxAdmitsCap:
// row-membership rejection — a well-formed ExecCtx whose
// row_type does not claim the requested Effect must reject via
// the row_contains_v clause of CtxAdmitsCap.
//
// Violation: `CtxAdmitsCap<Ctx, Cap>` requires the Effect to be
// an atom in the Ctx's CLAIMED row (Ctx::row_type), not just in
// the cap-source's PERMITTED row.  HotFgCtx is the canonical
// alias for the Vessel-side dispatch context:
//
//     HotFgCtx = ExecCtx<
//         ctx_cap::Fg, ctx_numa::Local, ctx_alloc::Stack,
//         ctx_heat::Hot, ctx_resid::L1, Row<>,
//         ctx_workload::Unspecified, ctx_progress::Terminating>
//
// Its row_type is `Row<>` (empty) — the foreground recording
// path makes ZERO effect claims.  Any non-empty Effect must be
// rejected by the row-axis check: even Alloc, which is the most
// permissive value-effect tag, is NOT claimed.  Fg's
// cap_permitted_row is also empty (`Row<>`), so neither
// CtxAdmitsCap nor CtxAdmitsCapStrict can ever accept on this
// ctx.
//
// This is the row-axis check working correctly: a band-3 site
// that claimed `requires CtxAdmitsCap<HotFgCtx, Effect::Alloc>`
// would compile-error here, forcing the call site to either
// (a) promote the row before the call (`ctx.in_row<Alloc>()`),
// (b) take a different Ctx (BgDrainCtx), or
// (c) reconsider the design (foreground dispatch should never
//     need an Alloc-typed primitive on the hot path — the
//     memory plan is pre-baked).
//
// Distinct from fixture #1 (IsExecCtx structural rejection):
//   * Fixture #1 — STRUCTURAL rejection (`int` is not an
//     ExecCtx<...> at all; IsExecCtx clause rejects).
//   * Fixture #2 — SEMANTIC rejection (HotFgCtx IS a valid
//     ExecCtx<...>, but its row_type = Row<> doesn't contain
//     Alloc; row_contains_v clause rejects).
// Two distinct rejection axes ⇒ HS14 floor satisfied.
//
// Expected diagnostic: static assertion failed mentioning
// CtxAdmitsCap (concept unsatisfied because Effect::Alloc is
// not in HotFgCtx::row_type = Row<>).

#include <crucible/fixy/Cap.h>

int main() {
    // HotFgCtx's row_type = Row<> (empty).  Effect::Alloc is the
    // most permissive value-effect tag; if even Alloc is rejected,
    // every other Effect is also rejected.  This pins the row-axis
    // check: the concept enforces what the ctx CLAIMS in its row,
    // independent of what the cap-source COULD permit.  If the
    // concept ever drifted to use cap_permitted_row (the
    // CtxCanMint axis) instead of row_type (the CtxAdmitsCap
    // axis), this fixture would still red on HotFgCtx because
    // cap_permitted_row<ctx_cap::Fg> is ALSO Row<> — defense-in-
    // depth across both axes.
    static_assert(::crucible::fixy::cap::CtxAdmitsCap<
                      ::crucible::effects::HotFgCtx,
                      ::crucible::effects::Effect::Alloc>,
        "FIXY-V-217 fixture #2: HotFgCtx::row = Row<> (empty) — "
        "CtxAdmitsCap must reject every Effect via row_contains_v.");
    return 0;
}
