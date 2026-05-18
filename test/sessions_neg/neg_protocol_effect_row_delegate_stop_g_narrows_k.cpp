// SPDX-License-Identifier: Apache-2.0
//
// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-028 — HS14 fixture #1.  `protocol_effect_row<Delegate<
// Stop_g<C>, K>>` MUST yield `Row<>` (bottom-preservation), agreeing
// with `compose<Delegate<Stop_g<C>, K>, Q> -> Stop_g<C>` at
// SessionDelegate.h:711-713.  Pre-fix the trait inherited from the
// general `protocol_effect_row<Delegate<T, K>>` rule
// (SessionRowExtraction.h:488-490) and walked K — surfacing K's
// effect row even though the carrier is bottom and K is unreachable.
// Post-fix the new bottom-preservation specialization fires and the
// trait reports `Row<>`, MATCHING the compose-side semantics and
// closing the row-narrowing-under-composition soundness hole that
// fixy-A2-028 documents.
//
// This fixture witnesses the BUGGY pre-fix shape:
//
//   protocol_effect_row_t<Delegate<Stop_g<Abort>,
//                                  Recv<Computation<Row<Bg>, int>, End>>>
//     == Row<Bg>  (pre-fix: walks K, reports K's Bg row)
//     == Row<>    (post-fix: bottom-preservation, K unreachable)
//
// Asserting the pre-fix equality to `Row<Bg>` MUST fail after the
// post-fix bottom-preservation specialization fires.  The trait now
// agrees with `protocol_effect_row<compose_t<...>>` = `Row<>` — no
// row narrowing across the composition boundary.
//
// Companion to `neg_protocol_effect_row_accept_stop_g_narrows_k`:
//   * THIS fixture covers `protocol_effect_row<Delegate<Stop_g<C>, K>>`.
//   * The companion covers `protocol_effect_row<Accept<Stop_g<C>, K>>`.
// Together the pair pins fixy-A2-028's promise that BOTH arms of
// delegation-of-crashed-channel report empty rows uniformly with
// compose's bottom-preservation rule (fixy-A2-002 + GAPS-048).
//
// Why this matters (Met(X) row-admission soundness):
//   `protocol_effect_row<P>` drives `CtxFitsProtocol`'s admission
//   check in `mint_permissioned_session`.  A user composing
//   `Delegate<Stop_g<C>, Recv<Bg-bearing payload>>` with anything
//   should produce `Stop_g<C>` — a bottom carrier whose effect row
//   is empty.  Pre-fix, the standalone protocol's row claimed Bg
//   admission; post-compose the row is empty.  The asymmetry
//   between pre- and post-composition rows IS the row-narrowing
//   soundness hole the framework's Met(X) row-admission discipline
//   explicitly disallows.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "static assertion failed" / "static assertion" / "static_assert".

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/Computation.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionDelegate.h>
#include <crucible/sessions/SessionRowExtraction.h>

#include <type_traits>

namespace eff   = crucible::effects;
namespace proto = crucible::safety::proto;

namespace {

using InnerBgRecv =
    proto::Recv<eff::Computation<eff::Row<eff::Effect::Bg>, int>, proto::End>;

using DelegateStopWithBg =
    proto::Delegate<proto::Stop_g<proto::CrashClass::Abort>, InnerBgRecv>;

// Pre-fix witness: the general `protocol_effect_row<Delegate<T, K>>`
// rule fires for T = Stop_g<Abort> and the trait inherits K's row.
// Asserting equivalence to that shape MUST fail post-fixy-A2-028 —
// the new bottom-preservation specialization fires and the trait
// reports `Row<>`.  This fixture's failure-to-compile post-fix IS
// the witness that the row-narrowing hole is closed.
static_assert(std::is_same_v<
    proto::protocol_effect_row_t<DelegateStopWithBg>,
    eff::Row<eff::Effect::Bg>>,
    "fixy-A2-028 regression: Delegate<Stop_g<C>, K> protocol_effect_row "
    "did not collapse to Row<> under bottom-preservation");

}  // namespace

int main() { return 0; }
