// The compile-time checks of foundation/effects/Computation.h.

#include <foundation/effects/Computation.h>

namespace foundation::effects {

static_assert(std::is_same_v<typename ComputationGraded<Row<>, int>::value_type, int>);

static_assert(std::is_same_v<typename ComputationGraded<Row<>, int>::lattice_type, EffectRowLattice::At<>>);

static_assert(
    std::is_same_v<typename ComputationGraded<Row<Effect::Bg>, int>::lattice_type, EffectRowLattice::At<Effect::Bg>>);

static_assert(std::is_same_v<typename ComputationGraded<Row<Effect::Alloc, Effect::IO>, int>::lattice_type,
                             EffectRowLattice::At<Effect::Alloc, Effect::IO>>);

static_assert(ComputationGraded<Row<>, int>::modality == ::foundation::algebra::ModalityKind::Relative);

static_assert(ComputationGraded<Row<Effect::Bg>, double>::modality == ::foundation::algebra::ModalityKind::Relative);

static_assert(std::is_empty_v<typename ComputationGraded<Row<>, int>::grade_type>);
static_assert(std::is_empty_v<typename ComputationGraded<Row<Effect::Bg>, int>::grade_type>);
static_assert(std::is_empty_v<typename ComputationGraded<every_effect_row, int>::grade_type>);

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
static_assert(sizeof(ComputationGraded<every_effect_row, int>) == sizeof(int));

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
using CompOverAll = ComputationGraded<every_effect_row, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CompOverAll, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CompOverAll, EightByteValue);

}  // namespace detail::computation_graded_layout

// EmptyRow must stay a transparent alias.  Turning it into a distinct
// type would give it a distinct carrier, and these fire.
static_assert(std::is_same_v<ComputationGraded<EmptyRow, int>, ComputationGraded<Row<>, int>>);
static_assert(std::is_same_v<typename ComputationGraded<EmptyRow, int>::lattice_type,
                             typename ComputationGraded<Row<>, int>::lattice_type>);
static_assert(sizeof(ComputationGraded<EmptyRow, int>) == sizeof(ComputationGraded<Row<>, int>));

static_assert(!detail::computation_graded_caps::HasAtBottom<ComputationGraded<Row<>, int>>);
static_assert(!detail::computation_graded_caps::HasAtBottom<ComputationGraded<every_effect_row, int>>);

static_assert(!std::is_default_constructible_v<ComputationGraded<Row<>, int>>);
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

using G_pure = ComputationGraded<Row<>, int>;
using G_bg = ComputationGraded<Row<Effect::Bg>, int>;

// A write in place would keep the row over bytes that another
// computation produced, so peek_mut needs the key.  A swap keeps each
// value under its own row, and it reaches this Relative-modality carrier
// because the row grade is empty.
static_assert(!HasPeekMut<G_pure>);
static_assert(!HasPeekMut<G_bg>);
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

// The derived class adds no members, so it is the size of its base,
// which is the size of the payload.
static_assert(sizeof(Computation<Row<>, int>) == sizeof(ComputationGraded<Row<>, int>));
static_assert(sizeof(Computation<Row<Effect::Bg, Effect::IO>, double>) == sizeof(double));

namespace detail::computation_self_test {

struct EmptyValue {};
struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

using C_empty = Computation<Row<>, EmptyValue>;
using C_one_byte = Computation<Row<>, OneByteValue>;
using C_eight_byte = Computation<Row<Effect::Bg>, EightByteValue>;

static_assert(std::is_same_v<C_empty::row_type, Row<>>);
static_assert(std::is_same_v<C_empty::value_type, EmptyValue>);

static_assert(std::is_same_v<C_empty::graded_type, ComputationGraded<Row<>, EmptyValue>>);
static_assert(std::is_same_v<C_eight_byte::graded_type, ComputationGraded<Row<Effect::Bg>, EightByteValue>>);

static_assert(std::is_default_constructible_v<C_empty>);
static_assert(std::is_default_constructible_v<C_one_byte>);
static_assert(!std::is_default_constructible_v<C_eight_byte>,
              "A default value at an engaged row would claim effects that nothing exercised.");

// The substrate's consume, peek and peek_mut hand out the payload with no
// gate, so none of them is a member of a Computation, and no public
// constructor builds one from a value.
template <typename C>
concept ExposesSubstratePayload = requires(C& c) {
    { c.peek() };
} || requires(C& c) {
    { c.peek_mut() };
} || requires(C&& c) {
    { std::move(c).consume() };
};
static_assert(!ExposesSubstratePayload<C_empty> && !ExposesSubstratePayload<C_eight_byte>);
static_assert(!std::is_constructible_v<C_eight_byte, EightByteValue>
              && !std::is_constructible_v<C_one_byte, OneByteValue>);
static_assert(!std::is_convertible_v<C_eight_byte*, typename C_eight_byte::graded_type*>,
              "The substrate is a private base, so no pointer conversion reaches it.");

static_assert(sizeof(C_empty) == 1);
static_assert(sizeof(C_one_byte) == sizeof(OneByteValue));
static_assert(sizeof(C_eight_byte) == sizeof(EightByteValue));

// Alignment and copy-triviality are checked against the substrate as
// well as the size, so that padding or an attribute change that leaves
// the size alone is still caught.
static_assert(alignof(C_empty) == alignof(typename C_empty::graded_type));
static_assert(alignof(C_one_byte) == alignof(typename C_one_byte::graded_type));
static_assert(alignof(C_eight_byte) == alignof(typename C_eight_byte::graded_type));

static_assert(std::is_trivially_copyable_v<C_empty> == std::is_trivially_copyable_v<typename C_empty::graded_type>);
static_assert(std::is_trivially_copyable_v<C_one_byte>
              == std::is_trivially_copyable_v<typename C_one_byte::graded_type>);
static_assert(std::is_trivially_copyable_v<C_eight_byte>
              == std::is_trivially_copyable_v<typename C_eight_byte::graded_type>);

static_assert(C_empty::effect_count_in_row() == 0);
static_assert(C_one_byte::effect_count_in_row() == 0);
static_assert(C_eight_byte::effect_count_in_row() == 1);

template <typename T>
using ComputationOverEmptyRow = Computation<Row<>, T>;
CRUCIBLE_COMPUTATION_LAYOUT_INVARIANT(ComputationOverEmptyRow, OneByteValue);
CRUCIBLE_COMPUTATION_LAYOUT_INVARIANT(ComputationOverEmptyRow, EightByteValue);

static_assert(
    [] consteval {
        auto pure = Computation<Row<>, int>::mint_computation(42);
        return pure.extract() == 42;
    }(),
    "The round trip through mk and extract on an empty-row Computation<int> failed.");

// The scenarios that build an engaged row take a context from the test
// door, so they sit in test/foundation/test_computation.cpp.

static_assert(
    noexcept(std::declval<Computation<Row<Effect::Bg>, int>>().template weaken<Row<Effect::Bg, Effect::IO>>()),
    "The rvalue weaken must be noexcept for a payload that is trivially move-constructible.");

// The copy-constructible conjunct on the lvalue weaken must not
// exclude the ordinary case, so both overloads are witnessed on a
// copyable payload.
static_assert(
    requires(Computation<Row<Effect::Bg>, int> const& c) { c.template weaken<Row<Effect::Bg, Effect::IO>>(); },
    "The lvalue weaken must remain available for a copy-constructible payload.");

static_assert(
    requires(Computation<Row<Effect::Bg>, int>&& c) { std::move(c).template weaken<Row<Effect::Bg, Effect::IO>>(); },
    "The rvalue weaken must remain available for a copy-constructible payload.");

static_assert(std::is_copy_constructible_v<int>,
              "The positive witness for the lvalue weaken constraint depends on int being "
              "copy-constructible.");

// The pins below exercise the trait the constraint depends on without
// instantiating a Computation over a move-only payload.
namespace weaken_copy_constructible_pin {

struct MoveOnlyProbe {
    constexpr MoveOnlyProbe() noexcept = default;
    constexpr MoveOnlyProbe(MoveOnlyProbe&&) noexcept = default;
    MoveOnlyProbe(MoveOnlyProbe const&) = delete;
    MoveOnlyProbe& operator=(MoveOnlyProbe&&) noexcept = default;
    MoveOnlyProbe& operator=(MoveOnlyProbe const&) = delete;
    ~MoveOnlyProbe() = default;
};

static_assert(!std::is_copy_constructible_v<MoveOnlyProbe>,
              "The lvalue weaken constraint depends on std::is_copy_constructible_v answering false for a "
              "type with a deleted copy constructor.  Should that stop holding, the constraint gates "
              "nothing.");

static_assert(std::is_move_constructible_v<MoveOnlyProbe>,
              "The rvalue weaken on a move-only payload depends on that payload being "
              "move-constructible.");

}  // namespace weaken_copy_constructible_pin

namespace extract_payload_gate {

static_assert(detail::extract_admits_payload_v<int>);
static_assert(detail::extract_admits_payload_v<double>);

static_assert(detail::extract_admits_payload_v<Computation<Row<>, int>>);

static_assert(!detail::extract_admits_payload_v<Computation<Row<Effect::Bg>, int>>);
static_assert(!detail::extract_admits_payload_v<Computation<Row<Effect::Alloc, Effect::IO>, int>>);

static_assert(!detail::extract_admits_payload_v<Computation<Row<>, Computation<Row<Effect::Bg>, int>>>);

static_assert(
    !detail::extract_admits_payload_v<Computation<Row<>, Computation<Row<>, Computation<Row<Effect::Bg>, int>>>>);

static_assert(detail::extract_admits_payload_v<Computation<Row<>, Computation<Row<>, Computation<Row<>, int>>>>);

// The three authority kinds the relation enumerates.  Each reaches the
// relation through its own specialization, so each needs its own cell.
// A repair that covered only the capability would satisfy the first
// line and leave the other two open.

struct AuthorityProbeTag {};

static_assert(!detail::extract_admits_payload_v<Capability<Effect::IO, Bg>>);
static_assert(!detail::extract_admits_payload_v<::foundation::permissions::Permission<AuthorityProbeTag>>);
static_assert(!detail::extract_admits_payload_v<::foundation::permissions::SharedPermission<AuthorityProbeTag>>);
static_assert(!detail::extract_admits_payload_v<ExecCtx<Bg, Row<Effect::Bg>>>);

// And each one through a pure carrier.  The row is empty and the
// authority is inside, so a relation that read only the row would admit
// it.

static_assert(!detail::extract_admits_payload_v<Computation<Row<>, Capability<Effect::IO, Bg>>>);
static_assert(
    !detail::extract_admits_payload_v<Computation<Row<>, ::foundation::permissions::Permission<AuthorityProbeTag>>>);
static_assert(!detail::extract_admits_payload_v<Computation<Row<>, ExecCtx<Bg, Row<Effect::Bg>>>>);

// A carrier the list does not name conveys what it holds.  The walk
// reads a member, a base, a pointer target and an argument of a
// template, whatever its parameters.  Capability is only declared here,
// so the cells over complete carriers are in
// test/foundation/test_computation.cpp.  The target of a pointer needs
// no complete type.
static_assert(!detail::extract_admits_payload_v<Capability<Effect::IO, Bg>*>);
static_assert(!detail::extract_admits_payload_v<Capability<Effect::IO, Bg> const* const*>);

// A function hands out its return type and can write through a
// reference parameter.  A pointer to member is a function from its class
// to its member.  So each of them conveys the capability that it names.
struct AuthorityProbeHolder {
    int count = 0;
};
struct HoldsCapabilityFactory {
    Capability<Effect::IO, Bg> (*make)() = nullptr;
};
static_assert(!detail::extract_admits_payload_v<Capability<Effect::IO, Bg> (*)()>);
static_assert(!detail::extract_admits_payload_v<void (*)(Capability<Effect::IO, Bg>&)>);
static_assert(!detail::extract_admits_payload_v<Capability<Effect::IO, Bg> AuthorityProbeHolder::*>);
static_assert(!detail::extract_admits_payload_v<HoldsCapabilityFactory>);
static_assert(detail::extract_admits_payload_v<int (*)(double)>);
static_assert(detail::extract_admits_payload_v<double AuthorityProbeHolder::*>);

// A lambda with captures holds state the walk cannot read, so it is
// refused whatever it captures.  A lambda without captures is empty.
inline constexpr auto captures_an_int = [held = 7] { return held; };
inline constexpr auto captures_nothing = [] { return 7; };
static_assert(!detail::extract_admits_payload_v<decltype(captures_an_int)>);
static_assert(detail::extract_admits_payload_v<decltype(captures_nothing)>);

// The opt-in escape, for a type that conveys authority without being
// nameable here.  Declaring the member false is not a way out of the
// enumerated list, and declaring nothing is the admitting default the
// residual comment above describes.

struct DeclaredAuthority {
    static constexpr bool conveys_authority = true;
};
static_assert(!detail::extract_admits_payload_v<DeclaredAuthority>);
static_assert(!detail::extract_admits_payload_v<Computation<Row<>, DeclaredAuthority>>);

struct DeclaresNoAuthority {
    static constexpr bool conveys_authority = false;
};
static_assert(detail::extract_admits_payload_v<DeclaresNoAuthority>);

// Only the admitting direction is witnessed through a requires
// expression.  GCC 16 turns a failed constraint inside the body of a
// negated requires expression into a hard error rather than a
// substitution failure, so the rejecting direction is pinned on the
// trait above.
static_assert(
    requires(Computation<Row<>, Computation<Row<>, int>> const& c) { c.extract(); },
    "A pure Computation wrapped in a pure Computation must admit extract.");

static_assert(requires(Computation<Row<>, int> const& c) { c.extract(); }, "A plain payload must still admit extract.");

}  // namespace extract_payload_gate

namespace then_payload_gate {

constexpr auto legit_callback = [](int x) { return Computation<Row<>, int>::mint_computation(x + 1); };
static_assert(
    requires(Computation<Row<>, int> const& c) { c.then(legit_callback); },
    "A callback returning a plain payload must admit through then.");

constexpr auto nested_pure_callback = [](int) {
    using Inner = Computation<Row<>, int>;
    return Computation<Row<>, Inner>::mint_computation(Inner::mint_computation(42));
};
static_assert(
    requires(Computation<Row<>, int> const& c) { c.then(nested_pure_callback); },
    "A callback returning a nested Computation at the empty row must admit through then.  Nothing is "
    "hidden by an empty inner row.");

using LaunderingInner = Computation<Row<Effect::Bg>, int>;
static_assert(!detail::extract_admits_payload_v<LaunderingInner>,
              "A callback whose returned value is itself an engaged Computation must not admit through "
              "then.  The inner row would disappear from the union.");

}  // namespace then_payload_gate

namespace mint_provenance_surface {

// The witnessed mint is the one door of an engaged row.  No unwitnessed
// lift exists.
template <typename C>
concept HasUnwitnessedLift = requires { C::template lift<Effect::Bg>(0); };
static_assert(!HasUnwitnessedLift<Computation<Row<>, int>>);

// The asymmetry between the two contexts below is the closure: one
// carries Bg in its row and can witness a Bg claim, the other cannot.
// If either pin reds, the gate has lost its discriminating power.
static_assert(row_contains(^^typename detail::ctx_witnesses::BgWitness::row_type, Effect::Bg),
              "The background drain context must carry Effect::Bg in its row.  It is the context that "
              "witnesses a Bg claim.");

static_assert(!row_contains(^^typename detail::ctx_witnesses::FgWitness::row_type, Effect::Bg),
              "The hot foreground context must not carry Effect::Bg in its row.  Foreground code must "
              "not be able to witness a Bg claim.");

static_assert(std::is_same_v<decltype(Computation<Row<>, int>::template mint_computation_in_ctx<Effect::Bg>(
                                 std::declval<detail::ctx_witnesses::BgWitness const&>(), 42)),
                             Computation<Row<Effect::Bg>, int>>,
              "The witnessed mint must admit when the context's row contains the requested effect, and "
              "must return a Computation at the one-atom row.");

template <Effect Cap, class Ctx>
concept MintsInCtx =
    requires(Ctx const& ctx) { Computation<Row<>, int>::template mint_computation_in_ctx<Cap>(ctx, 0); };
static_assert(MintsInCtx<Effect::Bg, detail::ctx_witnesses::BgWitness>);
static_assert(!MintsInCtx<Effect::IO, detail::ctx_witnesses::BgWitness>);
static_assert(!MintsInCtx<Effect::Bg, detail::ctx_witnesses::FgWitness>);

}  // namespace mint_provenance_surface

static_assert(noexcept(std::declval<Computation<Row<>, int>>().extract()),
              "The rvalue extract must be noexcept for a payload that is trivially move-constructible.");

static_assert(IsComputation<Computation<Row<>, int>>);
static_assert(IsComputation<Computation<Row<Effect::Bg>, double>>);
static_assert(IsComputation<Computation<Row<>, int>&>);
static_assert(IsComputation<const Computation<Row<>, int>&>);
static_assert(!IsComputation<int>);
static_assert(!IsComputation<Row<Effect::Bg>>);

static_assert(
    [] consteval {
        auto pure = Computation<Row<>, int>::mint_computation(7);
        auto doubled = pure.map([](int x) { return x * 2; });
        using Doubled = decltype(doubled);
        return std::is_same_v<Doubled::row_type, Row<>> && std::is_same_v<Doubled::value_type, int>
            && doubled.extract() == 14;
    }(),
    "A map preserves the row and applies the function to the value.");

static_assert(
    [] consteval {
        auto pure = Computation<Row<>, int>::mint_computation(3);
        auto as_double = pure.map([](int x) -> double { return x + 0.5; });
        using D = decltype(as_double);
        return std::is_same_v<D::row_type, Row<>> && std::is_same_v<D::value_type, double>;
    }(),
    "A map admits a change of value type while preserving the row.");

static_assert(
    [] consteval {
        auto pure = Computation<Row<>, int>::mint_computation(5);
        auto chained = pure.then([](int x) { return Computation<Row<>, int>::mint_computation(x * 2); });
        return chained.extract() == 10 && std::is_same_v<decltype(chained)::row_type, Row<>>;
    }(),
    "A bind over two empty rows must produce a result at the empty row.");

static_assert(
    [] consteval {
        auto pure = Computation<Row<>, int>::mint_computation(99);
        auto const& g = pure.graded();
        using G = std::remove_cvref_t<decltype(g)>;
        return std::is_same_v<G, ComputationGraded<Row<>, int>> && g.peek() == 99;
    }(),
    "The lvalue graded accessor must expose the substrate view at the matching specialization.");

// The view is const, and a payload that conveys an authority does not
// reach the substrate view at all.
static_assert(std::is_same_v<decltype(std::declval<Computation<Row<>, int> const&>().graded()),
                             ComputationGraded<Row<>, int> const&>);
template <typename C>
concept HasGradedView = requires(C const& c) { c.graded(); };
template <typename C>
concept HasGradedMove = requires(C&& c) { std::move(c).graded(); };
static_assert(HasGradedView<Computation<Row<Effect::Bg>, int>> && HasGradedMove<Computation<Row<Effect::Bg>, int>>);
static_assert(!detail::extract_admits_payload_v<detail::ctx_witnesses::BgWitness>,
              "The positive witness for the graded constraint depends on a context conveying authority.");

}  // namespace detail::computation_self_test

}  // namespace foundation::effects
