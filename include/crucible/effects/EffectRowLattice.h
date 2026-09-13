#pragma once

// The carrier is a bitmask rather than type-level set algebra because
// the grading substrate that consumes this lattice checks `leq` at
// runtime inside an enforced precondition.  A consteval-only comparison
// would not instantiate there.  Every operation here is a single
// bitwise primitive with identical compile-time and runtime meaning.
//
// `row_descriptor_v` is a one-way projection.  Code that must preserve
// the Effect pack itself keeps naming `Row<Es...>`.

#include <crucible/algebra/Lattice.h>

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::effects {

static_assert(effect_count <= 64, "The EffectRowLattice carrier is uint64_t.  Extend it to a wider carrier "
                                  "(uint128 or std::bitset<effect_count>) before adding a 65th Effect atom.");

// The cardinality guard above is not enough.  Atom values are assigned
// by hand, so a pack of seven atoms could still spell one of them 100
// and shift past the carrier width.  This check reads the underlying
// value of every atom instead of counting them.
[[nodiscard]] consteval bool every_effect_underlying_lt_64() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^Effect));
    // -Wshadow fires spuriously on the expansion-statement induction variable.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (static_cast<std::uint64_t>([:en:]) >= 64) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_effect_underlying_lt_64(),
              "At least one Effect atom has an underlying value >= 64, so the "
              "EffectRowLattice::At<...>::value bit-shift is undefined.  Give every atom the next free "
              "position below 64, or migrate the carrier first.");

struct EffectRowLattice {
    using element_type = std::uint64_t;

    [[nodiscard]] static constexpr element_type bottom() noexcept { return 0; }

    [[nodiscard]] static constexpr element_type top() noexcept {
        return (effect_count == 0) ? element_type{0} : ((element_type{1} << effect_count) - element_type{1});
    }

    // Subset test.  Every bit of a is also in b exactly when a & ~b is
    // empty.
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept { return (a & ~b) == 0; }

    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept { return a | b; }

    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept { return a & b; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "EffectRow"; }

    // A singleton sub-lattice whose one element is the pinned atom set.
    // Every operation returns that element, so the lattice axioms are
    // satisfied without inspecting anything, and the grade can live in
    // the type instead of in a runtime field.
    template <Effect... Atoms>
    struct At {
        static constexpr std::uint64_t value =
            ((std::uint64_t{1} << static_cast<std::uint8_t>(Atoms)) | ... | std::uint64_t{0});

        struct element_type {
            constexpr element_type() noexcept = default;
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };
        static_assert(std::is_empty_v<element_type>,
                      "At<Atoms...>::element_type must be empty so that the grade slot of the "
                      "grading substrate collapses to 0 bytes.");

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept { return "EffectRow::At"; }

        [[nodiscard]] static constexpr std::uint64_t bits() noexcept { return value; }
    };
};

// A type carrying no row maps to the empty mask, which is below every
// descriptor.  That answer is safe rather than meaningful: such a type
// has no business being queried.
template <typename R>
inline constexpr EffectRowLattice::element_type row_descriptor_v = 0;

template <Effect... Es>
inline constexpr EffectRowLattice::element_type row_descriptor_v<Row<Es...>> =
    ((EffectRowLattice::element_type{1} << static_cast<std::uint8_t>(Es)) | ... | EffectRowLattice::element_type{0});

static_assert(::crucible::algebra::Lattice<EffectRowLattice>);
static_assert(::crucible::algebra::BoundedBelowLattice<EffectRowLattice>);
static_assert(::crucible::algebra::BoundedAboveLattice<EffectRowLattice>);
static_assert(::crucible::algebra::BoundedLattice<EffectRowLattice>);

static_assert(::crucible::algebra::Lattice<EffectRowLattice::At<>>);
static_assert(::crucible::algebra::BoundedLattice<EffectRowLattice::At<>>);
static_assert(::crucible::algebra::Lattice<EffectRowLattice::At<Effect::Alloc>>);
static_assert(::crucible::algebra::BoundedLattice<EffectRowLattice::At<Effect::Alloc>>);
static_assert(::crucible::algebra::Lattice<EffectRowLattice::At<Effect::Bg, Effect::Alloc, Effect::IO>>);
static_assert(::crucible::algebra::BoundedLattice<EffectRowLattice::At<Effect::Bg, Effect::Alloc, Effect::IO>>);

static_assert(std::is_empty_v<EffectRowLattice::At<>::element_type>);
static_assert(std::is_empty_v<EffectRowLattice::At<Effect::Alloc>::element_type>);
static_assert(std::is_empty_v<EffectRowLattice::At<Effect::Alloc, Effect::IO>::element_type>);

static_assert(EffectRowLattice::At<>::bits() == 0);
static_assert(EffectRowLattice::At<Effect::Alloc>::bits()
              == (std::uint64_t{1} << static_cast<std::uint8_t>(Effect::Alloc)));
static_assert(EffectRowLattice::At<Effect::IO>::bits() == (std::uint64_t{1} << static_cast<std::uint8_t>(Effect::IO)));
static_assert(EffectRowLattice::At<Effect::Alloc, Effect::IO>::bits()
              == EffectRowLattice::join(EffectRowLattice::At<Effect::Alloc>::bits(),
                                        EffectRowLattice::At<Effect::IO>::bits()));

static_assert(EffectRowLattice::At<>::bits() == row_descriptor_v<Row<>>);
static_assert(EffectRowLattice::At<Effect::Alloc>::bits() == row_descriptor_v<Row<Effect::Alloc>>);
static_assert(EffectRowLattice::At<Effect::Alloc, Effect::IO>::bits()
              == row_descriptor_v<Row<Effect::Alloc, Effect::IO>>);
static_assert(EffectRowLattice::At<Effect::Alloc, Effect::IO>::bits()
              == EffectRowLattice::At<Effect::IO, Effect::Alloc>::bits());

namespace detail {
// The primary template is left undefined so that a non-Row argument
// fails as an incomplete type at the alias.  A defined fallback would
// hand the caller a grade that silently means nothing.
template <typename R>
struct effect_row_to_at;

template <Effect... Es>
struct effect_row_to_at<Row<Es...>> {
    using type = EffectRowLattice::template At<Es...>;
};
}  // namespace detail

template <typename R>
using effect_row_to_at_t = typename detail::effect_row_to_at<R>::type;

static_assert(std::is_same_v<effect_row_to_at_t<Row<>>, EffectRowLattice::At<>>);
static_assert(std::is_same_v<effect_row_to_at_t<Row<Effect::Alloc>>, EffectRowLattice::At<Effect::Alloc>>);
static_assert(std::is_same_v<effect_row_to_at_t<Row<Effect::Bg>>, EffectRowLattice::At<Effect::Bg>>);
static_assert(std::is_same_v<effect_row_to_at_t<Row<Effect::Alloc, Effect::IO>>,
                             EffectRowLattice::At<Effect::Alloc, Effect::IO>>);
static_assert(std::is_same_v<
              effect_row_to_at_t<Row<Effect::Alloc, Effect::IO, Effect::Block, Effect::Bg, Effect::Init, Effect::Test>>,
              EffectRowLattice::At<Effect::Alloc, Effect::IO, Effect::Block, Effect::Bg, Effect::Init, Effect::Test>>);

static_assert(effect_row_to_at_t<Row<>>::bits() == row_descriptor_v<Row<>>);
static_assert(effect_row_to_at_t<Row<Effect::Alloc>>::bits() == row_descriptor_v<Row<Effect::Alloc>>);
static_assert(effect_row_to_at_t<Row<Effect::Alloc, Effect::IO>>::bits()
              == row_descriptor_v<Row<Effect::Alloc, Effect::IO>>);

namespace detail::effect_row_lattice_self_test {

using L = EffectRowLattice;
using EL = L::element_type;

inline constexpr EL e_bot = L::bottom();
inline constexpr EL e_alloc = EL{1} << static_cast<std::uint8_t>(Effect::Alloc);
inline constexpr EL e_io = EL{1} << static_cast<std::uint8_t>(Effect::IO);
inline constexpr EL e_block = EL{1} << static_cast<std::uint8_t>(Effect::Block);
inline constexpr EL e_bg = EL{1} << static_cast<std::uint8_t>(Effect::Bg);
inline constexpr EL e_init = EL{1} << static_cast<std::uint8_t>(Effect::Init);
inline constexpr EL e_test = EL{1} << static_cast<std::uint8_t>(Effect::Test);
inline constexpr EL e_top = L::top();

static_assert(L::leq(e_bot, e_top));
static_assert(!L::leq(e_top, e_bot));

static_assert(L::leq(e_bot, e_alloc));
static_assert(L::leq(e_alloc, e_top));
static_assert(!L::leq(e_alloc, e_io));
static_assert(!L::leq(e_io, e_alloc));

static_assert(L::join(e_alloc, e_io) == (e_alloc | e_io));
static_assert(L::join(e_alloc, e_bot) == e_alloc);
static_assert(L::join(e_alloc, e_top) == e_top);

static_assert(L::meet(e_alloc | e_io, e_io | e_block) == e_io);
static_assert(L::meet(e_alloc, e_bot) == e_bot);
static_assert(L::meet(e_alloc, e_top) == e_alloc);

static_assert((e_top & e_alloc) == e_alloc);
static_assert((e_top & e_io) == e_io);
static_assert((e_top & e_block) == e_block);
static_assert((e_top & e_bg) == e_bg);
static_assert((e_top & e_init) == e_init);
static_assert((e_top & e_test) == e_test);

static_assert(L::top() == ((EL{1} << effect_count) - EL{1}));

static_assert(::crucible::algebra::verify_bounded_lattice_axioms_at<L>(e_bot, e_bot, e_bot));
static_assert(::crucible::algebra::verify_bounded_lattice_axioms_at<L>(e_bot, e_alloc, e_alloc | e_io));
static_assert(::crucible::algebra::verify_bounded_lattice_axioms_at<L>(e_alloc, e_io, e_block));
static_assert(::crucible::algebra::verify_bounded_lattice_axioms_at<L>(e_alloc | e_io, e_io | e_block, e_block | e_bg));
static_assert(::crucible::algebra::verify_bounded_lattice_axioms_at<L>(e_bot, e_alloc, e_top));
static_assert(::crucible::algebra::verify_bounded_lattice_axioms_at<L>(e_top, e_top, e_top));

static_assert(::crucible::algebra::verify_distributive_lattice<L>(e_alloc, e_io, e_block));
static_assert(::crucible::algebra::verify_distributive_lattice<L>(e_alloc | e_io, e_io | e_block, e_block | e_bg));
static_assert(::crucible::algebra::verify_distributive_lattice<L>(e_bot, e_alloc, e_top));

static_assert(row_descriptor_v<Row<>> == e_bot);
static_assert(row_descriptor_v<Row<Effect::Alloc>> == e_alloc);
static_assert(row_descriptor_v<Row<Effect::IO>> == e_io);
static_assert(row_descriptor_v<Row<Effect::Block>> == e_block);
static_assert(row_descriptor_v<Row<Effect::Bg>> == e_bg);
static_assert(row_descriptor_v<Row<Effect::Init>> == e_init);
static_assert(row_descriptor_v<Row<Effect::Test>> == e_test);

static_assert(row_descriptor_v<Row<Effect::Alloc, Effect::IO>> == (e_alloc | e_io));
static_assert(row_descriptor_v<Row<Effect::Block, Effect::Alloc, Effect::IO>> == (e_block | e_alloc | e_io));

static_assert(row_descriptor_v<Row<Effect::Alloc, Effect::IO>> == row_descriptor_v<Row<Effect::IO, Effect::Alloc>>);

static_assert(row_descriptor_v<Row<Effect::Alloc, Effect::Alloc>> == row_descriptor_v<Row<Effect::Alloc>>);

static_assert(row_descriptor_v<Row<Effect::Alloc, Effect::IO, Effect::Block, Effect::Bg, Effect::Init, Effect::Test>>
              == e_top);

static_assert(row_descriptor_v<int> == 0);
static_assert(row_descriptor_v<double> == 0);

// The membership-based subrow test and the bitmask-based order must
// give the same answer at every query site.  A disagreement would let
// the two surfaces classify the same pair of rows differently.
static_assert(L::leq(row_descriptor_v<Row<>>, row_descriptor_v<Row<Effect::Alloc>>)
              == is_subrow_v<Row<>, Row<Effect::Alloc>>);

static_assert(L::leq(row_descriptor_v<Row<Effect::Alloc>>, row_descriptor_v<Row<Effect::Alloc, Effect::IO>>)
              == is_subrow_v<Row<Effect::Alloc>, Row<Effect::Alloc, Effect::IO>>);

static_assert(L::leq(row_descriptor_v<Row<Effect::Alloc, Effect::IO>>, row_descriptor_v<Row<Effect::Alloc>>)
              == is_subrow_v<Row<Effect::Alloc, Effect::IO>, Row<Effect::Alloc>>);

static_assert(L::leq(row_descriptor_v<Row<Effect::IO>>, row_descriptor_v<Row<Effect::Alloc>>)
              == is_subrow_v<Row<Effect::IO>, Row<Effect::Alloc>>);

}  // namespace detail::effect_row_lattice_self_test

// Every accessor is called here with non-constant arguments.  The
// static_assert wall above only proves the constant-evaluated path.
inline void runtime_smoke_test_lattice() noexcept {
    using L = EffectRowLattice;
    using EL = L::element_type;

    [[maybe_unused]] EL b = L::bottom();
    [[maybe_unused]] EL t = L::top();

    [[maybe_unused]] bool ok_le = L::leq(b, t);
    [[maybe_unused]] EL u = L::join(b, t);
    [[maybe_unused]] EL i = L::meet(b, t);

    [[maybe_unused]] auto nm = ::crucible::algebra::lattice_name<L>();

    [[maybe_unused]] EL d_pure = row_descriptor_v<Row<>>;
    [[maybe_unused]] EL d_alloc = row_descriptor_v<Row<Effect::Alloc>>;

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

}  // namespace crucible::effects
