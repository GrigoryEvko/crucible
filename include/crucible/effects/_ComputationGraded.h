#pragma once

// The effect-row carrier, expressed as one instantiation of the graded
// substrate rather than a parallel hierarchy of its own.
//
// The modality is Relative because a row carries neither a unit nor a
// counit.  A value cannot be injected into a row, since the row is
// already the typing context, and it cannot be extracted out of one
// short of closing the universe.  The comonadic and relative-monadic
// modalities exist for grades that do carry those operations.  What a
// row does carry is propagation, which `weaken` widens up the lattice.

#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/_Modality.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/effects/_EffectRowLattice.h>

#include <cstddef>
#include <type_traits>

namespace crucible::effects {

// R must be a Row.  The constraint is structural rather than a concept:
// the lift from a row to a lattice point has no primary template, so a
// non-Row argument yields an incomplete type at the instantiation point.
template <typename R, typename T>
using ComputationGraded =
    ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Relative, effect_row_to_at_t<R>, T>;

static_assert(std::is_same_v<typename ComputationGraded<Row<>, int>::value_type, int>);

static_assert(std::is_same_v<typename ComputationGraded<Row<>, int>::lattice_type, EffectRowLattice::At<>>);

static_assert(
    std::is_same_v<typename ComputationGraded<Row<Effect::Bg>, int>::lattice_type, EffectRowLattice::At<Effect::Bg>>);

static_assert(std::is_same_v<typename ComputationGraded<Row<Effect::Alloc, Effect::IO>, int>::lattice_type,
                             EffectRowLattice::At<Effect::Alloc, Effect::IO>>);

static_assert(ComputationGraded<Row<>, int>::modality == ::crucible::algebra::ModalityKind::Relative);

static_assert(ComputationGraded<Row<Effect::Bg>, double>::modality == ::crucible::algebra::ModalityKind::Relative);

static_assert(std::is_empty_v<typename ComputationGraded<Row<>, int>::grade_type>);
static_assert(std::is_empty_v<typename ComputationGraded<Row<Effect::Bg>, int>::grade_type>);
static_assert(
    std::is_empty_v<typename ComputationGraded<
        Row<Effect::Alloc, Effect::IO, Effect::Block, Effect::Bg, Effect::Init, Effect::Test>, int>::grade_type>);

// The modality and lattice names are stable spellings and can be
// compared.  The value-type name is not: it is reflection-derived and
// its text varies with translation-unit context, so no assertion here
// compares it.

static_assert(ComputationGraded<Row<>, int>::modality_name() == "Relative");
static_assert(ComputationGraded<Row<Effect::Bg>, int>::lattice_name() == "EffectRow::At");

// The row is type-level only, so the carrier collapses to the payload's
// own footprint however many atoms the row names.

namespace detail::computation_graded_layout {

struct EmptyValue {};
struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

static_assert(sizeof(ComputationGraded<Row<>, EmptyValue>) == 1);
static_assert(sizeof(ComputationGraded<Row<Effect::Bg>, EmptyValue>) == 1);

static_assert(sizeof(ComputationGraded<Row<>, int>) == sizeof(int));
static_assert(sizeof(ComputationGraded<Row<Effect::Alloc>, int>) == sizeof(int));
static_assert(sizeof(ComputationGraded<Row<Effect::Bg>, int>) == sizeof(int));
static_assert(
    sizeof(
        ComputationGraded<Row<Effect::Alloc, Effect::IO, Effect::Block, Effect::Bg, Effect::Init, Effect::Test>, int>)
    == sizeof(int));

static_assert(sizeof(ComputationGraded<Row<>, OneByteValue>) == sizeof(OneByteValue));
static_assert(sizeof(ComputationGraded<Row<Effect::Bg>, OneByteValue>) == sizeof(OneByteValue));

static_assert(sizeof(ComputationGraded<Row<>, EightByteValue>) == sizeof(EightByteValue));
static_assert(sizeof(ComputationGraded<Row<Effect::Bg>, EightByteValue>) == sizeof(EightByteValue));

static_assert(alignof(ComputationGraded<Row<>, int>) == alignof(int));
static_assert(alignof(ComputationGraded<Row<Effect::Bg>, EightByteValue>) == alignof(EightByteValue));

template <typename T>
using CompOverEmpty = ComputationGraded<Row<>, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CompOverEmpty, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CompOverEmpty, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CompOverEmpty, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CompOverEmpty, EightByteValue);

template <typename T>
using CompOverBg = ComputationGraded<Row<Effect::Bg>, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CompOverBg, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CompOverBg, EightByteValue);

template <typename T>
using CompOverAll =
    ComputationGraded<Row<Effect::Alloc, Effect::IO, Effect::Block, Effect::Bg, Effect::Init, Effect::Test>, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CompOverAll, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CompOverAll, EightByteValue);

}  // namespace detail::computation_graded_layout

// EmptyRow must stay a transparent alias.  Turning it into a distinct
// type would give it a distinct carrier, and these fire.
static_assert(std::is_same_v<ComputationGraded<EmptyRow, int>, ComputationGraded<Row<>, int>>);
static_assert(std::is_same_v<typename ComputationGraded<EmptyRow, int>::lattice_type,
                             typename ComputationGraded<Row<>, int>::lattice_type>);
static_assert(sizeof(ComputationGraded<EmptyRow, int>) == sizeof(ComputationGraded<Row<>, int>));

// The substrate publishes at_bottom only for a lattice that is bounded
// below.  The row lattice is, so the factory must reach every
// instantiation.

namespace detail::computation_graded_caps {
template <typename G>
concept HasAtBottom = requires(typename G::value_type v) { G::at_bottom(v); };
}  // namespace detail::computation_graded_caps

static_assert(detail::computation_graded_caps::HasAtBottom<ComputationGraded<Row<>, int>>);
static_assert(detail::computation_graded_caps::HasAtBottom<ComputationGraded<Row<Effect::Bg>, int>>);
static_assert(
    detail::computation_graded_caps::HasAtBottom<
        ComputationGraded<Row<Effect::Alloc, Effect::IO, Effect::Block, Effect::Bg, Effect::Init, Effect::Test>, int>>);

static_assert(std::is_default_constructible_v<ComputationGraded<Row<>, int>>);
static_assert(std::is_copy_constructible_v<ComputationGraded<Row<>, int>>);
static_assert(std::is_move_constructible_v<ComputationGraded<Row<Effect::Bg>, int>>);
static_assert(std::is_copy_assignable_v<ComputationGraded<Row<>, int>>);
static_assert(std::is_move_assignable_v<ComputationGraded<Row<Effect::Bg>, int>>);
static_assert(std::is_destructible_v<ComputationGraded<Row<>, int>>);

// Persistence copies these objects byte-wise, so the carrier must not
// cost the payload its trivial copyability.
static_assert(std::is_trivially_copyable_v<int> == std::is_trivially_copyable_v<ComputationGraded<Row<>, int>>);
static_assert(std::is_trivially_copyable_v<int>
              == std::is_trivially_copyable_v<ComputationGraded<Row<Effect::Bg>, int>>);

namespace detail::computation_graded_caps {

template <typename G>
concept HasPeekMut = requires(G& g) { g.peek_mut(); };
template <typename G>
concept HasSwap = requires(G& a, G& b) { a.swap(b); };
template <typename G>
concept HasComonadExtract = requires(G g) { std::move(g).extract(); };
template <typename G>
concept HasRelMonadInject = requires { G::inject(typename G::value_type{}, typename G::grade_type{}); };
template <typename G>
concept HasWeaken = requires(G g, typename G::grade_type r) { std::move(g).weaken(r); };
template <typename G>
concept HasCompose = requires(G g, G const& o) { std::move(g).compose(o); };

using G_pure = ComputationGraded<Row<>, int>;
using G_bg = ComputationGraded<Row<Effect::Bg>, int>;

// peek_mut and swap reach a Relative-modality carrier here because the
// substrate gates them on an absolute modality OR an empty grade, and
// the row grade is empty.
static_assert(HasPeekMut<G_pure>);
static_assert(HasPeekMut<G_bg>);
static_assert(HasSwap<G_pure>);
static_assert(HasSwap<G_bg>);

static_assert(!HasComonadExtract<G_pure>);
static_assert(!HasComonadExtract<G_bg>);
static_assert(!HasRelMonadInject<G_pure>);
static_assert(!HasRelMonadInject<G_bg>);

static_assert(HasWeaken<G_pure>);
static_assert(HasWeaken<G_bg>);
static_assert(HasCompose<G_pure>);
static_assert(HasCompose<G_bg>);

}  // namespace detail::computation_graded_caps

// Drives every accessor with non-constant arguments, where inline-body
// and constant-evaluation regressions surface that the assertions above
// cannot see.
inline void runtime_smoke_test_computation_graded() noexcept {
    using G_pure = ComputationGraded<Row<>, int>;
    using G_bg = ComputationGraded<Row<Effect::Bg>, int>;

    int value_runtime = 42;
    G_pure pure{value_runtime, G_pure::grade_type{}};
    G_bg bg{value_runtime + 1, G_bg::grade_type{}};

    G_pure pure_default{};
    G_pure pure_copy{pure};
    G_pure pure_moved{std::move(pure_copy)};

    [[maybe_unused]] int const& peeked = pure.peek();
    [[maybe_unused]] int moved = std::move(pure_default).consume();
    [[maybe_unused]] auto grade = pure.grade();

    G_pure pure_a{};
    G_pure pure_b{};
    pure_a.peek_mut() = 7;
    pure_a.swap(pure_b);

    [[maybe_unused]] auto widened = std::move(bg).weaken(G_bg::grade_type{});

    G_pure pure_c{value_runtime, G_pure::grade_type{}};
    G_pure pure_d{value_runtime + 2, G_pure::grade_type{}};
    [[maybe_unused]] G_pure composed = pure_c.compose(pure_d);

    [[maybe_unused]] G_pure pure_bot = G_pure::at_bottom(value_runtime);
    [[maybe_unused]] G_bg bg_bot = G_bg::at_bottom(value_runtime + 3);

    static_assert(detail::computation_graded_caps::HasPeekMut<G_pure>);
    static_assert(detail::computation_graded_caps::HasWeaken<G_bg>);
    static_assert(!detail::computation_graded_caps::HasComonadExtract<G_pure>);
    static_assert(!detail::computation_graded_caps::HasRelMonadInject<G_bg>);
}

}  // namespace crucible::effects
