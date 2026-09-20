// The linear capability token, driven with non-constant arguments.
//
// The body below was an inline runtime_smoke_test_capability in
// Capability.h, compiled into every translation unit that included the
// header.  It builds witnesses and mints from them, which is a scenario
// rather than a claim about the shipped type, so it belongs here.  The
// assertions that ARE about the shipped type stayed in the header.

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

// Each source mints the effects it permits, and a capability carries
// both the effect and the source that issued it.
void each_source_mints_what_it_permits() {
    auto bg = fe::testing::bg();
    auto bg_alloc = fe::mint_cap<Effect::Alloc>(bg);
    auto bg_io = fe::mint_cap<Effect::IO>(bg);
    auto bg_block = fe::mint_cap<Effect::Block>(bg);
    auto bg_self = fe::mint_cap<Effect::Bg>(bg);

    auto init = fe::testing::init();
    auto init_alloc = fe::mint_cap<Effect::Alloc>(init);
    auto init_io = fe::mint_cap<Effect::IO>(init);
    auto init_self = fe::mint_cap<Effect::Init>(init);

    auto test = fe::testing::test();
    auto test_alloc = fe::mint_cap<Effect::Alloc>(test);
    auto test_block = fe::mint_cap<Effect::Block>(test);

    fe::Capability<Effect::Alloc, fe::Bg> moved = std::move(bg_alloc);
    std::move(moved).consume();

    static_assert(fe::cap_of_v<decltype(bg_io)> == Effect::IO);
    static_assert(fe::cap_of_v<decltype(test_block)> == Effect::Block);
    static_assert(std::is_same_v<fe::source_of_t<decltype(init_io)>, fe::Init>);
    static_assert(std::is_same_v<fe::source_of_t<decltype(test_alloc)>, fe::Test>);

    static_assert(fe::IsCapability<decltype(bg_io)>);
    static_assert(!fe::IsCapability<int>);

    static_cast<void>(bg_io);
    static_cast<void>(bg_block);
    static_cast<void>(bg_self);
    static_cast<void>(init_alloc);
    static_cast<void>(init_io);
    static_cast<void>(init_self);
    static_cast<void>(test_alloc);
    static_cast<void>(test_block);
}

// A value atom hands back its bare tag when the capability is consumed.
void every_value_atom_extracts_its_bare_tag() {
    auto bg = fe::testing::bg();

    auto a = fe::mint_cap<Effect::Alloc>(bg);
    [[maybe_unused]] fe::cap::Alloc bare_a = fe::extract_bare(std::move(a));

    auto i = fe::mint_cap<Effect::IO>(bg);
    [[maybe_unused]] fe::cap::IO bare_i = fe::extract_bare(std::move(i));

    auto b = fe::mint_cap<Effect::Block>(bg);
    [[maybe_unused]] fe::cap::Block bare_b = fe::extract_bare(std::move(b));
}

}  // namespace

int main() {
    each_source_mints_what_it_permits();
    every_value_atom_extracts_its_bare_tag();

    // Each context is handed the capability it claims, and mint_from_ctx
    // reads what the context's source permits.
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
