// The linear capability token and its two mints, driven at run time.
//
// A context mints a capability for an atom exactly when its row claims
// the atom, whatever its source permits.  The matrix below reads the
// atoms off the enum, so a new atom is covered the moment it is
// declared.  Each claimed atom is then minted from a context built at run
// time, moved into the function that spends it, and consumed there.

#include <foundation/effects/Capability.h>

#include <meta>
#include <type_traits>
#include <utility>

namespace {

namespace fe = ::foundation::effects;
using fe::Effect;

// One context of each source, spelled with the public names.
namespace w {
using FgWitness = fe::ExecCtx<fe::ctx_cap::Fg, fe::Row<>>;
using BgWitness = fe::ExecCtx<fe::Bg, fe::Row<Effect::Bg, Effect::Alloc>>;
using BgIoWitness = fe::ExecCtx<fe::Bg, fe::Row<Effect::Bg, Effect::Alloc, Effect::IO>>;
using BgBlockWitness = fe::ExecCtx<fe::Bg, fe::Row<Effect::Bg, Effect::Alloc, Effect::IO, Effect::Block>>;
using InitWitness = fe::ExecCtx<fe::Init, fe::Row<Effect::Init, Effect::Alloc, Effect::IO>>;
using InitBlockWitness = fe::ExecCtx<fe::Init, fe::Row<Effect::Init, Effect::Alloc, Effect::IO, Effect::Block>>;
using TestWitnessCtx = fe::ExecCtx<fe::Test, fe::Row<Effect::Test, Effect::Alloc, Effect::IO, Effect::Block>>;
}  // namespace w

template <Effect E, class Ctx>
concept MintsFromCtx = requires(Ctx const& ctx) { fe::mint_from_ctx<E>(ctx); };

inline constexpr auto atoms = std::define_static_array(std::meta::enumerators_of(^^fe::Effect));

// True when the context mints the atoms of its row and no other atom.
// Complexity: one probe for each atom of the catalog.
template <class Ctx>
[[nodiscard]] consteval bool mints_exactly_its_row() noexcept {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto atom : atoms) {
        constexpr Effect effect = [:atom:];
        if constexpr (MintsFromCtx<effect, Ctx> != fe::row_contains_v<typename Ctx::row_type, effect>) return false;
    }
#pragma GCC diagnostic pop
    return true;
}

static_assert(mints_exactly_its_row<w::FgWitness>());
static_assert(mints_exactly_its_row<w::BgWitness>());
static_assert(mints_exactly_its_row<w::BgIoWitness>());
static_assert(mints_exactly_its_row<w::BgBlockWitness>());
static_assert(mints_exactly_its_row<w::InitWitness>());
static_assert(mints_exactly_its_row<w::InitBlockWitness>());
static_assert(mints_exactly_its_row<w::TestWitnessCtx>());

// The drain context claims Bg and Alloc, and its source permits IO and
// Block as well.  The source is not the bound.
static_assert(fe::CanMintCap<Effect::IO, fe::Bg> && !MintsFromCtx<Effect::IO, w::BgWitness>);
static_assert(fe::CanMintCap<Effect::Block, fe::Bg> && !MintsFromCtx<Effect::Block, w::BgWitness>);

// A token is spent once, by a function that takes it by value.
template <Effect E, class Source>
[[nodiscard]] int spend(fe::Capability<E, Source> token) noexcept {
    std::move(token).consume();
    return static_cast<int>(E) + 1;
}

// The sum that spending every atom of a row returns.
template <class R>
[[nodiscard]] consteval int expected_spend_of_row() noexcept {
    int sum = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto atom : atoms) {
        constexpr Effect effect = [:atom:];
        if constexpr (fe::row_contains_v<R, effect>) sum += static_cast<int>(effect) + 1;
    }
#pragma GCC diagnostic pop
    return sum;
}

// Mints every atom the context claims, and spends each token.
template <class Ctx>
[[nodiscard]] int spend_every_claimed_atom(Ctx const& ctx) noexcept {
    int sum = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto atom : atoms) {
        constexpr Effect effect = [:atom:];
        if constexpr (fe::row_contains_v<typename Ctx::row_type, effect>) {
            auto token = fe::mint_from_ctx<effect>(ctx);
            static_assert(std::is_same_v<decltype(token), fe::Capability<effect, typename Ctx::cap_type>>);
            sum += spend(std::move(token));
        }
    }
#pragma GCC diagnostic pop
    return sum;
}

template <class Ctx>
[[nodiscard]] bool spends_its_row(Ctx const& ctx) noexcept {
    return spend_every_claimed_atom(ctx) == expected_spend_of_row<typename Ctx::row_type>();
}

// A source mints what it permits, and a value atom hands back its bare
// tag when the capability is consumed.
[[nodiscard]] int each_source_mints_what_it_permits() noexcept {
    auto const bg = fe::testing::bg();
    auto const init = fe::testing::init();
    auto const test = fe::testing::test();
    int sum = spend(fe::mint_cap<Effect::Bg>(bg)) + spend(fe::mint_cap<Effect::Init>(init))
              + spend(fe::mint_cap<Effect::Test>(test));
    [[maybe_unused]] fe::cap::Alloc alloc = fe::extract_bare(fe::mint_cap<Effect::Alloc>(bg));
    [[maybe_unused]] fe::cap::IO io = fe::extract_bare(fe::mint_cap<Effect::IO>(init));
    [[maybe_unused]] fe::cap::Block block = fe::extract_bare(fe::mint_cap<Effect::Block>(test));
    return sum;
}

}  // namespace

int main() {
    if (!spends_its_row(w::FgWitness{fe::testing::foreground()})) return 1;
    if (!spends_its_row(w::BgWitness{fe::testing::bg()})) return 2;
    if (!spends_its_row(w::BgIoWitness{fe::testing::bg()})) return 3;
    if (!spends_its_row(w::BgBlockWitness{fe::testing::bg()})) return 4;
    if (!spends_its_row(w::InitWitness{fe::testing::init()})) return 5;
    if (!spends_its_row(w::InitBlockWitness{fe::testing::init()})) return 6;
    if (!spends_its_row(w::TestWitnessCtx{fe::testing::test()})) return 7;
    if (each_source_mints_what_it_permits() != (4 + 5 + 6)) return 8;
    return 0;
}
