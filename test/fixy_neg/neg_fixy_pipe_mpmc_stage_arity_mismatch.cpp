// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// `mint_mpmc_stage_from_endpoints` rejects a call when its endpoint pack
// does not agree with the arity of the body.
//
// Violation: the body `fan_in_body` takes 4 parameters (3 consumers and
// 1 producer), and the caller gives only 3 arguments.  The first check in
// `mpmc_stage_from_endpoints_gate::compute()` compares
// `sizeof...(Endpoints)` with `arity_v<FnPtr>`, and the gate rejects the
// call.
//
// The non-endpoint fixture stops at a different check.  This fixture
// stops at the arity check, and that fixture stops at the handle-type
// check.
//
// Expected diagnostic: "associated constraints are not satisfied"
// at CtxFitsMpmcStageFromEndpoints or mpmc_stage_from_endpoints_gate.

#include <crucible/concurrent/_StageEndpointBridge.h>
#include <crucible/effects/_ExecCtx.h>

#include <optional>
#include <utility>

namespace eff = crucible::effects;
namespace conc = crucible::concurrent;

template <typename T>
struct FakeConsumer {
    static constexpr std::size_t per_call_working_set = 64;
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct FakeProducer {
    static constexpr std::size_t per_call_working_set = 64;
    [[nodiscard]] bool try_push(T const&) noexcept { return false; }
};

// The body takes 4 endpoints: 3 consumers and 1 producer.
inline void fan_in_body(FakeConsumer<int>&&, FakeConsumer<int>&&, FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}

int main() {
    eff::HotFgCtx ctx;

    // The caller gives 3 endpoints, not 4.
    auto bad = conc::mint_mpmc_stage_from_endpoints<&fan_in_body>(ctx, FakeConsumer<int>{}, FakeConsumer<int>{},
                                                                  FakeConsumer<int>{});
    (void)bad;
    return 0;
}
