// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-AUDIT-D7 fixture: `mint_mpmc_stage_from_endpoints` via fixy::
// alias rejects when one of the variadic endpoint pack arguments is
// not a (Producer|Consumer)Endpoint.
//
// Violation: passes a bare int in place of the producer endpoint.
// `StageHandlesMatchEndpointsExtended<FnPtr, inputs, outputs>` in
// `mpmc_stage_from_endpoints_gate::compute()` cannot match an `int`
// to the producer-handle slot of fan_in_body's signature.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at CtxFitsMpmcStageFromEndpoints /
// mpmc_stage_from_endpoints_gate.

#include <crucible/concurrent/Endpoint.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Pipe.h>

#include <optional>
#include <utility>

namespace eff   = crucible::effects;
namespace fpipe = crucible::fixy::pipe;
namespace conc  = crucible::concurrent;

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

inline void fan_in_body(FakeConsumer<int>&&,
                        FakeConsumer<int>&&,
                        FakeProducer<int>&&) noexcept {}

int main() {
    eff::HotFgCtx ctx;

    // First two slots are valid; the third (must be a Producer
    // endpoint) is a bare int — the variadic gate fails.
    int not_an_endpoint = 0;

    auto bad = fpipe::mint_mpmc_stage_from_endpoints<&fan_in_body>(
        ctx,
        FakeConsumer<int>{},
        FakeConsumer<int>{},
        not_an_endpoint);
    (void)bad;
    return 0;
}
