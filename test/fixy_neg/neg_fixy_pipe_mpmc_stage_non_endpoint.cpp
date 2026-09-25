// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// `mint_mpmc_stage_from_endpoints` rejects a call when one argument of
// the endpoint pack is not a producer endpoint or a consumer endpoint.
//
// Violation: a bare int takes the position of the producer endpoint.
// `StageHandlesMatchEndpointsExtended<FnPtr, inputs, outputs>` in
// `mpmc_stage_from_endpoints_gate::compute()` cannot match an `int` to
// the producer handle in the signature of fan_in_body.
//
// Expected diagnostic: "associated constraints are not satisfied"
// at CtxFitsMpmcStageFromEndpoints or mpmc_stage_from_endpoints_gate.

#include <crucible/concurrent/Endpoint.h>
#include <crucible/concurrent/StageEndpointBridge.h>
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

inline void fan_in_body(FakeConsumer<int>&&, FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {}

int main() {
    eff::HotFgCtx ctx;

    // The first two arguments are correct.  The third argument must be a
    // producer endpoint, but it is a bare int.
    int not_an_endpoint = 0;

    auto bad = conc::mint_mpmc_stage_from_endpoints<&fan_in_body>(ctx, FakeConsumer<int>{}, FakeConsumer<int>{},
                                                                  not_an_endpoint);
    (void)bad;
    return 0;
}
