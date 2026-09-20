// Sentinel TU for the linear capability token: the header's own smoke
// test, driven with non-constant arguments.

#include <foundation/effects/Capability.h>

#include <type_traits>
#include <utility>

namespace {

namespace fe = ::foundation::effects;
using fe::Effect;
using fe::detail::exec_ctx_self_test::BgIoWitness;
using fe::detail::exec_ctx_self_test::BgWitness;

// A token is one byte, move-only, and minted only through the friended
// factory.
static_assert(sizeof(fe::Capability<Effect::Alloc, fe::Bg>) == 1);
static_assert(!std::is_copy_constructible_v<fe::Capability<Effect::IO, fe::Bg>>);
static_assert(!std::is_default_constructible_v<fe::Capability<Effect::IO, fe::Bg>>);

// What the source permits, not what the context claims, is what a
// context-bound mint reads.
static_assert(fe::CtxCanMint<BgWitness, Effect::IO>);
static_assert(!fe::CtxOwnsCapability<BgWitness, Effect::IO>);

}  // namespace

int main() {
    fe::runtime_smoke_test_capability();

    // Each context is handed the capability it claims.
    BgWitness bg_ctx{fe::testing::bg()};
    BgIoWitness bg_io_ctx{fe::testing::bg()};
    auto alloc = fe::mint_from_ctx<Effect::Alloc>(bg_ctx);
    auto io = fe::mint_from_ctx<Effect::IO>(bg_io_ctx);
    static_assert(std::is_same_v<decltype(alloc), fe::Capability<Effect::Alloc, fe::Bg>>);
    static_assert(std::is_same_v<decltype(io), fe::Capability<Effect::IO, fe::Bg>>);
    [[maybe_unused]] fe::cap::Alloc bare = fe::extract_bare(std::move(alloc));
    std::move(io).consume();
    return 0;
}
