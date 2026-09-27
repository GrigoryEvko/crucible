// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// An atomic session sends a payload that carries IO, and the context of
// the mint holds no IO.  The gate of mint_atomic_session is the gate of
// mint_session over the reference to the cell, so the mint refuses the
// context.
//
// Expected diagnostic: the context does not admit the row of the protocol.
#include <fixy/session/MachineBridge.h>

#include <foundation/effects/Computation.h>

#include <atomic>

namespace neg_sess_atomic_ctx_row_refused_types {
enum class Phase : unsigned char {
    Idle,
    Busy
};

struct PhaseCell : ::foundation::Pinned<PhaseCell> {
    using state_type = Phase;
    [[nodiscard]] Phase load(std::memory_order order = std::memory_order_acquire) const noexcept {
        return value_.load(order);
    }

private:
    std::atomic<Phase> value_{Phase::Idle};
};
}  // namespace neg_sess_atomic_ctx_row_refused_types

int main() {
    namespace s = ::fixy::session;
    namespace eff = ::foundation::effects;
    using namespace neg_sess_atomic_ctx_row_refused_types;
    using SendsIo = s::Send<eff::Computation<eff::Row<eff::Effect::IO>, int>, s::End>;
    const eff::detail::ctx_witnesses::BgWitness ctx{eff::testing::bg()};
    PhaseCell cell{};
    auto session = s::mint_atomic_session<SendsIo>(ctx, cell);
    std::move(session).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
