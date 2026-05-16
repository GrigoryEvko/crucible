// SPDX-License-Identifier: Apache-2.0
//
// test/test_fixy_aliases.cpp
//
// FIXY-C Phase C sentinel TU.  Verifies the three alias headers
// (Sess.h, Mach.h, Contract.h) compile clean AND produce identity-
// equivalent types over the substrate.  A regression in any
// re-export surfaces here at compile time.
//
// Acceptance per misc/16_05_2026_fixy.md §4 Phase C:
//   - fixy::sess::* aliases identical to safety::proto::*
//   - fixy::mach::* aliases identical to safety::*
//   - fixy::contract::CRUCIBLE_PRE / CRUCIBLE_POST macros expand
//     identically when included via fixy/Contract.h
//
// Phase C does NOT promise full SessionPatterns compatibility under
// `using namespace fixy::sess;` — that's a Phase C+ verification
// run.  This file pins the load-bearing identity properties.

#include <crucible/fixy/Contract.h>
#include <crucible/fixy/Mach.h>
#include <crucible/fixy/Sess.h>

#include <crucible/safety/Machine.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionDelegate.h>

#include <cstdio>
#include <type_traits>
#include <utility>

namespace {

namespace cs = crucible::fixy::sess;
namespace cm = crucible::fixy::mach;
namespace sp = crucible::safety::proto;
namespace ss = crucible::safety;

// ─── 1. fixy::sess — core combinators are identity aliases ────────────
//
// Every fixy::sess::X is the safety::proto::X by template-name
// equality.  std::is_same_v fires only if the alias chain breaks.

using Sess_Send  = cs::Send<int, cs::End>;
using Sub_Send   = sp::Send<int, sp::End>;
static_assert(std::is_same_v<Sess_Send, Sub_Send>);

using Sess_Recv  = cs::Recv<float, cs::End>;
using Sub_Recv   = sp::Recv<float, sp::End>;
static_assert(std::is_same_v<Sess_Recv, Sub_Recv>);

using Sess_Loop  = cs::Loop<cs::End>;
using Sub_Loop   = sp::Loop<sp::End>;
static_assert(std::is_same_v<Sess_Loop, Sub_Loop>);

using Sess_Select = cs::Select<cs::Send<int, cs::End>, cs::Recv<int, cs::End>>;
using Sub_Select  = sp::Select<sp::Send<int, sp::End>, sp::Recv<int, sp::End>>;
static_assert(std::is_same_v<Sess_Select, Sub_Select>);

using Sess_Offer = cs::Offer<cs::Send<int, cs::End>, cs::Recv<int, cs::End>>;
using Sub_Offer  = sp::Offer<sp::Send<int, sp::End>, sp::Recv<int, sp::End>>;
static_assert(std::is_same_v<Sess_Offer, Sub_Offer>);

// Terminal states (Continue, End, Stop) round-trip as non-templated
// types.
static_assert(std::is_same_v<cs::Continue, sp::Continue>);
static_assert(std::is_same_v<cs::End,      sp::End>);
static_assert(std::is_same_v<cs::Stop,     sp::Stop>);

// Stop_g<CrashClass> family
static_assert(std::is_same_v<cs::Stop_g<cs::CrashClass::Abort>,
                             sp::Stop_g<sp::CrashClass::Abort>>);
static_assert(std::is_same_v<cs::Stop_g<cs::CrashClass::Abort>, cs::Stop>);

// Delegate / Accept (higher-order session delegation)
using Sess_Delegate = cs::Delegate<cs::Send<int, cs::End>, cs::End>;
using Sub_Delegate  = sp::Delegate<sp::Send<int, sp::End>, sp::End>;
static_assert(std::is_same_v<Sess_Delegate, Sub_Delegate>);

using Sess_Accept = cs::Accept<cs::Send<int, cs::End>, cs::End>;
using Sub_Accept  = sp::Accept<sp::Send<int, sp::End>, sp::End>;
static_assert(std::is_same_v<Sess_Accept, Sub_Accept>);

// CheckpointedSession (re-entrant cancel + replay)
using Sess_Ckpt = cs::CheckpointedSession<cs::Send<int, cs::End>, cs::End>;
using Sub_Ckpt  = sp::CheckpointedSession<sp::Send<int, sp::End>, sp::End>;
static_assert(std::is_same_v<Sess_Ckpt, Sub_Ckpt>);

// EpochedDelegate (epoch-aware delegation, Phase C re-export)
using Sess_Epoch = cs::EpochedDelegate<cs::Send<int, cs::End>, cs::End, 0, 0>;
using Sub_Epoch  = sp::EpochedDelegate<sp::Send<int, sp::End>, sp::End, 0, 0>;
static_assert(std::is_same_v<Sess_Epoch, Sub_Epoch>);

// ─── 2. fixy::mach — Machine + mint factories are identity aliases ────

struct DummyState { int x = 0; };

using Mach_T = cm::Machine<DummyState>;
using Sub_T  = ss::Machine<DummyState>;
static_assert(std::is_same_v<Mach_T, Sub_T>);

// Zero-cost claim (mirrors substrate).
static_assert(sizeof(Mach_T) == sizeof(DummyState));

// ─── 3. fixy::contract — CRUCIBLE_PRE / CRUCIBLE_POST expand correctly
//
// Macros aren't typed, so we exercise them in a constexpr function
// body and pin the consteval-fire path.  Same shape as
// safety/Contract.h consumers — the include path was the only thing
// the fixy header changed.

[[nodiscard]] constexpr int double_positive(int n) noexcept {
    CRUCIBLE_PRE(n > 0);
    int result = n * 2;
    CRUCIBLE_POST(result, result > n);
    return result;
}

static_assert(double_positive(3) == 6);
static_assert(double_positive(100) == 200);

// ─── 4. fixy::decide — named predicate catalog re-export ─────────────
//
// `fixy::decide::no_overflow_sum(a, b)` is the substrate
// `safety::decide::no_overflow_sum(a, b)` by namespace alias.  A call
// through either path produces the same machine code.  Pin via
// constexpr equality checks that fire at compile time.

namespace cd = crucible::fixy::decide;

// The compiler sees cd::X and crucible::decide::X as the same symbol
// (namespace alias collapses), which is the whole point — the
// substrate symbol IS the fixy::decide path.  Pin the actual predicate
// behavior to prove the re-export is live.
static_assert(cd::no_overflow_sum(int{3}, int{5}));
static_assert(!cd::no_overflow_sum(INT32_MAX, int{1}));
static_assert(cd::in_range(int{7}, int{0}, int{10}));
static_assert(cd::positive(int{42}));
static_assert(!cd::positive(int{0}));
static_assert(cd::implies(true, true));

}  // namespace

// ─── Runtime smoke driver ────────────────────────────────────────────
//
// Exercise the aliases at runtime to pin that they actually
// instantiate and execute identically to the substrate path.

int main() {
    // Mach: mint via fixy alias, transition via fixy alias.
    auto m = cm::mint_machine<DummyState>(DummyState{42});
    if (m.data().x != 42) {
        std::fprintf(stderr, "test_fixy_aliases: mint_machine failed\n");
        return 1;
    }

    struct NextState { int doubled; };
    auto m2 = cm::transition_to<NextState>(std::move(m), NextState{84});
    if (m2.data().doubled != 84) {
        std::fprintf(stderr, "test_fixy_aliases: transition_to failed\n");
        return 1;
    }

    // Contract: macros must fire at runtime under enforce semantic.
    volatile int input = 7;
    int got = double_positive(input);
    if (got != 14) {
        std::fprintf(stderr, "test_fixy_aliases: contract macro path failed\n");
        return 1;
    }

    std::printf("fixy aliases sentinel: sess + mach + contract green\n");
    return 0;
}
