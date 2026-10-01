// The compile-time checks of foundation/algebra/Graded.h.

#include <foundation/algebra/Graded.h>

namespace foundation::algebra {

namespace detail::graded_self_test {

// A class that is not an authority for anything.
struct stranger {};

static_assert(!std::is_default_constructible_v<grade_key<stranger>>, "only the authority builds its key");
static_assert(!std::is_copy_constructible_v<grade_key<self_test_authority>>);
static_assert(!std::is_trivially_copyable_v<grade_key<self_test_authority>>, "std::bit_cast must not build a key");
static_assert(!std::is_implicit_lifetime_v<grade_key<self_test_authority>>,
              "a lifetime started over a buffer must not build a key");

static_assert(Lattice<TrivialEmptyLattice>);
static_assert(BoundedLattice<TrivialEmptyLattice>);
static_assert(std::is_empty_v<TrivialEmptyLattice::element_type>);

static_assert(BoundedLattice<TrivialSlotLattice> && GradeIgnoresBytes<TrivialSlotLattice>);
static_assert(!GradeIgnoresBytes<TrivialBoolLattice>);

static_assert(BoundedLattice<TrivialChainLattice>);

struct EightByteValue {
    unsigned long long v{0};
};

using GComonad = Graded<ModalityKind::Comonad, TrivialBoolLattice, EmptyValue>;
using GRelMonad = Graded<ModalityKind::RelativeMonad, TrivialBoolLattice, EmptyValue>;
using GAbsolute = Graded<ModalityKind::Absolute, TrivialBoolLattice, EmptyValue>;
using GRelative = Graded<ModalityKind::Relative, TrivialBoolLattice, EmptyValue>;

// A stored grade over bytes is a claim, and a default value would make
// it with no key.
static_assert(!std::is_default_constructible_v<GComonad>);
static_assert(!std::is_default_constructible_v<GRelMonad>);
static_assert(!std::is_default_constructible_v<GAbsolute>);
static_assert(!std::is_default_constructible_v<GRelative>);
static_assert(!std::is_constructible_v<GAbsolute, EmptyValue, bool>, "the grade needs the key");

static_assert(std::is_same_v<GAbsolute::value_type, EmptyValue>);
static_assert(std::is_same_v<GAbsolute::lattice_type, TrivialBoolLattice>);
static_assert(std::is_same_v<GAbsolute::grade_type, bool>);
static_assert(GAbsolute::modality == ModalityKind::Absolute);

static_assert(GAbsolute::modality_name() == "Absolute");
static_assert(GAbsolute::lattice_name() == "TrivialBool");
static_assert(GComonad::modality_name() == "Comonad");

static_assert(sizeof(GAbsolute) == 1);

static_assert(sizeof(GOneByte) == 2);

using GEightByte = Graded<ModalityKind::Absolute, TrivialBoolLattice, EightByteValue>;
static_assert(sizeof(GEightByte) == 16);

using GEmptyGrade_Empty = Graded<ModalityKind::Absolute, TrivialEmptyLattice, EmptyValue>;
using GEmptyGrade_OneByte = Graded<ModalityKind::Absolute, TrivialEmptyLattice, OneByteValue>;
using GEmptyGrade_EightB = Graded<ModalityKind::Absolute, TrivialEmptyLattice, EightByteValue>;

static_assert(sizeof(GEmptyGrade_Empty) == 1);
static_assert(sizeof(GEmptyGrade_OneByte) == sizeof(OneByteValue));
static_assert(sizeof(GEmptyGrade_EightB) == sizeof(EightByteValue));

constexpr GOneByte g_at_top{self_test_authority::key(), OneByteValue{}, true};
static_assert(g_at_top.grade() == true);
static_assert(g_at_top.peek().c == 0);

constexpr GOneByte g_bot{self_test_authority::key(), OneByteValue{}, TrivialBoolLattice::bottom()};
static_assert(g_bot.grade() == false);

constexpr GOneByte g_weakened = g_bot.weaken(true);
static_assert(g_weakened.grade() == true);

constexpr GOneByte g_composed = g_bot.compose(g_at_top);
static_assert(g_composed.grade() == true);

// Under a grade that any bytes satisfy, the default value and the
// bottom-graded default are built without a key.
static_assert(std::is_default_constructible_v<GSlotOneByte>);
constexpr GSlotOneByte g_slot_bot = GSlotOneByte::at_bottom();
static_assert(g_slot_bot.grade() == false);
static_assert(g_slot_bot.peek().c == 0);

// A stored grade inside the order constructs at compile time; one
// outside it fails the constructor's check and is not a constant
// expression.  The negative direction is
// test/foundation/neg/neg_graded_stored_grade_outside_the_order.cpp.
constexpr GChainOneByte g_chain_in_order{self_test_authority::key(), OneByteValue{}, static_cast<unsigned char>(2)};
static_assert(g_chain_in_order.grade() == 2);
static_assert(g_chain_in_order.weaken(static_cast<unsigned char>(3)).grade() == 3);

// The same chain as its own grade.  The value is checked against the
// order as the stored grade is; the negative direction is
// test/foundation/neg/neg_graded_element_grade_outside_the_order.cpp.
constexpr GChainElement g_chain_element{static_cast<unsigned char>(2)};
static_assert(g_chain_element.grade() == 2);
static_assert(g_chain_element.weaken(static_cast<unsigned char>(3)).grade() == 3);
static_assert(g_chain_element.compose(GChainElement{static_cast<unsigned char>(1)}).grade() == 2);

// The head checks the orientation only where the grade is stored.  A
// chain that states no orientation grades a value that is its own grade,
// and it is refused beside a value that it does not name.
struct UnstatedChainLattice {
    using element_type = unsigned char;
    [[nodiscard]] static constexpr element_type bottom() noexcept { return 0; }
    [[nodiscard]] static constexpr element_type top() noexcept { return 3; }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept { return a <= b; }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept { return a < b ? b : a; }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept { return a < b ? a : b; }
};

template <typename L, typename T>
concept CanNameGraded = requires { typename Graded<ModalityKind::Absolute, L, T>; };

static_assert(claim_orientation_v<UnstatedChainLattice> == ClaimOrientation::unstated);
static_assert(CanNameGraded<UnstatedChainLattice, unsigned char>);
static_assert(!CanNameGraded<UnstatedChainLattice, OneByteValue>,
              "an unstated orientation is refused as a stored grade");
static_assert(CanNameGraded<TrivialChainLattice, OneByteValue>);

template <typename G>
concept CanInjectWithoutKey = requires { G::inject(typename G::value_type{}, typename G::grade_type{}); };

static_assert(CanExtract<GComonad>);
static_assert(!CanExtract<GAbsolute>);
static_assert(!CanExtract<GRelMonad>);
static_assert(!CanExtract<GRelative>);

static_assert(CanInject<GRelMonad>);
static_assert(!CanInject<GComonad>);
static_assert(!CanInject<GAbsolute>);
static_assert(!CanInject<GRelative>);
static_assert(!CanInjectWithoutKey<GRelMonad>);

static_assert(!CanPeekMut<GOneByte> && CanPeekMutWithKey<GOneByte>);
static_assert(CanPeekMut<GSlotOneByte> && CanPeekMutWithKey<GSlotOneByte>);
static_assert(!CanPeekMut<GComonad> && !CanPeekMutWithKey<GComonad>,
              "a Comonad grade over a non-empty element names the value, and no key reopens it");

// at_bottom() exists where the grade is derived from the type or the
// value, and in the stored regime only for a grade that any bytes
// satisfy.  at_bottom(T) exists nowhere.
template <typename G>
concept CanAtBottomNoArg = requires { G::at_bottom(); };
template <typename G>
concept CanAtBottomValue = requires(typename G::value_type v) { G::at_bottom(std::move(v)); };

// Grade stored beside the value.
static_assert(!CanAtBottomNoArg<GOneByte>);
static_assert(CanAtBottomNoArg<GSlotOneByte>);
static_assert(!CanAtBottomValue<GOneByte> && !CanAtBottomValue<GSlotOneByte>,
              "at_bottom(T) in the stored regime is the strongest claim about a value the caller chose.  The "
              "keyed constructor with L::bottom() carries it.");

// Grade is the value.
static_assert(CanAtBottomNoArg<GBoolElement>);
static_assert(!CanAtBottomValue<GBoolElement>,
              "at_bottom(T) on a lattice whose element type is T could only honour the request by "
              "discarding the argument.");
static_assert(std::is_default_constructible_v<GBoolElement>);

// The keyed constructor's witness is the assertion that this value is
// already at bottom, and it is not tautological: passing true fails it.
constexpr GBoolElement g_bool_bot_checked{self_test_authority::key(), false, TrivialBoolLattice::bottom()};
static_assert(g_bool_bot_checked.grade() == TrivialBoolLattice::bottom());

static_assert(Lattice<MiniDerivedLattice>);
static_assert(BoundedBelowLattice<MiniDerivedLattice>);
static_assert(LatticeDerivesGrade<MiniDerivedLattice, MiniContainer>);

// Grade derived from the value.
static_assert(CanAtBottomNoArg<GDerivedSeq>);
static_assert(!CanAtBottomValue<GDerivedSeq>,
              "at_bottom(T) on a derived-grade lattice would need an inverse of grade_of.");
static_assert(!CanPeekMut<GDerivedSeq> && CanPeekMutWithKey<GDerivedSeq>);

constexpr GDerivedSeq g_derived_bot_noarg = GDerivedSeq::at_bottom();
static_assert(g_derived_bot_noarg.grade() == MiniDerivedLattice::bottom());

// Not tautological: MiniContainer{3} fails the witness check.
constexpr GDerivedSeq g_derived_bot_checked{self_test_authority::key(), MiniContainer{}, MiniDerivedLattice::bottom()};
static_assert(g_derived_bot_checked.grade() == MiniDerivedLattice::bottom());
static_assert(GDerivedSeq{MiniContainer{3}}.grade() == 3);

template <typename T>
using AbsoluteOverEmpty = Graded<ModalityKind::Absolute, TrivialEmptyLattice, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AbsoluteOverEmpty, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AbsoluteOverEmpty, EightByteValue);

CRUCIBLE_GRADED_LAYOUT_INVARIANT(AbsoluteOverEmpty, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AbsoluteOverEmpty, double);

// The regimes whose grade is not stored keep one member of bytes: the
// empty grade member shares the value's address.
template <typename T>
using ChainElementOver = Graded<ModalityKind::Absolute, TrivialChainLattice, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ChainElementOver, unsigned char);
static_assert(sizeof(GDerivedSeq) == sizeof(MiniContainer));

static_assert(std::is_trivially_destructible_v<int>);
static_assert(std::is_trivially_destructible_v<AbsoluteOverEmpty<int>>);
static_assert(std::is_trivially_copyable_v<int>);
static_assert(std::is_trivially_copyable_v<AbsoluteOverEmpty<int>>);

struct MoveOnlyValue {
    int v{0};
    constexpr MoveOnlyValue() = default;
    constexpr MoveOnlyValue(int x) noexcept : v{x} {}
    MoveOnlyValue(const MoveOnlyValue&) = delete;
    MoveOnlyValue(MoveOnlyValue&&) noexcept = default;
    MoveOnlyValue& operator=(const MoveOnlyValue&) = delete;
    MoveOnlyValue& operator=(MoveOnlyValue&&) noexcept = default;
};

using GMoveOnly = Graded<ModalityKind::Absolute, TrivialEmptyLattice, MoveOnlyValue>;

template <typename G>
concept HasConstWeaken = requires(G const& g, typename G::grade_type r) { g.weaken(r); };
template <typename G>
concept HasRvalueWeaken = requires(G g, typename G::grade_type r) { std::move(g).weaken(r); };
template <typename G>
concept HasConstRvalueWeaken = requires(G const g, typename G::grade_type r) { std::move(g).weaken(r); };

static_assert(HasConstWeaken<GOneByte>);
static_assert(HasRvalueWeaken<GOneByte>);
static_assert(!HasConstWeaken<GMoveOnly>);
static_assert(HasRvalueWeaken<GMoveOnly>);
static_assert(!HasConstRvalueWeaken<GMoveOnly>, "a const rvalue cannot move its value out");

}  // namespace detail::graded_self_test

namespace detail::is_graded_self_test {

using GraderAB = Graded<ModalityKind::Absolute, ::foundation::algebra::detail::TrivialBoolLattice, bool>;

static_assert(IsGraded<GraderAB>);
static_assert(IsGraded<GraderAB const>);
static_assert(IsGraded<GraderAB&>);
static_assert(IsGraded<GraderAB&&>);

static_assert(!IsGraded<int>);
static_assert(!IsGraded<void>);
static_assert(!IsGraded<::foundation::algebra::detail::TrivialBoolLattice>);

}  // namespace detail::is_graded_self_test

}  // namespace foundation::algebra
