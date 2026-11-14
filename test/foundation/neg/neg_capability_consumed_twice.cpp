// A capability that a move spent, consumed one more time in a constant
// evaluation.  The token records that the move spent it, so the second
// use fails the check in its body and the evaluation stops.

#include <foundation/effects/Capability.h>

#include <utility>

namespace fe = ::foundation::effects;

using AllocToken = fe::Capability<fe::Effect::Alloc, fe::Bg>;

[[nodiscard]] constexpr AllocToken& held(AllocToken& token) noexcept { return token; }

[[nodiscard]] constexpr bool consumes_a_spent_token() noexcept {
    AllocToken source = fe::mint_cap<fe::Effect::Alloc>(fe::testing::bg());
    AllocToken target{std::move(held(source))};
    std::move(target).consume();
    std::move(held(source)).consume();
    return true;
}

static_assert(consumes_a_spent_token());

int main() { return 0; }
