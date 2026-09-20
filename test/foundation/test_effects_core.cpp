// Sentinel TU for the effect atoms and rows.  The two headers carry
// their own static_asserts and inline smoke tests; this file includes
// them, calls each smoke test once, and keeps the runtime cells of the
// old test_effects.cpp that concern atoms, contexts and rows.

#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <string_view>
#include <type_traits>

namespace {

namespace fe = ::foundation::effects;

// A context stays one byte however many empty capability members it
// carries, and the short aliases resolve to the tag types.
static_assert(sizeof(fe::Bg) == 1 && sizeof(fe::Init) == 1 && sizeof(fe::Test) == 1);
static_assert(std::is_same_v<fe::Alloc, fe::cap::Alloc>);
static_assert(std::is_same_v<fe::IO, fe::cap::IO>);
static_assert(std::is_same_v<fe::Block, fe::cap::Block>);

// A context is built only through a minter holding the passkey; the
// test witness is the one minter this layer defines.
static_assert(!std::is_default_constructible_v<fe::Bg>);
static_assert(!std::is_default_constructible_v<fe::Init>);
static_assert(!std::is_default_constructible_v<fe::Test>);
static_assert(noexcept(fe::testing::bg()) && noexcept(fe::testing::init()) && noexcept(fe::testing::test()));

// The production minters are declared here and defined above this
// layer, so they are incomplete in a foundation TU.
template <class T>
concept Complete = requires { sizeof(T); };
static_assert(!Complete<fe::host::BackgroundOwner>);
static_assert(!Complete<fe::host::InitOwner>);

// The mask lattice is a Row, and the two row spellings agree on
// membership for every atom.
static_assert(::foundation::algebra::Row<fe::EffectRowLattice>);
[[nodiscard]] consteval bool every_atom_agrees_across_spellings() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^fe::Effect));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        constexpr fe::Effect atom = [:en:];
        if (!fe::EffectRowLattice::contains(fe::row_descriptor_v<fe::Row<atom>>, atom)) return false;
        if (fe::EffectRowLattice::contains(fe::row_descriptor_v<fe::Row<>>, atom)) return false;
        if (!fe::row_contains_v<fe::Row<atom>, atom>) return false;
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_atom_agrees_across_spellings());

// The tag type is the entire gate: no other parameter admits a call.
[[nodiscard]] int with_alloc(fe::cap::Alloc) noexcept { return 42; }

// Every accessor called with a non-constant argument.  The
// static_assert walls only prove the constant-evaluated path.  This was
// an inline runtime_smoke_test in Effect.h, compiled into every
// translation unit that included the header.
void every_accessor_runs_at_run_time() {
    fe::Effect e = fe::Effect::Alloc;
    [[maybe_unused]] std::string_view n1 = fe::effect_name(e);
    e = fe::Effect::IO;
    [[maybe_unused]] std::string_view n2 = fe::effect_name(e);
    e = fe::Effect::Block;
    [[maybe_unused]] std::string_view n3 = fe::effect_name(e);
    e = fe::Effect::Bg;
    [[maybe_unused]] std::string_view n4 = fe::effect_name(e);
    e = fe::Effect::Init;
    [[maybe_unused]] std::string_view n5 = fe::effect_name(e);
    e = fe::Effect::Test;
    [[maybe_unused]] std::string_view n6 = fe::effect_name(e);

    [[maybe_unused]] fe::cap::Alloc a_tag{};
    [[maybe_unused]] fe::cap::IO i_tag{};
    [[maybe_unused]] fe::cap::Block b_tag{};

    [[maybe_unused]] auto bg_ctx = fe::testing::bg();
    [[maybe_unused]] auto init_ctx = fe::testing::init();
    [[maybe_unused]] auto test_ctx = fe::testing::test();
}

}  // namespace

int main() {
    every_accessor_runs_at_run_time();
    fe::detail::effect_row_self_test::runtime_smoke_test();
    fe::runtime_smoke_test_lattice();

    auto bg = fe::testing::bg();
    auto init = fe::testing::init();
    auto test = fe::testing::test();
    [[maybe_unused]] fe::cap::IO io_bg = bg.io;
    [[maybe_unused]] fe::cap::Block blk_bg = bg.block;
    [[maybe_unused]] fe::cap::Alloc a_init = init.alloc;
    [[maybe_unused]] fe::cap::IO io_init = init.io;
    [[maybe_unused]] fe::cap::Block blk_test = test.block;
    if (with_alloc(bg.alloc) != 42) return 1;
    if (with_alloc(test.alloc) != 42) return 2;

    fe::Effect atom = fe::Effect::Block;  // deliberately not constexpr
    auto row = fe::EffectRowLattice::join(fe::row_descriptor_v<fe::Row<fe::Effect::Alloc>>,
                                          fe::EffectRowLattice::single(atom));
    if (!fe::EffectRowLattice::contains(row, atom)) return 3;
    if (!fe::EffectRowLattice::leq(row, fe::EffectRowLattice::top())) return 4;
    return 0;
}
