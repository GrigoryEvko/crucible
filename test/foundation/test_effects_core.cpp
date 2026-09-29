// Sentinel TU for the effect atoms and rows.  The two headers carry
// their own static_asserts.  This file includes them and holds the
// runtime cells that concern atoms, contexts and rows.

#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <cstddef>
#include <cstdint>
#include <meta>
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

// No class derives from a context.  A class that a translation unit
// declares cannot become a context by derivation, and a copy of it
// cannot become a context either.  The walk asks IsContext about each
// type of the namespace, and a context that a later change adds is in the
// walk at once.
[[nodiscard]] consteval std::size_t contexts_that_are_not_final() {
    std::size_t contexts = 0;
    std::size_t open = 0;
    for (const std::meta::info member :
         std::meta::members_of(^^::foundation::effects, std::meta::access_context::current())) {
        if (!std::meta::is_type(member) || !std::meta::can_substitute(^^fe::IsContext, {member})) continue;
        if (!std::meta::extract<bool>(std::meta::substitute(^^fe::IsContext, {member}))) continue;
        ++contexts;
        if (!std::meta::is_final_type(member)) ++open;
    }
    return contexts == 0 ? 1 : open;
}
static_assert(contexts_that_are_not_final() == 0, "a context is not final, or the walk found no context");

// The owners are defined in the header of their keys, so they are
// complete in every TU that can name a key, and no TU can define one
// again.  Each owner has one member, the door to its context, and
// scripts/check-ctx-init-door.py limits the calls of each door.
template <class T>
concept Complete = requires { sizeof(T); };
static_assert(Complete<fe::host::BackgroundOwner> && Complete<fe::host::InitOwner>);
static_assert(std::is_empty_v<fe::host::BackgroundOwner> && std::is_empty_v<fe::host::InitOwner>);

// The three are the whole roster.  The foreground marker, a plain type,
// a value atom and a lookalike that derives from the family base with a
// real key are not contexts, so nothing a translation unit declares
// reaches a gate.
struct Lookalike final
    : fe::detail::ContextBase<Lookalike, fe::detail::ctx_mint::bg_key, fe::Effect::Bg, fe::Effect::Alloc> {
    constexpr Lookalike() noexcept = default;
};
static_assert(fe::IsContext<fe::Bg> && fe::IsContext<fe::Init> && fe::IsContext<fe::Test>);
static_assert(!fe::IsContext<Lookalike> && !fe::IsContext<int> && !fe::IsContext<fe::cap::Alloc>);
static_assert(!fe::IsContext<fe::Bg const> && !fe::IsContext<fe::Bg&>, "The roster is matched on the exact type.");

// One key mints one context: the factory's constraint reads the
// context's own key_type, so the wrong key is refused at the call, and
// the lookalike is refused although it names a real key.
static_assert(fe::CanMintContext<fe::Bg, fe::detail::ctx_mint::bg_key>);
static_assert(fe::CanMintContext<fe::Init, fe::detail::ctx_mint::init_key>);
static_assert(fe::CanMintContext<fe::Test, fe::detail::ctx_mint::test_key>);
static_assert(!fe::CanMintContext<fe::Init, fe::detail::ctx_mint::bg_key>,
              "A background key cannot mint an init context.");
static_assert(!fe::CanMintContext<fe::Bg, fe::detail::ctx_mint::init_key>);
static_assert(!fe::CanMintContext<fe::Test, fe::detail::ctx_mint::bg_key>);
static_assert(!fe::CanMintContext<Lookalike, fe::detail::ctx_mint::bg_key>);
static_assert(!fe::CanMintContext<int, fe::detail::ctx_mint::bg_key>);

// Each context declares its atom, its key and the row it permits, and
// Ctx.h reads the row through permitted_as rather than restating it.
template <fe::Effect... Es>
struct RowProbe {};
static_assert(fe::Bg::own_effect == fe::Effect::Bg && fe::Init::own_effect == fe::Effect::Init
              && fe::Test::own_effect == fe::Effect::Test);
static_assert(std::is_same_v<fe::Bg::key_type, fe::detail::ctx_mint::bg_key>);
static_assert(std::is_same_v<fe::Init::key_type, fe::detail::ctx_mint::init_key>);
static_assert(std::is_same_v<fe::Test::key_type, fe::detail::ctx_mint::test_key>);
static_assert(std::is_same_v<fe::Bg::permitted_as<RowProbe>,
                             RowProbe<fe::Effect::Bg, fe::Effect::Alloc, fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<fe::Init::permitted_as<RowProbe>,
                             RowProbe<fe::Effect::Init, fe::Effect::Alloc, fe::Effect::IO, fe::Effect::Block>>);
static_assert(std::is_same_v<fe::Test::permitted_as<RowProbe>,
                             RowProbe<fe::Effect::Test, fe::Effect::Alloc, fe::Effect::IO, fe::Effect::Block>>);

// The fields are the holdings and nothing else.  Each of the three
// contexts has a block to give to a function that waits, because process
// startup waits in the kernel too.  A lookalike with no Block holding
// has no field of that name.
template <class C>
concept HoldsBlock = requires(C const& c) { c.block; };
template <class C>
concept HoldsAllocAndIo = requires(C const& c) {
    c.alloc;
    c.io;
};
static_assert(HoldsAllocAndIo<fe::Bg> && HoldsAllocAndIo<fe::Init> && HoldsAllocAndIo<fe::Test>);
static_assert(HoldsBlock<fe::Bg> && HoldsBlock<fe::Test> && HoldsBlock<fe::Init>);
static_assert(!HoldsBlock<Lookalike>);
static_assert(std::is_same_v<decltype(fe::testing::init().alloc), fe::cap::Alloc>);
static_assert(std::is_same_v<decltype(fe::testing::init().block), fe::cap::Block>);
static_assert(std::is_same_v<decltype(fe::testing::bg().block), fe::cap::Block>);

// The row recognizer strips cv and reference before it matches, so a
// forwarded row is still a row, and a forwarded non-row is still refused.
static_assert(fe::IsEffectRow<fe::Row<fe::Effect::Bg> const&>);
static_assert(fe::IsEffectRow<fe::Row<>&&>);
static_assert(!fe::IsEffectRow<int&&>);
static_assert(!fe::IsEffectRow<fe::Effect const&>);

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

// The three projections between a row and a mask take a row and nothing
// else: not a bare atom, not the mask itself, and not a plain type.
template <class R>
concept ProjectsToMask = requires { fe::bits_from_row<R>(); };
template <class R>
concept ChecksMaskAgainstRow = requires(fe::EffectMask sample) { fe::row_subsumes_bits<R>(sample); };
template <class R>
concept ChecksRowAgainstMask = requires(fe::EffectMask sample) { fe::bits_subsumes_row<R>(sample); };

template <class R>
inline constexpr int projections_taking =
    int{ProjectsToMask<R>} + int{ChecksMaskAgainstRow<R>} + int{ChecksRowAgainstMask<R>};

static_assert(projections_taking<fe::Row<>> == 3 && projections_taking<fe::Row<fe::Effect::Bg, fe::Effect::IO>> == 3);
static_assert(projections_taking<int> == 0 && projections_taking<fe::Effect> == 0
              && projections_taking<fe::EffectMask> == 0 && projections_taking<RowProbe<fe::Effect::Bg>> == 0);

// The tag type is the entire gate: no other parameter admits a call.
[[nodiscard]] int with_alloc(fe::cap::Alloc) noexcept { return 42; }

// Every accessor called with a non-constant argument.  The
// static_assert walls only prove the constant-evaluated path.
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

// The rest of Row.h is consteval-only.  This body is the one place the
// row types are instantiated as runtime objects, so a canonicalization
// change that dragged in a non-trivial default constructor fails here.
void row_types_run_at_run_time() {
    using namespace ::foundation::effects;
    using namespace ::foundation::effects::detail::effect_row_self_test;
    [[maybe_unused]] Row<Effect::Bg> r_bg{};
    [[maybe_unused]] Row<Effect::Bg, Effect::IO> r_bg_io{};
    [[maybe_unused]] EmptyRow r_empty{};

    [[maybe_unused]] auto sz1 = sizeof(r_bg);
    [[maybe_unused]] auto sz2 = sizeof(r_empty);

    [[maybe_unused]] std::size_t n_bg = decltype(r_bg)::size;
    [[maybe_unused]] std::size_t n_bg_io = decltype(r_bg_io)::size;
    [[maybe_unused]] std::size_t n_empty = EmptyRow::size;
}

// Every accessor is called here with non-constant arguments.  The
// static_assert wall in the header only proves the constant-evaluated
// path.
void effect_row_lattice_runs_at_run_time() noexcept {
    using namespace ::foundation::effects;
    using L = EffectRowLattice;
    using EL = L::element_type;

    [[maybe_unused]] EL b = L::bottom();
    [[maybe_unused]] EL t = L::top();

    [[maybe_unused]] bool ok_le = L::leq(b, t);
    [[maybe_unused]] EL u = L::join(b, t);
    [[maybe_unused]] EL i = L::meet(b, t);

    [[maybe_unused]] auto nm = ::foundation::algebra::lattice_name<L>();

    [[maybe_unused]] EL d_pure = row_descriptor_v<Row<>>;
    [[maybe_unused]] EL d_alloc = row_descriptor_v<Row<Effect::Alloc>>;

    Effect atom = ok_le ? Effect::IO : Effect::Alloc;  // deliberately not constexpr
    [[maybe_unused]] EL one = L::single(atom);
    [[maybe_unused]] bool holds = L::contains(L::join(d_alloc, one), atom);

    using AtAlloc = L::template At<Effect::Alloc>;
    using AtAB = L::template At<Effect::Alloc, Effect::IO>;

    [[maybe_unused]] auto at_b = AtAlloc::bottom();
    [[maybe_unused]] auto at_t = AtAlloc::top();
    [[maybe_unused]] bool at_le = AtAlloc::leq(at_b, at_t);
    [[maybe_unused]] auto at_join = AtAlloc::join(at_b, at_t);
    [[maybe_unused]] auto at_meet = AtAlloc::meet(at_b, at_t);
    [[maybe_unused]] auto at_nm = AtAlloc::name();
    [[maybe_unused]] std::uint64_t at_bits = AtAlloc::bits();
    [[maybe_unused]] std::uint64_t ab_bits = AtAB::bits();
}

}  // namespace

namespace {

// The mask operations called with values the optimizer cannot fold.
// The wire value comes back through a volatile load, as a received byte
// string would.
[[nodiscard]] bool effect_mask_runs_at_run_time() noexcept {
    using namespace ::foundation::effects;
    auto const bg = bits_for<Effect::Bg>();
    if (bg != bits_from_row<Row<Effect::Bg>>()) return false;

    auto const multi = bits_from_row<Row<Effect::Bg, Effect::Alloc, Effect::IO>>();
    if (multi.popcount() != 3 || !multi.test(Effect::Bg) || !multi.test(Effect::Alloc) || !multi.test(Effect::IO)) {
        return false;
    }

    using R_bg = Row<Effect::Bg>;
    if (row_subsumes_bits<R_bg>(bits_for<Effect::Bg, Effect::IO>())) return false;
    if (!row_subsumes_bits<R_bg>(bg)) return false;

    using R_bg_alloc = Row<Effect::Bg, Effect::Alloc>;
    if (bits_subsumes_row<R_bg_alloc>(bg)) return false;
    auto const full = bits_for<Effect::Bg, Effect::Alloc>();
    if (!bits_subsumes_row<R_bg_alloc>(full)) return false;

    volatile EffectMask::underlying_type wire = full.raw();
    if (EffectMask::from_raw(wire) != full) return false;

    // The complement of a received mask stays inside the valid bits.
    return ((~EffectMask::from_raw(wire)).raw() & ~EffectRowLattice::top()) == 0;
}

}  // namespace

int main() {
    if (!effect_mask_runs_at_run_time()) return 5;
    every_accessor_runs_at_run_time();
    row_types_run_at_run_time();
    effect_row_lattice_runs_at_run_time();

    auto bg = fe::testing::bg();
    auto init = fe::testing::init();
    auto test = fe::testing::test();
    [[maybe_unused]] fe::cap::IO io_bg = bg.io;
    [[maybe_unused]] fe::cap::Block blk_bg = bg.block;
    [[maybe_unused]] fe::cap::Alloc a_init = init.alloc;
    [[maybe_unused]] fe::cap::IO io_init = init.io;
    [[maybe_unused]] fe::cap::Block blk_init = init.block;
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
