// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-AUDIT-D7 fixture: `mint_mpmc_stage_from_endpoints` via fixy::
// alias rejects when the variadic endpoint pack size does not match
// the body's arity.
//
// Violation: body `fan_in_body` takes 4 parameters (3 consumers + 1
// producer); caller supplies only 3 arguments.  The first short-
// circuit in `mpmc_stage_from_endpoints_gate::compute()` —
// `sizeof...(Endpoints) != arity_v<FnPtr>` — fails the gate.
//
// Distinct rejection class from the non-endpoint fixture: this
// exercises the arity-mismatch short-circuit (Phase 1) rather than
// the handle-type-match check (Phase 2).
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at CtxFitsMpmcStageFromEndpoints /
// mpmc_stage_from_endpoints_gate.

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Pipe.h>

#include <optional>
#include <utility>

namespace eff   = crucible::effects;
namespace fpipe = crucible::fixy::pipe;

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

// Body expects 4 endpoints: 3 consumers + 1 producer.
inline void fan_in_body(FakeConsumer<int>&&,
                        FakeConsumer<int>&&,
                        FakeConsumer<int>&&,
                        FakeProducer<int>&&) noexcept {}

int main() {
    eff::HotFgCtx ctx;

    // Caller supplies only 3 endpoints, not 4 — arity mismatch.
    auto bad = fpipe::mint_mpmc_stage_from_endpoints<&fan_in_body>(
        ctx,
        FakeConsumer<int>{},
        FakeConsumer<int>{},
        FakeConsumer<int>{});
    (void)bad;
    return 0;
}
