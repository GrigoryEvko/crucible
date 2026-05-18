// SPDX-License-Identifier: Apache-2.0
//
// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-002 — HS14 fixture: `compose<EpochedAccept<Stop_g<C>, K, E, G>, Q>`
// MUST collapse to `Stop_g<C>` symmetric to the EpochedDelegate-side
// bottom-preservation at SessionDelegate.h:665-669.  Pre-fix the
// general `compose<EpochedAccept<T, K, E, G>, Q>` specialization fired
// even when T was `Stop_g<C>`, producing the structurally wrong
// `EpochedAccept<Stop_g<C>, compose<K, Q>, E, G>` — semantically
// claiming the recipient still advances K AND preserves the epoch
// discipline past an already-crashed delegated endpoint.  Post-fix the
// new specialization
// `compose<EpochedAccept<Stop_g<C>, K, E, G>, Q> -> Stop_g<C>` fires;
// the buggy `EpochedAccept<Stop_g<C>, ...>` shape disappears and the
// epoch/generation NTTPs are correctly dropped (a crashed channel has
// no epoch discipline left to enforce).
//
// This fixture witnesses the BUGGY pre-fix shape for
// `EpochedAccept<Stop_g<CrashClass::Throw>, K, 5, 3>`:
//
//     static_assert(std::is_same_v<
//         compose_t<EpochedAccept<Stop_g<Throw>, Send<int, End>, 5, 3>,
//                   Recv<Ack, End>>,
//         EpochedAccept<Stop_g<Throw>,
//                       compose_t<Send<int, End>, Recv<Ack, End>>,
//                       5, 3>>);
//
// Pre-fix the assertion holds.  Post-fix the bottom-preservation rule
// wins partial-ordering, produces `Stop_g<Throw>`, and the
// static_assert fires — the file no longer compiles.
//
// Companion to `neg_compose_accept_stop_g_missing_bottom.cpp`:
//   * That fixture covers `compose<Accept<Stop_g<C>, K>, Q>`.
//   * THIS fixture covers
//     `compose<EpochedAccept<Stop_g<C>, K, MinEpoch, MinGen>, Q>`.
// Together the pair pins fixy-A2-002's promise that BOTH Accept
// shapes collapse to Stop_g<C> under composition — restoring the
// duality identity end-to-end on the Epoched path used by
// Cipher reshard discipline (GAPS-072) where MinEpoch/MinGen NTTPs
// are load-bearing.
//
// Why this matters specifically for Epoched* (BSYZ22 + Cipher
// reshard): the EpochedDelegate combinator threads epoch and
// generation NTTPs through delegation so that Cipher reshards (which
// bump generation) and federation membership changes (which bump
// epoch) both refuse delegated channels that pre-date the change.
// Without the symmetric Accept-side bottom rule, the receiver-side of
// a delegated-and-then-crashed channel would silently preserve stale
// MinEpoch/MinGen NTTPs through composition — a phantom epoch
// discipline claim on a channel that has none.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "static assertion failed" / "static assertion" / "static_assert".

#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionDelegate.h>

#include <type_traits>

namespace proto = crucible::safety::proto;

namespace {

struct Ack {};

// Pre-fix witness: the general `compose<EpochedAccept<T, K, E, G>, Q>`
// rule fires for T = Stop_g<Throw>, producing the structurally wrong
// `EpochedAccept<Stop_g<Throw>, Send<int, Recv<Ack, End>>, 5, 3>`.
// Asserting equivalence to that shape MUST fail post-fixy-A2-002 —
// the new bottom-preservation specialization fires, the
// MinEpoch/MinGen NTTPs are correctly dropped, and the type collapses
// to `Stop_g<Throw>`.
static_assert(std::is_same_v<
    proto::compose_t<
        proto::EpochedAccept<proto::Stop_g<proto::CrashClass::Throw>,
                             proto::Send<int, proto::End>,
                             /*MinEpoch=*/5u,
                             /*MinGeneration=*/3u>,
        proto::Recv<Ack, proto::End>>,
    proto::EpochedAccept<proto::Stop_g<proto::CrashClass::Throw>,
                         proto::compose_t<proto::Send<int, proto::End>,
                                          proto::Recv<Ack, proto::End>>,
                         /*MinEpoch=*/5u,
                         /*MinGeneration=*/3u>>,
    "fixy-A2-002 regression: EpochedAccept<Stop_g<C>, K, E, G> compose "
    "did not collapse to Stop_g<C>");

}  // namespace

int main() { return 0; }
