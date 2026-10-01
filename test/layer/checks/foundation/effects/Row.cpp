// The compile-time checks of foundation/effects/Row.h.

#include <foundation/effects/Row.h>

namespace foundation::effects {

static_assert(row_size(^^every_effect_row) == effect_count,
              "every_effect_row must hold one atom for each Effect enumerator.  It is substituted from "
              "enumerators_of, so a mismatch means a duplicate enumerator value collapsed two atoms into "
              "one row bit.");

namespace detail::effect_row_self_test {

using R_empty = Row<>;
using R_alloc = Row<Effect::Alloc>;
using R_io = Row<Effect::IO>;
using R_alloc_io = Row<Effect::Alloc, Effect::IO>;
using R_alloc_io_bg = Row<Effect::Alloc, Effect::IO, Effect::Bg>;

static_assert(row_size(^^R_empty) == 0);
static_assert(row_size(^^R_alloc) == 1);
static_assert(row_size(^^R_alloc_io) == 2);
static_assert(row_size(^^R_alloc_io_bg) == 3);

static_assert(!row_contains(^^R_empty, Effect::Alloc));
static_assert(row_contains(^^R_alloc, Effect::Alloc));
static_assert(!row_contains(^^R_alloc, Effect::IO));
static_assert(row_contains(^^R_alloc_io, Effect::Alloc));
static_assert(row_contains(^^R_alloc_io, Effect::IO));
static_assert(!row_contains(^^R_alloc_io, Effect::Bg));
static_assert(!row_contains(^^int, Effect::Alloc), "a type that is not a row names no effect");

static_assert(is_subrow(^^R_empty, ^^R_empty));
static_assert(is_subrow(^^R_empty, ^^R_alloc));
static_assert(is_subrow(^^R_alloc, ^^R_alloc_io));
static_assert(!is_subrow(^^R_alloc_io, ^^R_alloc));
static_assert(is_subrow(^^R_alloc_io, ^^R_alloc_io_bg));
static_assert(!is_subrow(^^R_io, ^^R_alloc));
static_assert(!is_subrow(^^int, ^^R_alloc) && !is_subrow(^^R_empty, ^^int), "a type that is not a row is no subrow");

static_assert(Subrow<R_empty, R_alloc_io>);
static_assert(Subrow<R_alloc, R_alloc_io_bg>);
static_assert(!Subrow<R_alloc_io, R_alloc>);

static_assert(std::is_same_v<row_union_t<R_empty, R_empty>, R_empty>);
static_assert(std::is_same_v<row_union_t<R_alloc_io, R_empty>, R_alloc_io>);
static_assert(is_subrow(^^R_alloc_io, ^^row_union_t<R_empty, R_alloc_io>));
static_assert(is_subrow(^^row_union_t<R_empty, R_alloc_io>, ^^R_alloc_io));

using R_union_a_io = row_union_t<R_alloc, R_io>;
static_assert(is_subrow(^^R_alloc, ^^R_union_a_io));
static_assert(is_subrow(^^R_io, ^^R_union_a_io));
static_assert(row_size(^^R_union_a_io) == 2);

using R_union_dup = row_union_t<R_alloc_io, R_alloc>;
static_assert(row_size(^^R_union_dup) == 2);
static_assert(is_subrow(^^R_alloc_io, ^^R_union_dup));
static_assert(is_subrow(^^R_alloc, ^^R_union_dup));
static_assert(!row_contains(^^R_union_dup, Effect::Bg));

using R_left = row_union_t<R_alloc, R_io>;
using R_right = row_union_t<R_io, R_alloc>;
static_assert(is_subrow(^^R_left, ^^R_right));
static_assert(is_subrow(^^R_right, ^^R_left));

using R_lr_then_bg = row_union_t<row_union_t<R_alloc, R_io>, Row<Effect::Bg>>;
using R_lr_then_bg_alt = row_union_t<R_alloc, row_union_t<R_io, Row<Effect::Bg>>>;
static_assert(is_subrow(^^R_lr_then_bg, ^^R_lr_then_bg_alt));
static_assert(is_subrow(^^R_lr_then_bg_alt, ^^R_lr_then_bg));
static_assert(row_size(^^R_lr_then_bg) == 3);

static_assert(std::is_same_v<row_difference_t<R_alloc_io, R_empty>, R_alloc_io>);
static_assert(row_size(^^row_difference_t<R_alloc_io, R_alloc_io>) == 0);
static_assert(std::is_same_v<row_difference_t<R_alloc_io, R_alloc_io>, R_empty>);

using R_diff = row_difference_t<R_alloc_io_bg, R_alloc>;
static_assert(row_size(^^R_diff) == 2);
static_assert(!row_contains(^^R_diff, Effect::Alloc));
static_assert(row_contains(^^R_diff, Effect::IO));
static_assert(row_contains(^^R_diff, Effect::Bg));

static_assert(row_size(^^row_intersection_t<R_empty, R_alloc_io>) == 0);
static_assert(std::is_same_v<row_intersection_t<R_alloc_io, R_alloc_io>, R_alloc_io>);

using R_inter = row_intersection_t<R_alloc_io, R_alloc_io_bg>;
static_assert(is_subrow(^^R_inter, ^^R_alloc_io));
static_assert(is_subrow(^^R_alloc_io, ^^R_inter));
static_assert(row_size(^^R_inter) == 2);

using R_inter_disjoint = row_intersection_t<R_alloc, R_io>;
static_assert(row_size(^^R_inter_disjoint) == 0);

using R_universe = every_effect_row;
static_assert(row_size(^^R_universe) == effect_count);
static_assert(is_subrow(^^R_alloc_io, ^^R_universe));
static_assert(is_subrow(^^R_alloc_io_bg, ^^R_universe));

static_assert(row_size(^^row_difference_t<R_universe, R_universe>) == 0);
static_assert(row_size(^^row_intersection_t<R_universe, R_empty>) == 0);

// The expected packs below are in sorted underlying-value order:
// Alloc, IO, Block, Bg, Init, Test.
static_assert(std::is_same_v<canonical_row_t<Row<>>, Row<>>);

static_assert(std::is_same_v<canonical_row_t<Row<Effect::Bg>>, Row<Effect::Bg>>);

static_assert(std::is_same_v<canonical_row_t<Row<Effect::Alloc, Effect::IO>>, Row<Effect::Alloc, Effect::IO>>);

static_assert(std::is_same_v<canonical_row_t<Row<Effect::IO, Effect::Alloc>>, Row<Effect::Alloc, Effect::IO>>);

static_assert(std::is_same_v<canonical_row_t<Row<Effect::Bg, Effect::IO, Effect::Bg>>, Row<Effect::IO, Effect::Bg>>);

static_assert(std::is_same_v<canonical_row_t<Row<Effect::IO, Effect::IO, Effect::IO>>, Row<Effect::IO>>);

static_assert(std::is_same_v<canonical_row_t<Row<Effect::Bg, Effect::IO, Effect::Bg, Effect::Block>>,
                             Row<Effect::IO, Effect::Block, Effect::Bg>>);

using R_canon_once = canonical_row_t<Row<Effect::Bg, Effect::Alloc, Effect::Bg>>;
using R_canon_twice = canonical_row_t<R_canon_once>;
static_assert(std::is_same_v<R_canon_once, R_canon_twice>);
static_assert(std::is_same_v<R_canon_once, Row<Effect::Alloc, Effect::Bg>>);

static_assert(row_size(^^canonical_row_t<Row<Effect::Bg, Effect::IO, Effect::Bg>>) == 2);
static_assert(row_size(^^canonical_row_t<Row<Effect::IO, Effect::IO, Effect::IO>>) == 1);
static_assert(row_size(^^canonical_row_t<Row<>>) == 0);

namespace cardinality_lens_witness {

using R_dup = Row<Effect::Bg, Effect::IO, Effect::Bg>;

static_assert(row_pack_size_v<R_dup> == 3);
static_assert(row_size(^^R_dup) == 3);

static_assert(row_unique_size_v<R_dup> == 2);

// The row hash is set-semantic, so it counts what
// `row_unique_size_v` counts and needs no trait of its own.
static_assert(row_pack_size_v<canonical_row_t<R_dup>> == row_unique_size_v<R_dup>);

using R_canon = canonical_row_t<R_dup>;
static_assert(row_pack_size_v<R_canon> == 2);
static_assert(row_unique_size_v<R_canon> == 2);

static_assert(row_pack_size_v<Row<>> == 0);
static_assert(row_unique_size_v<Row<>> == 0);
static_assert(row_size(^^Row<>) == 0);

}  // namespace cardinality_lens_witness

static_assert(std::is_same_v<row_union_t<Row<Effect::Bg, Effect::IO, Effect::Bg>, Row<Effect::Block>>,
                             Row<Effect::IO, Effect::Block, Effect::Bg>>);

static_assert(std::is_same_v<row_union_t<R_alloc, R_io>, row_union_t<R_io, R_alloc>>);

static_assert(std::is_same_v<R_lr_then_bg, R_lr_then_bg_alt>);

static_assert(std::is_same_v<row_difference_t<Row<Effect::Bg, Effect::IO, Effect::Bg>, Row<Effect::Block>>,
                             Row<Effect::IO, Effect::Bg>>);

static_assert(std::is_same_v<row_intersection_t<Row<Effect::Bg, Effect::IO>, Row<Effect::IO, Effect::Bg>>,
                             Row<Effect::IO, Effect::Bg>>);

static_assert(std::is_same_v<row_union_t<Row<Effect::Test, Effect::Alloc>, Row<Effect::Bg>>,
                             row_union_t<Row<Effect::Bg>, Row<Effect::Alloc, Effect::Test>>>);

}  // namespace detail::effect_row_self_test

static_assert(effect_count <= 64, "The EffectRowLattice carrier is uint64_t.  Extend it to a wider carrier "
                                  "(uint128 or std::bitset<effect_count>) before adding a 65th Effect atom.");

static_assert(every_effect_underlying_lt_64(),
              "At least one Effect atom has an underlying value >= 64, so the "
              "EffectRowLattice::At<...>::value bit-shift is undefined.  Give every atom the next free "
              "position below 64, or migrate the carrier first.");

static_assert(::foundation::algebra::Lattice<EffectRowLattice>);
static_assert(::foundation::algebra::Row<EffectRowLattice>, "EffectRowLattice is the first Row; if this fires, "
                                                            "atom_type, single or contains has drifted.");
static_assert(::foundation::algebra::BoundedBelowLattice<EffectRowLattice>);
static_assert(::foundation::algebra::BoundedAboveLattice<EffectRowLattice>);
static_assert(::foundation::algebra::BoundedLattice<EffectRowLattice>);

static_assert(::foundation::algebra::Lattice<EffectRowLattice::At<>>);
static_assert(::foundation::algebra::BoundedLattice<EffectRowLattice::At<>>);
static_assert(::foundation::algebra::Lattice<EffectRowLattice::At<Effect::Alloc>>);
static_assert(::foundation::algebra::BoundedLattice<EffectRowLattice::At<Effect::Alloc>>);
static_assert(::foundation::algebra::Lattice<EffectRowLattice::At<Effect::Bg, Effect::Alloc, Effect::IO>>);
static_assert(::foundation::algebra::BoundedLattice<EffectRowLattice::At<Effect::Bg, Effect::Alloc, Effect::IO>>);

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

static_assert(std::is_same_v<effect_row_to_at_t<Row<>>, EffectRowLattice::At<>>);
static_assert(std::is_same_v<effect_row_to_at_t<Row<Effect::Alloc>>, EffectRowLattice::At<Effect::Alloc>>);
static_assert(std::is_same_v<effect_row_to_at_t<Row<Effect::Bg>>, EffectRowLattice::At<Effect::Bg>>);
static_assert(std::is_same_v<effect_row_to_at_t<Row<Effect::Alloc, Effect::IO>>,
                             EffectRowLattice::At<Effect::Alloc, Effect::IO>>);
// Both sides come from the same walk over the enum, so what this pins
// is that the conversion carries every atom across rather than that two
// hand-written lists agree.
static_assert(
    std::is_same_v<effect_row_to_at_t<every_effect_row>, typename[:detail::every_effect_of_(^^EffectRowLattice::At):]>);

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

static_assert(::foundation::algebra::verify_bounded_lattice_axioms_at<L>(e_bot, e_bot, e_bot));
static_assert(::foundation::algebra::verify_bounded_lattice_axioms_at<L>(e_bot, e_alloc, e_alloc | e_io));
static_assert(::foundation::algebra::verify_bounded_lattice_axioms_at<L>(e_alloc, e_io, e_block));
static_assert(::foundation::algebra::verify_bounded_lattice_axioms_at<L>(e_alloc | e_io, e_io | e_block,
                                                                         e_block | e_bg));
static_assert(::foundation::algebra::verify_bounded_lattice_axioms_at<L>(e_bot, e_alloc, e_top));
static_assert(::foundation::algebra::verify_bounded_lattice_axioms_at<L>(e_top, e_top, e_top));

static_assert(::foundation::algebra::verify_distributive_lattice<L>(e_alloc, e_io, e_block));
static_assert(::foundation::algebra::verify_distributive_lattice<L>(e_alloc | e_io, e_io | e_block, e_block | e_bg));
static_assert(::foundation::algebra::verify_distributive_lattice<L>(e_bot, e_alloc, e_top));

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

static_assert(row_descriptor_v<every_effect_row> == e_top);

static_assert(row_descriptor_v<int> == 0);
static_assert(row_descriptor_v<double> == 0);

static_assert(L::single(Effect::Alloc) == e_alloc);
static_assert(L::single(Effect::Test) == e_test);
static_assert(L::contains(row_descriptor_v<Row<Effect::Alloc, Effect::IO>>, Effect::IO));
static_assert(!L::contains(row_descriptor_v<Row<Effect::Alloc, Effect::IO>>, Effect::Bg));
static_assert(L::contains(e_top, Effect::Init));
static_assert(!L::contains(e_bot, Effect::Init));
static_assert(L::contains(row_descriptor_v<Row<Effect::Bg>>, Effect::Bg)
              == row_contains(^^Row<Effect::Bg>, Effect::Bg));
static_assert(L::contains(row_descriptor_v<Row<Effect::Bg>>, Effect::IO)
              == row_contains(^^Row<Effect::Bg>, Effect::IO));

// The membership-based subrow test and the bitmask-based order must
// give the same answer at every query site.  A disagreement would let
// the two surfaces classify the same pair of rows differently.
static_assert(L::leq(row_descriptor_v<Row<>>, row_descriptor_v<Row<Effect::Alloc>>)
              == is_subrow(^^Row<>, ^^Row<Effect::Alloc>));

static_assert(L::leq(row_descriptor_v<Row<Effect::Alloc>>, row_descriptor_v<Row<Effect::Alloc, Effect::IO>>)
              == is_subrow(^^Row<Effect::Alloc>, ^^Row<Effect::Alloc, Effect::IO>));

static_assert(L::leq(row_descriptor_v<Row<Effect::Alloc, Effect::IO>>, row_descriptor_v<Row<Effect::Alloc>>)
              == is_subrow(^^Row<Effect::Alloc, Effect::IO>, ^^Row<Effect::Alloc>));

static_assert(L::leq(row_descriptor_v<Row<Effect::IO>>, row_descriptor_v<Row<Effect::Alloc>>)
              == is_subrow(^^Row<Effect::IO>, ^^Row<Effect::Alloc>));

}  // namespace detail::effect_row_lattice_self_test

static_assert(sizeof(EffectMask) == sizeof(EffectRowLattice::element_type));
static_assert(std::is_trivially_copyable_v<EffectMask>);
static_assert(std::is_standard_layout_v<EffectMask>);

namespace detail::effect_row_projection_self_test {

static_assert(IsEffectRow<Row<>>);
static_assert(IsEffectRow<Row<Effect::Bg>>);
static_assert(IsEffectRow<Row<Effect::Bg, Effect::Alloc>>);
static_assert(!IsEffectRow<int>);
static_assert(!IsEffectRow<Effect>);
static_assert(!IsEffectRow<EffectMask>);

static_assert(bits_for<>().none());
static_assert(bits_for<>().popcount() == 0);

static_assert(bits_for<Effect::Alloc>().raw() == (1u << 0));
static_assert(bits_for<Effect::IO>().raw() == (1u << 1));
static_assert(bits_for<Effect::Bg>().raw() == (1u << 3));
static_assert(bits_for<Effect::Test>().raw() == (1u << 5));

static_assert(bits_for<Effect::Bg>().test(Effect::Bg));
static_assert(!bits_for<Effect::Bg>().test(Effect::Alloc));
static_assert(bits_for<Effect::Bg>().popcount() == 1);

static_assert(bits_for<Effect::Bg, Effect::Alloc>().test(Effect::Bg));
static_assert(bits_for<Effect::Bg, Effect::Alloc>().test(Effect::Alloc));
static_assert(bits_for<Effect::Bg, Effect::Alloc>().popcount() == 2);
static_assert(bits_for<Effect::Bg, Effect::Alloc>().raw() == ((1u << 3) | (1u << 0)));

static_assert(bits_from_row<Row<>>().none());
static_assert(bits_from_row<Row<Effect::IO>>().test(Effect::IO));
static_assert(bits_from_row<Row<Effect::IO>>().popcount() == 1);

static_assert(bits_from_row<Row<Effect::Bg, Effect::Alloc>>() == bits_from_row<Row<Effect::Alloc, Effect::Bg>>(),
              "bits_from_row must not depend on the order of the row.  Bitwise OR is commutative, and "
              "federation peers that project two orders of one row must compute one cache key.");

// The mask and the row descriptor are one encoding.
static_assert(bits_from_row<Row<>>().raw() == row_descriptor_v<Row<>>);
static_assert(bits_from_row<Row<Effect::Bg, Effect::Alloc>>().raw()
              == row_descriptor_v<Row<Effect::Bg, Effect::Alloc>>);
static_assert(bits_from_row<every_effect_row>().raw() == EffectRowLattice::top());
static_assert(bits_from_row<Row<Effect::IO> const&>() == bits_from_row<Row<Effect::IO>>());

static_assert(EffectMask::from_raw(0x0B).raw() == 0x0B);

static_assert(EffectMask::from_raw(0x3F).raw() == 0x3F);
static_assert(EffectMask::from_raw(0x00).raw() == 0x00);
static_assert(EffectMask::from_raw(0x20).raw() == 0x20);
static_assert(EffectMask::from_raw(0x01).raw() == 0x01);

// The complement never leaves the valid mask, and applying it twice
// gives the operand back.
static_assert((~EffectMask{}).raw() == EffectRowLattice::top());
static_assert((~bits_for<Effect::Bg>()).raw() == (EffectRowLattice::top() & ~EffectRowLattice::single(Effect::Bg)));
static_assert(~~bits_for<Effect::Bg, Effect::IO>() == bits_for<Effect::Bg, Effect::IO>());
static_assert(~bits_from_row<every_effect_row>() == EffectMask{});

[[nodiscard]] consteval bool drift_detection() noexcept {
    using R_bg_only = Row<Effect::Bg>;
    auto sample_bg_alloc = bits_for<Effect::Bg, Effect::Alloc>();
    if (row_subsumes_bits<R_bg_only>(sample_bg_alloc)) return false;
    auto sample_bg_only = bits_for<Effect::Bg>();
    if (!row_subsumes_bits<R_bg_only>(sample_bg_only)) return false;
    if (!row_subsumes_bits<R_bg_only>(EffectMask{})) return false;
    return true;
}
static_assert(drift_detection());

[[nodiscard]] consteval bool inverse_subsumption() noexcept {
    using R_bg_alloc = Row<Effect::Bg, Effect::Alloc>;
    auto sample_full = bits_for<Effect::Bg, Effect::Alloc, Effect::IO>();
    if (!bits_subsumes_row<R_bg_alloc>(sample_full)) return false;
    auto sample_partial = bits_for<Effect::Bg>();
    if (bits_subsumes_row<R_bg_alloc>(sample_partial)) return false;
    return true;
}
static_assert(inverse_subsumption());

static_assert(row_subsumes_bits<Row<>>(EffectMask{}));
static_assert(!row_subsumes_bits<Row<>>(bits_for<Effect::Bg>()),
              "An empty row has no atoms, so it does not cover a sample that contains Effect::Bg.  If this "
              "fires, row_subsumes_bits has the subsumption direction inverted.");

// The run-time half of these checks lives in
// test/foundation/test_effects_core.cpp.

}  // namespace detail::effect_row_projection_self_test

}  // namespace foundation::effects
