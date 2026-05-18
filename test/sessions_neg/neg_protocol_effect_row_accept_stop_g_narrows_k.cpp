// SPDX-License-Identifier: Apache-2.0
//
// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-028 — HS14 fixture #2.  `protocol_effect_row<Accept<
// Stop_g<C>, K>>` MUST yield `Row<>` (bottom-preservation),
// symmetric to the Delegate-side rule (fixture #1) and aligned with
// `compose<Accept<Stop_g<C>, K>, Q> -> Stop_g<C>` at
// SessionDelegate.h:731-734 (fixy-A2-002).  Pre-fix the trait
// inherited from the general `protocol_effect_row<Accept<T, K>>`
// rule (SessionRowExtraction.h:493-495) and walked K — surfacing
// K's effect row even though the recipient accepted an already-
// crashed delegated endpoint and K is unreachable.  Post-fix the
// new bottom-preservation specialization fires and the trait
// reports `Row<>`, matching the compose-side semantics.
//
// This fixture witnesses the BUGGY pre-fix shape on the Accept arm:
//
//   protocol_effect_row_t<Accept<Stop_g<Throw>,
//                                Recv<Computation<Row<IO>, int>, End>>>
//     == Row<IO>  (pre-fix: walks K, reports K's IO row)
//     == Row<>    (post-fix: bottom-preservation, K unreachable)
//
// Asserting the pre-fix equality to `Row<IO>` MUST fail after the
// post-fix bottom-preservation specialization fires.  Together with
// `neg_protocol_effect_row_delegate_stop_g_narrows_k`, the pair
// pins fixy-A2-028's promise that BOTH delegation arms (sender
// `Delegate<Stop_g<C>, K>` AND receiver `Accept<Stop_g<C>, K>`)
// report empty rows uniformly with compose's bottom-preservation
// rule.  Different CrashClass (Abort/Throw) chosen on each fixture
// to pin that the bottom-preservation rule is CrashClass-agnostic.
//
// Why this matters (duality identity):
//   `dual_of(Delegate<T, K>) = Accept<T, dual(K))` per
//   SessionDelegate.h:590.  A row-extraction asymmetry between
//   the Delegate and Accept arms would break the duality round-
//   trip's row-preservation: a protocol that admitted Bg on the
//   sender side would silently morph to admitting nothing on the
//   receiver side, defeating the bidirectional Met(X) admission
//   discipline.  This fixture pins the symmetry by witnessing the
//   pre-fix asymmetry post-fix-impossibility.
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

using InnerIoRecv =
    proto::Recv<eff::Computation<eff::Row<eff::Effect::IO>, int>, proto::End>;

using AcceptStopWithIo =
    proto::Accept<proto::Stop_g<proto::CrashClass::Throw>, InnerIoRecv>;

// Pre-fix witness: the general `protocol_effect_row<Accept<T, K>>`
// rule fires for T = Stop_g<Throw> and the trait inherits K's row.
// Asserting equivalence to that shape MUST fail post-fixy-A2-028 —
// the new bottom-preservation specialization fires and the trait
// reports `Row<>`.  This fixture's failure-to-compile post-fix IS
// the witness that the symmetric duality-side soundness hole is
// closed.
static_assert(std::is_same_v<
    proto::protocol_effect_row_t<AcceptStopWithIo>,
    eff::Row<eff::Effect::IO>>,
    "fixy-A2-028 regression: Accept<Stop_g<C>, K> protocol_effect_row "
    "did not collapse to Row<> under bottom-preservation");

}  // namespace

int main() { return 0; }
