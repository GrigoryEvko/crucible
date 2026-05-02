// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Tier 3 commit 1 audit round 2 (CLAUDE.md §XXI HS14): mint_stage's
// consumer_handle_type / producer_handle_type parameters use the
// non-deduced typename context
//
//   typename Stage<FnPtr, Ctx>::consumer_handle_type&& in
//
// which forces EXACT TYPE MATCH against FnPtr's expected handle type.
// Substitution into the typename is a soundness gate distinct from
// the CtxFitsStage concept gate — it pins the handles passed at the
// call site to the exact types FnPtr's signature names.
//
// Violation: FnPtr expects FakeConsumer<int>&& but the call passes
// FakeConsumer<float>&&.  Both types satisfy IsConsumerHandle<>; the
// CtxFitsStage gate alone WOULD admit this if there were no
// parameter-type binding.  The non-deduced typename binding rejects
// it: substitution produces FakeConsumer<int> on the parameter, which
// does not bind to a FakeConsumer<float>&& argument.
//
// Expected diagnostic: "no matching function for call to|cannot
// convert|cannot bind" — substitution failure or argument-binding
// failure, not a "constraints are not satisfied" message (the
// concept passes; the parameter-type binding fails).

#include <crucible/concurrent/Stage.h>
#include <crucible/effects/ExecCtx.h>

#include <optional>
#include <utility>

namespace conc = crucible::concurrent;
namespace eff  = crucible::effects;

template <typename T>
struct FakeConsumer {
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct FakeProducer {
    [[nodiscard]] bool try_push(T const&) noexcept { return false; }
};

// Stage body expects FakeConsumer<int>&& on slot 0.
inline void int_to_int_stage(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}

int main() {
    eff::HotFgCtx ctx;
    FakeConsumer<float> wrong_type_consumer;     // <-- float, not int
    FakeProducer<int>   producer;

    auto bad = conc::mint_stage<&int_to_int_stage>(
        ctx,
        std::move(wrong_type_consumer),  // type mismatch vs FakeConsumer<int>
        std::move(producer));
    (void)bad;
    return 0;
}
