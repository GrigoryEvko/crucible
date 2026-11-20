// The linear capability token and its two mints, driven at run time, and
// the door of the bare tag from a context.
//
// A context mints a capability for an atom exactly when its row claims
// the atom, whatever its source permits.  The matrix below reads the
// atoms off the enum, so a new atom is covered the moment it is
// declared.  Each claimed atom is then minted from a context built at run
// time, moved into the function that spends it, and consumed there.

#include <foundation/effects/Capability.h>
#include <foundation/reflect/ReflectionList.h>

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
// Contexts whose row claims one value atom of the source and no other atom.
using BgAllocOnly = fe::ExecCtx<fe::Bg, fe::Row<Effect::Alloc>>;
using InitIoOnly = fe::ExecCtx<fe::Init, fe::Row<Effect::IO>>;
using TestBlockOnly = fe::ExecCtx<fe::Test, fe::Row<Effect::Block>>;
using BgNondetOnly = fe::ExecCtx<fe::Bg, fe::Row<Effect::Bg, Effect::Nondet>>;
}  // namespace w

template <Effect E, class Ctx>
concept MintsFromCtx = requires(Ctx const& ctx) { fe::mint_from_ctx<E>(ctx); };

inline constexpr ::foundation::reflect::ReflectionList atoms =
    ::foundation::reflect::reflection_list(std::meta::enumerators_of(^^fe::Effect));

// True when the context mints the atoms of its row and no other atom, and
// CapMatchesCtx admits a capability for exactly the same atoms, whatever
// its source.  Complexity: one probe for each atom of the catalog.
template <class Ctx>
[[nodiscard]] consteval bool mints_exactly_its_row() noexcept {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto atom : atoms) {
        constexpr Effect effect = [:atom:];
        constexpr bool claims = fe::row_contains(^^typename Ctx::row_type, effect);
        if constexpr (MintsFromCtx<effect, Ctx> != claims) return false;
        if constexpr (fe::CapMatchesCtx<fe::Capability<effect, fe::Init>, Ctx> != claims) return false;
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

template <Effect E, class Ctx>
concept TagsFromCtx = requires(Ctx const& ctx) { fe::extract_bare_from_ctx<E>(ctx); };

// True when the tag door gives the tag of each value atom of the row, and no
// tag of an atom that the row does not claim, whatever the source holds.
// Complexity: one probe for each atom of the catalog.
template <class Ctx>
[[nodiscard]] consteval bool tags_exactly_its_row() noexcept {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto atom : atoms) {
        constexpr Effect effect = [:atom:];
        constexpr bool gives = fe::row_contains(^^typename Ctx::row_type, effect) && fe::HasBareTag<effect>;
        if constexpr (TagsFromCtx<effect, Ctx> != gives) return false;
    }
#pragma GCC diagnostic pop
    return true;
}

static_assert(tags_exactly_its_row<w::FgWitness>());
static_assert(tags_exactly_its_row<w::BgWitness>());
static_assert(tags_exactly_its_row<w::BgBlockWitness>());
static_assert(tags_exactly_its_row<w::InitBlockWitness>());
static_assert(tags_exactly_its_row<w::TestWitnessCtx>());
static_assert(tags_exactly_its_row<w::BgAllocOnly>());
static_assert(tags_exactly_its_row<w::InitIoOnly>());
static_assert(tags_exactly_its_row<w::TestBlockOnly>());
static_assert(tags_exactly_its_row<w::BgNondetOnly>());

// The door gives the tag of the type of the atom.  A constant evaluation
// takes the same body as a run-time call.
template <Effect E, class Ctx>
[[nodiscard]] constexpr bool gives_its_tag(Ctx const& ctx) noexcept {
    [[maybe_unused]] fe::bare_tag_t<E> const tag = fe::extract_bare_from_ctx<E>(ctx);
    return std::is_same_v<decltype(fe::extract_bare_from_ctx<E>(ctx)), fe::bare_tag_t<E>>;
}

static_assert(gives_its_tag<Effect::Alloc>(w::BgAllocOnly{fe::testing::bg()}));
static_assert(gives_its_tag<Effect::IO>(w::InitIoOnly{fe::testing::init()}));
static_assert(gives_its_tag<Effect::Block>(w::TestBlockOnly{fe::testing::test()}));
static_assert(gives_its_tag<Effect::Nondet>(w::BgNondetOnly{fe::testing::bg()}));

// The tag of each value atom, each from a context whose row claims only that
// atom, at run time.
[[nodiscard]] bool each_narrow_context_gives_its_tag() noexcept {
    return gives_its_tag<Effect::Alloc>(w::BgAllocOnly{fe::testing::bg()})
        && gives_its_tag<Effect::IO>(w::InitIoOnly{fe::testing::init()})
        && gives_its_tag<Effect::Block>(w::TestBlockOnly{fe::testing::test()})
        && gives_its_tag<Effect::Nondet>(w::BgNondetOnly{fe::testing::bg()});
}

// The bare tag of an atom that the row claims is not a capability, so it
// does not match the context.
static_assert(fe::CapMatchesCtx<fe::Capability<Effect::IO, fe::Bg>, w::BgIoWitness>);
static_assert(!fe::CapMatchesCtx<fe::cap::IO, w::BgIoWitness>);
static_assert(!fe::CapMatchesCtx<int, w::BgIoWitness>);

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
        if constexpr (fe::row_contains(^^R, effect)) sum += static_cast<int>(effect) + 1;
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
        if constexpr (fe::row_contains(^^typename Ctx::row_type, effect)) {
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
    if (!each_narrow_context_gives_its_tag()) return 9;
    return 0;
}
