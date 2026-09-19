#pragma once

// Graded<M, L, T> pairs a value with its position in a lattice.  Four
// storage regimes exist.  The lattice selects one, and the choice is
// invisible in the public API:
//
//   LatticeElement<L> is empty      the grade lives in the type, and
//                                   both members collapse under
//                                   [[no_unique_address]]
//   L::element_type is T            value and grade are one member
//   L publishes grade_of(T const&)  one member, grade computed on read
//   none of the above               two members, grade stored per
//                                   instance
//
// The first three cost sizeof(T).  A new lattice picks its regime by
// answering "where does the grade already live?".  If the grade is
// recoverable from the type or from the value, do not add a member
// for it.

#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/Modality.h>

#include <contracts>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace foundation::algebra {

template <ModalityKind M, Lattice L, typename T>
class [[nodiscard]] Graded {
    static_assert(IsModality<M>, "Graded<M, L, T>: M must be one of Comonad / RelativeMonad / "
                                 "Absolute / Relative / Stepping.");

public:
    static constexpr ModalityKind modality = M;

    using modality_kind_type = ModalityKind;
    using lattice_type = L;
    using value_type = T;
    using grade_type = LatticeElement<L>;

private:
    [[no_unique_address]] T inner_{};
    [[no_unique_address]] grade_type grade_{};

public:
    [[nodiscard]] static consteval std::string_view modality_name() noexcept {
        return ::foundation::algebra::modality_name(M);
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return ::foundation::algebra::lattice_name<L>();
    }
    // The qualification depth of the returned name depends on the
    // scope chain of the including translation unit: the same T can
    // print as "MyKey" from one include path and as
    // "some::deep::MyKey" from another.  A caller asserting on this
    // string must therefore compare with ends_with, never with ==, or
    // the assertion passes or fails according to which translation
    // unit it sits in.  The simple name is always a suffix of the
    // qualified form.
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return std::meta::display_string_of(^^T);
    }

    constexpr Graded() = default;
    constexpr Graded(const Graded&) = default;
    constexpr Graded(Graded&&) = default;
    constexpr Graded& operator=(const Graded&) = default;
    constexpr Graded& operator=(Graded&&) = default;
    ~Graded() = default;

    // The stored grade is the only regime where a caller hands in a
    // grade that nothing else witnesses.  A carrier can hold values that
    // no lattice element names — an enum cast from an integer, a byte
    // above the top of a chain — and every later leq would then compare
    // against a grade that is outside the order.  This is the one place
    // a stored grade is checked; the other two regimes derive theirs.
    // It is an in-body contract_assert for the reason given on the
    // element-is-the-value specialization below.
    constexpr Graded(T value, grade_type grade) noexcept(std::is_nothrow_move_constructible_v<T>
                                                         && std::is_nothrow_move_constructible_v<grade_type>)
        : inner_{std::move(value)}, grade_{std::move(grade)} {
        if constexpr (BoundedLattice<L>) {
            contract_assert(L::leq(L::bottom(), grade_) && L::leq(grade_, L::top()));
        } else if constexpr (BoundedBelowLattice<L>) {
            contract_assert(L::leq(L::bottom(), grade_));
        } else if constexpr (BoundedAboveLattice<L>) {
            contract_assert(L::leq(grade_, L::top()));
        }
    }

    // at_bottom() exists on all three specializations and means the
    // same thing on each: the bottom-graded element.
    //
    // at_bottom(T) exists only here.  It means "hold this value, graded
    // at bottom", which needs a grade the caller can set without
    // touching the value — the arrangement this specialization has and
    // the other two do not.  Where the grade is the value, or is
    // derived from it, honouring the request would mean discarding the
    // argument or inverting grade_of, so the overload is absent there
    // and the two-arg constructor covers the case.  That constructor
    // takes the grade as a witness and checks it, which is exactly
    // "assert this value is already at bottom" when the grade passed is
    // L::bottom().
    //
    // The overload used to exist on all three, forcing the grade here
    // and asserting it there.  One call then had two behaviours chosen
    // by a storage regime the public API hides, so a value that this
    // specialization accepted silently made the other two abort.
    //
    // No contract_assert guards the result.  The grade is set to
    // L::bottom() one line above, and leq(bottom, bottom) is already
    // consteval-checked by the Lattice concept, so a check here would
    // be tautological — which is what it was.
    //
    // A post() clause on a templated class member crashes the compiler
    // when its predicate is template-dependent.  Every postcondition in
    // this file is therefore an in-body contract_assert, which checks
    // the same thing at runtime.
    [[nodiscard]] static constexpr Graded at_bottom(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        requires BoundedBelowLattice<L>
    {
        return Graded{std::move(value), L::bottom()};
    }

    [[nodiscard]] static constexpr Graded at_bottom() noexcept(std::is_nothrow_default_constructible_v<T>
                                                               && std::is_nothrow_move_constructible_v<T>)
        requires BoundedBelowLattice<L> && std::default_initializable<T>
    {
        return Graded{T{}, L::bottom()};
    }

    [[nodiscard]] constexpr T const& peek() const& noexcept { return inner_; }
    [[nodiscard]] constexpr T consume() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return std::move(inner_);
    }
    [[nodiscard]] constexpr grade_type grade() const noexcept(std::is_nothrow_copy_constructible_v<grade_type>) {
        return grade_;
    }

    // Mutation is admitted exactly when it cannot invalidate the grade.
    // An Absolute grade is a property of the slot rather than of the
    // bytes it holds, and an empty grade carries nothing to invalidate.
    // A Comonad or RelativeMonad grade over a non-empty element asserts
    // something about the specific value, so raw mutation there would
    // silently turn the grade into a lie.
    //
    // Graded exposes the sound operations.  A wrapper that needs a
    // narrower discipline hides them and offers its own.
    [[nodiscard]] constexpr T& peek_mut() & noexcept
        requires(AbsoluteModality<M> || std::is_empty_v<grade_type>)
    {
        return inner_;
    }

    constexpr void swap(Graded& other) noexcept(std::is_nothrow_swappable_v<T>
                                                && std::is_nothrow_swappable_v<grade_type>)
        requires(AbsoluteModality<M> || std::is_empty_v<grade_type>)
    {
        using std::swap;
        swap(inner_, other.inner_);
        swap(grade_, other.grade_);
    }

    friend constexpr void swap(Graded& a, Graded& b) noexcept(std::is_nothrow_swappable_v<T>
                                                              && std::is_nothrow_swappable_v<grade_type>)
        requires(AbsoluteModality<M> || std::is_empty_v<grade_type>)
    {
        a.swap(b);
    }

    [[nodiscard]] constexpr T extract() && noexcept(std::is_nothrow_move_constructible_v<T>)
        requires ComonadModality<M>
    {
        return std::move(inner_);
    }

    [[nodiscard]] static constexpr Graded
    inject(T value, grade_type grade) noexcept(std::is_nothrow_move_constructible_v<T>
                                               && std::is_nothrow_copy_constructible_v<grade_type>
                                               && std::is_nothrow_move_constructible_v<grade_type>)
        requires RelativeMonadModality<M>
    {
        grade_type expected = grade;
        Graded result{std::move(value), std::move(grade)};
        contract_assert(L::leq(result.grade(), expected) && L::leq(expected, result.grade()));
        return result;
    }

    // The const& overload is gated on copy_constructible<T> so that a
    // move-only T falls through to the && overload instead of emitting
    // a copy-deleted error cascade.
    [[nodiscard]] constexpr Graded
    weaken(grade_type new_grade) const& noexcept(std::is_nothrow_copy_constructible_v<T>
                                                 && std::is_nothrow_copy_constructible_v<grade_type>)
        requires std::copy_constructible<T>
    pre(L::leq(grade_, new_grade)) {
        Graded result{inner_, new_grade};
        contract_assert(L::leq(result.grade(), new_grade) && L::leq(new_grade, result.grade()));
        return result;
    }

    [[nodiscard]] constexpr Graded weaken(grade_type new_grade) && noexcept(
        std::is_nothrow_move_constructible_v<T> && std::is_nothrow_copy_constructible_v<grade_type>
        && std::is_nothrow_move_constructible_v<grade_type>) pre(L::leq(grade_, new_grade)) {
        grade_type expected = new_grade;
        Graded result{std::move(inner_), std::move(new_grade)};
        contract_assert(L::leq(result.grade(), expected) && L::leq(expected, result.grade()));
        return result;
    }

    [[nodiscard]] constexpr Graded
    compose(Graded const& other) const& noexcept(std::is_nothrow_copy_constructible_v<T>
                                                 && std::is_nothrow_copy_constructible_v<grade_type>)
        requires std::copy_constructible<T>
    {
        grade_type expected = L::join(grade_, other.grade_);
        Graded result{inner_, expected};
        contract_assert(L::leq(result.grade(), expected) && L::leq(expected, result.grade()));
        return result;
    }

    [[nodiscard]] constexpr Graded compose(Graded const& other) && noexcept(
        std::is_nothrow_move_constructible_v<T> && std::is_nothrow_copy_constructible_v<grade_type>) {
        grade_type expected = L::join(grade_, other.grade_);
        Graded result{std::move(inner_), expected};
        contract_assert(L::leq(result.grade(), expected) && L::leq(expected, result.grade()));
        return result;
    }
};

// When L::element_type is exactly T, the value and the grade are the
// same thing.  The primary template would store them as two members,
// paying twice sizeof(T) and letting the two copies drift apart under
// mutation.  This specialization keeps one member, so the drift is
// structurally impossible.
//
// The member set matches the primary exactly.  A caller cannot tell
// which specialization it got.

template <ModalityKind M, Lattice L, typename T>
    requires std::is_same_v<typename L::element_type, T>
class [[nodiscard]] Graded<M, L, T> {
public:
    static constexpr ModalityKind modality = M;

    using modality_kind_type = ModalityKind;
    using lattice_type = L;
    using value_type = T;
    using grade_type = LatticeElement<L>;

private:
    T value_{};

public:
    [[nodiscard]] static consteval std::string_view modality_name() noexcept {
        return ::foundation::algebra::modality_name(M);
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return ::foundation::algebra::lattice_name<L>();
    }
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return std::meta::display_string_of(^^T);
    }

    constexpr Graded() = default;
    constexpr Graded(const Graded&) = default;
    constexpr Graded(Graded&&) = default;
    constexpr Graded& operator=(const Graded&) = default;
    constexpr Graded& operator=(Graded&&) = default;
    ~Graded() = default;

    // The grade argument is a witness that is checked and then thrown
    // away, so this guard is the only barrier against a forged grade
    // constructing a value it does not describe.  It is an in-body
    // contract_assert rather than a pre() clause because a foldable,
    // this-free pre() is skipped during constant evaluation and
    // vanishes entirely under the ignore evaluation semantic.  A
    // failing contract_assert makes a constant evaluation non-constant,
    // so it cannot be skipped either way.
    constexpr Graded(T value, grade_type grade) noexcept(std::is_nothrow_move_constructible_v<T>)
        : value_{std::move(value)} {
        contract_assert(L::leq(value_, grade) && L::leq(grade, value_));
        (void)grade;
    }

    constexpr explicit Graded(T value_or_grade) noexcept(std::is_nothrow_move_constructible_v<T>)
        : value_{std::move(value_or_grade)} {}

    // Here the value is the grade, so there is no at_bottom(T): a
    // request to hold an arbitrary value at bottom could only be
    // honoured by discarding the argument.  A caller asserting that a
    // value is already at bottom writes Graded{value, L::bottom()},
    // whose witness check is that assertion.
    [[nodiscard]] static constexpr Graded at_bottom() noexcept(std::is_nothrow_move_constructible_v<T>)
        requires BoundedBelowLattice<L>
    {
        return Graded{L::bottom()};
    }

    [[nodiscard]] constexpr T const& peek() const& noexcept { return value_; }
    [[nodiscard]] constexpr T consume() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return std::move(value_);
    }
    [[nodiscard]] constexpr grade_type grade() const noexcept(std::is_nothrow_copy_constructible_v<T>) {
        return value_;
    }

    [[nodiscard]] constexpr T& peek_mut() & noexcept
        requires(AbsoluteModality<M> || std::is_empty_v<grade_type>)
    {
        return value_;
    }

    constexpr void swap(Graded& other) noexcept(std::is_nothrow_swappable_v<T>)
        requires(AbsoluteModality<M> || std::is_empty_v<grade_type>)
    {
        using std::swap;
        swap(value_, other.value_);
    }

    friend constexpr void swap(Graded& a, Graded& b) noexcept(std::is_nothrow_swappable_v<T>)
        requires(AbsoluteModality<M> || std::is_empty_v<grade_type>)
    {
        a.swap(b);
    }

    [[nodiscard]] constexpr T extract() && noexcept(std::is_nothrow_move_constructible_v<T>)
        requires ComonadModality<M>
    {
        return std::move(value_);
    }

    [[nodiscard]] static constexpr Graded inject(T value,
                                                 grade_type grade) noexcept(std::is_nothrow_move_constructible_v<T>
                                                                            && std::is_nothrow_copy_constructible_v<T>)
        requires RelativeMonadModality<M>
    {
        T expected = grade;
        Graded result{std::move(value)};
        contract_assert(L::leq(result.grade(), expected) && L::leq(expected, result.grade()));
        return result;
    }

    [[nodiscard]] constexpr Graded weaken(grade_type new_grade) const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    pre(L::leq(value_, new_grade)) {
        T expected = new_grade;
        Graded result{std::move(new_grade)};
        contract_assert(L::leq(result.grade(), expected) && L::leq(expected, result.grade()));
        return result;
    }

    [[nodiscard]] constexpr Graded weaken(grade_type new_grade) && noexcept(std::is_nothrow_move_constructible_v<T>
                                                                            && std::is_nothrow_copy_constructible_v<T>)
        pre(L::leq(value_, new_grade)) {
        T expected = new_grade;
        Graded result{std::move(new_grade)};
        contract_assert(L::leq(result.grade(), expected) && L::leq(expected, result.grade()));
        return result;
    }

    [[nodiscard]] constexpr Graded compose(Graded const& other) const& noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        T expected = L::join(value_, other.value_);
        Graded result{expected};
        contract_assert(L::leq(result.grade(), expected) && L::leq(expected, result.grade()));
        return result;
    }

    [[nodiscard]] constexpr Graded compose(Graded const& other) && noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        T expected = L::join(value_, other.value_);
        Graded result{expected};
        contract_assert(L::leq(result.grade(), expected) && L::leq(expected, result.grade()));
        return result;
    }
};

template <typename L, typename T>
concept LatticeDerivesGrade = requires(T const& v) {
    { L::grade_of(v) } -> std::same_as<typename L::element_type>;
};

// The specialization below has no weaken and no compose.  Both must
// produce a value at a grade the caller names, and the grade is a
// function of the value, so they would need an inverse of grade_of.
// No such inverse exists in general.  Where one does exist it belongs
// to the wrapper, which mutates the value and lets the derived grade
// follow.  Read-side lattice operations stay available through L.

template <ModalityKind M, Lattice L, typename T>
    requires LatticeDerivesGrade<L, T> && (!std::is_same_v<typename L::element_type, T>)
class [[nodiscard]] Graded<M, L, T> {
public:
    static constexpr ModalityKind modality = M;

    using modality_kind_type = ModalityKind;
    using lattice_type = L;
    using value_type = T;
    using grade_type = LatticeElement<L>;

private:
    T value_{};

public:
    [[nodiscard]] static consteval std::string_view modality_name() noexcept {
        return ::foundation::algebra::modality_name(M);
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return ::foundation::algebra::lattice_name<L>();
    }
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return std::meta::display_string_of(^^T);
    }

    constexpr Graded() = default;
    constexpr Graded(const Graded&) = default;
    constexpr Graded(Graded&&) = default;
    constexpr Graded& operator=(const Graded&) = default;
    constexpr Graded& operator=(Graded&&) = default;
    ~Graded() = default;

    // The grade argument is a witness that is checked and then thrown
    // away, so this guard is the only barrier against a forged grade
    // constructing a value it does not describe.  It is an in-body
    // contract_assert rather than a pre() clause for the reason given
    // on the preceding specialization.
    constexpr Graded(T value, grade_type grade) noexcept(std::is_nothrow_move_constructible_v<T>)
        : value_{std::move(value)} {
        contract_assert(L::leq(L::grade_of(value_), grade) && L::leq(grade, L::grade_of(value_)));
        (void)grade;
    }

    constexpr explicit Graded(T value) noexcept(std::is_nothrow_move_constructible_v<T>) : value_{std::move(value)} {}

    // This form relies on the default state of T deriving to bottom,
    // which holds for a container whose grade is its size.  The check
    // is not tautological here: grade_of reads the value, so a T whose
    // default state grades above bottom fires it.
    //
    // There is no at_bottom(T).  The grade follows the value through
    // grade_of, so holding an arbitrary value at bottom would need an
    // inverse of grade_of, and none exists in general.  A caller
    // asserting that a value already derives bottom writes
    // Graded{value, L::bottom()}, whose witness check is that
    // assertion.
    [[nodiscard]] static constexpr Graded at_bottom() noexcept(std::is_nothrow_default_constructible_v<T>
                                                               && std::is_nothrow_move_constructible_v<T>)
        requires BoundedBelowLattice<L> && std::default_initializable<T>
    {
        Graded result{T{}};
        contract_assert(L::leq(result.grade(), L::bottom()) && L::leq(L::bottom(), result.grade()));
        return result;
    }

    // grade() recomputes on every call.  A caller that needs it more
    // than once should hold the result, because the cost of grade_of
    // is the lattice's to choose.
    [[nodiscard]] constexpr T const& peek() const& noexcept { return value_; }
    [[nodiscard]] constexpr T consume() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return std::move(value_);
    }
    [[nodiscard]] constexpr grade_type grade() const noexcept(noexcept(L::grade_of(std::declval<T const&>()))) {
        return L::grade_of(value_);
    }

    // Mutation moves the derived grade with it.  Keeping that movement
    // inside the lattice order is the wrapper's obligation, not this
    // substrate's.
    [[nodiscard]] constexpr T& peek_mut() & noexcept
        requires(AbsoluteModality<M> || std::is_empty_v<grade_type>)
    {
        return value_;
    }

    constexpr void swap(Graded& other) noexcept(std::is_nothrow_swappable_v<T>)
        requires(AbsoluteModality<M> || std::is_empty_v<grade_type>)
    {
        using std::swap;
        swap(value_, other.value_);
    }

    friend constexpr void swap(Graded& a, Graded& b) noexcept(std::is_nothrow_swappable_v<T>)
        requires(AbsoluteModality<M> || std::is_empty_v<grade_type>)
    {
        a.swap(b);
    }

    [[nodiscard]] constexpr T extract() && noexcept(std::is_nothrow_move_constructible_v<T>)
        requires ComonadModality<M>
    {
        return std::move(value_);
    }

    [[nodiscard]] static constexpr Graded inject(T value,
                                                 grade_type grade) noexcept(std::is_nothrow_move_constructible_v<T>)
        requires RelativeMonadModality<M>
    {
        Graded result{std::move(value)};
        contract_assert(L::leq(result.grade(), grade) && L::leq(grade, result.grade()));
        return result;
    }
};

// Two further checks that look correct are deliberately absent from
// the macro below.
//
// Trivial-default-constructibility parity would fire on every T that
// has the property.  The NSDMI on the members makes the wrapper's
// implicit default constructor non-trivial even when T and the grade
// are both trivially default constructible, and the value
// initialization it costs is nothing at runtime.
//
// std::is_layout_compatible_v against T is always false.  Two
// standard-layout classes are layout-compatible only with the same
// members in the same order, and the wrapper always carries one member
// more than T, even when that member occupies no bytes.  Size,
// alignment and the two trivial-trait parities are the closest
// tractable statement of byte-level interchangeability.
#define CRUCIBLE_GRADED_LAYOUT_INVARIANT(GradedAlias, T_)                                                          \
    static_assert(sizeof(GradedAlias<T_>) == sizeof(T_),                                                           \
                  "Graded alias " #GradedAlias " over " #T_ ": sizeof mismatch — review [[no_unique_address]] "    \
                  "usage and the lattice element type");                                                           \
    static_assert(alignof(GradedAlias<T_>) == alignof(T_),                                                         \
                  "Graded alias " #GradedAlias " over " #T_ ": alignof mismatch — an over-aligned grade type "     \
                  "raised the wrapper alignment above T's");                                                       \
    static_assert(std::is_trivially_destructible_v<T_> == std::is_trivially_destructible_v<GradedAlias<T_>>,       \
                  "Graded alias " #GradedAlias " over " #T_ ": trivial-destructibility parity broken — the grade " \
                  "type introduced a non-trivial destructor");                                                     \
    static_assert(std::is_trivially_copyable_v<T_> == std::is_trivially_copyable_v<GradedAlias<T_>>,               \
                  "Graded alias " #GradedAlias " over " #T_ ": trivial-copyability parity broken — the wrapper "   \
                  "is no longer memcpy-safe")

namespace detail::graded_self_test {

using ::foundation::algebra::detail::lattice_self_test::TrivialBoolLattice;

// A lattice declares its operations constexpr rather than consteval.
// Graded calls leq from a contract predicate, which runs with
// non-constant arguments under the enforce semantic.
struct TrivialEmptyLattice {
    using element_type = std::integral_constant<int, 1>;
    [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
    [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
    [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
    [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
    [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "TrivialEmpty"; }
};
static_assert(Lattice<TrivialEmptyLattice>);
static_assert(BoundedLattice<TrivialEmptyLattice>);
static_assert(std::is_empty_v<TrivialEmptyLattice::element_type>);

// A chain whose carrier can hold values outside the order.  The bounds
// check in the primary's two-argument constructor exists for exactly
// this shape: 9 is a perfectly good unsigned char and no element of
// the chain.
struct TrivialChainLattice {
    using element_type = unsigned char;
    [[nodiscard]] static constexpr element_type bottom() noexcept { return 0; }
    [[nodiscard]] static constexpr element_type top() noexcept { return 3; }
    [[nodiscard]] static constexpr bool leq(element_type a, element_type b) noexcept { return a <= b; }
    [[nodiscard]] static constexpr element_type join(element_type a, element_type b) noexcept { return a < b ? b : a; }
    [[nodiscard]] static constexpr element_type meet(element_type a, element_type b) noexcept { return a < b ? a : b; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "TrivialChain"; }
};
static_assert(BoundedLattice<TrivialChainLattice>);

struct EmptyValue {};
struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

using GComonad = Graded<ModalityKind::Comonad, TrivialBoolLattice, EmptyValue>;
using GRelMonad = Graded<ModalityKind::RelativeMonad, TrivialBoolLattice, EmptyValue>;
using GAbsolute = Graded<ModalityKind::Absolute, TrivialBoolLattice, EmptyValue>;
using GRelative = Graded<ModalityKind::Relative, TrivialBoolLattice, EmptyValue>;

static_assert(std::is_default_constructible_v<GComonad>);
static_assert(std::is_default_constructible_v<GRelMonad>);
static_assert(std::is_default_constructible_v<GAbsolute>);
static_assert(std::is_default_constructible_v<GRelative>);

static_assert(std::is_same_v<GAbsolute::value_type, EmptyValue>);
static_assert(std::is_same_v<GAbsolute::lattice_type, TrivialBoolLattice>);
static_assert(std::is_same_v<GAbsolute::grade_type, bool>);
static_assert(GAbsolute::modality == ModalityKind::Absolute);

static_assert(GAbsolute::modality_name() == "Absolute");
static_assert(GAbsolute::lattice_name() == "TrivialBool");
static_assert(GComonad::modality_name() == "Comonad");

static_assert(sizeof(GAbsolute) == 1);

using GOneByte = Graded<ModalityKind::Absolute, TrivialBoolLattice, OneByteValue>;
static_assert(sizeof(GOneByte) == 2);

using GEightByte = Graded<ModalityKind::Absolute, TrivialBoolLattice, EightByteValue>;
static_assert(sizeof(GEightByte) == 16);

using GEmptyGrade_Empty = Graded<ModalityKind::Absolute, TrivialEmptyLattice, EmptyValue>;
using GEmptyGrade_OneByte = Graded<ModalityKind::Absolute, TrivialEmptyLattice, OneByteValue>;
using GEmptyGrade_EightB = Graded<ModalityKind::Absolute, TrivialEmptyLattice, EightByteValue>;

static_assert(sizeof(GEmptyGrade_Empty) == 1);
static_assert(sizeof(GEmptyGrade_OneByte) == sizeof(OneByteValue));
static_assert(sizeof(GEmptyGrade_EightB) == sizeof(EightByteValue));

constexpr GOneByte g_at_top{OneByteValue{}, true};
static_assert(g_at_top.grade() == true);
static_assert(g_at_top.peek().c == 0);

constexpr GOneByte g_bot = GOneByte::at_bottom(OneByteValue{});
static_assert(g_bot.grade() == false);

constexpr GOneByte g_bot_noarg = GOneByte::at_bottom();
static_assert(g_bot_noarg.grade() == false);
static_assert(g_bot_noarg.peek().c == 0);

constexpr GOneByte g_weakened = g_bot.weaken(true);
static_assert(g_weakened.grade() == true);

constexpr GOneByte g_composed = g_bot.compose(g_at_top);
static_assert(g_composed.grade() == true);

// A stored grade inside the order constructs at compile time; one
// outside it fails the constructor's contract_assert and is not a
// constant expression.  The negative direction is
// test/foundation/neg/neg_graded_stored_grade_outside_the_order.cpp.
using GChainOneByte = Graded<ModalityKind::Absolute, TrivialChainLattice, OneByteValue>;
constexpr GChainOneByte g_chain_in_order{OneByteValue{}, static_cast<unsigned char>(2)};
static_assert(g_chain_in_order.grade() == 2);
static_assert(g_chain_in_order.weaken(static_cast<unsigned char>(3)).grade() == 3);

// The reachability tests go through named concepts.  An inline
// requires-expression against a member-function constraint is a hard
// error rather than a substitution failure.
template <typename G>
concept CanExtract = requires(G g) { std::move(g).extract(); };
template <typename G>
concept CanInject = requires { G::inject(typename G::value_type{}, typename G::grade_type{}); };

static_assert(CanExtract<GComonad>);
static_assert(!CanExtract<GAbsolute>);
static_assert(!CanExtract<GRelMonad>);
static_assert(!CanExtract<GRelative>);

static_assert(CanInject<GRelMonad>);
static_assert(!CanInject<GComonad>);
static_assert(!CanInject<GAbsolute>);
static_assert(!CanInject<GRelative>);

// at_bottom() must be reachable on all three specializations, subject
// only to each one's intrinsic requirement: the primary and the
// derived-grade form need a default-initializable T.
//
// at_bottom(T) is reachable on the primary alone.  These cells are the
// witness for that split, and they are what a reintroduction of the
// overload on either specialization would fail.  The overload once
// existed on all three with two different meanings — set the grade
// here, assert it there — so the value that this concept admits below
// is the value the other two aborted on.
template <typename G>
concept CanAtBottomNoArg = requires { G::at_bottom(); };
template <typename G>
concept CanAtBottomValue = requires(typename G::value_type v) { G::at_bottom(std::move(v)); };

// Grade stored beside the value: both forms.
static_assert(CanAtBottomNoArg<GOneByte>);
static_assert(CanAtBottomValue<GOneByte>);

// Grade is the value: the no-arg form only.
using GBoolElement = Graded<ModalityKind::Absolute, TrivialBoolLattice, bool>;
static_assert(CanAtBottomNoArg<GBoolElement>);
static_assert(!CanAtBottomValue<GBoolElement>,
              "at_bottom(T) on a lattice whose element type is T could only honour the request by "
              "discarding the argument.  Graded{value, L::bottom()} carries the checked form.");

// The equivalent the absent overload points callers at.  The witness
// check in the two-arg constructor is the assertion that this value is
// already at bottom, and it is not tautological: passing true here
// fails it.
constexpr GBoolElement g_bool_bot_checked{false, TrivialBoolLattice::bottom()};
static_assert(g_bool_bot_checked.grade() == TrivialBoolLattice::bottom());

// The derived-grade lattice is written out here rather than reused,
// because the real one includes this header.
struct MiniContainer {
    std::size_t n{0};
    constexpr MiniContainer() = default;
    constexpr explicit MiniContainer(std::size_t k) noexcept : n{k} {}
    [[nodiscard]] constexpr std::size_t size() const noexcept { return n; }
    constexpr bool operator==(const MiniContainer&) const = default;
};

struct MiniDerivedLattice {
    using element_type = std::size_t;
    static constexpr element_type bottom() noexcept { return 0; }
    static constexpr bool leq(element_type a, element_type b) noexcept { return a <= b; }
    static constexpr element_type join(element_type a, element_type b) noexcept { return a < b ? b : a; }
    static constexpr element_type meet(element_type a, element_type b) noexcept { return a < b ? a : b; }
    static constexpr element_type grade_of(MiniContainer const& c) noexcept { return c.size(); }
    static constexpr std::string_view name() noexcept { return "MiniDerivedLattice"; }
};
static_assert(Lattice<MiniDerivedLattice>);
static_assert(BoundedBelowLattice<MiniDerivedLattice>);
static_assert(LatticeDerivesGrade<MiniDerivedLattice, MiniContainer>);

using GDerivedSeq = Graded<ModalityKind::Absolute, MiniDerivedLattice, MiniContainer>;

// Grade derived from the value: the no-arg form only.
static_assert(CanAtBottomNoArg<GDerivedSeq>);
static_assert(!CanAtBottomValue<GDerivedSeq>,
              "at_bottom(T) on a derived-grade lattice would need an inverse of grade_of.  "
              "Graded{value, L::bottom()} carries the checked form.");

constexpr GDerivedSeq g_derived_bot_noarg = GDerivedSeq::at_bottom();
static_assert(g_derived_bot_noarg.grade() == MiniDerivedLattice::bottom());

// The equivalent the absent overload points callers at.  Not
// tautological: MiniContainer{3} fails the witness check.
constexpr GDerivedSeq g_derived_bot_checked{MiniContainer{}, MiniDerivedLattice::bottom()};
static_assert(g_derived_bot_checked.grade() == MiniDerivedLattice::bottom());

template <typename T>
using AbsoluteOverEmpty = Graded<ModalityKind::Absolute, TrivialEmptyLattice, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AbsoluteOverEmpty, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AbsoluteOverEmpty, EightByteValue);

CRUCIBLE_GRADED_LAYOUT_INVARIANT(AbsoluteOverEmpty, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(AbsoluteOverEmpty, double);

static_assert(std::is_trivially_default_constructible_v<int>);
static_assert(!std::is_trivially_default_constructible_v<AbsoluteOverEmpty<int>>,
              "The NSDMI on the members makes the implicit default constructor "
              "non-trivial even for a trivially default constructible T, so the "
              "layout-invariant macro must not assert parity on that trait.");

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

static_assert(HasConstWeaken<GOneByte>);
static_assert(HasRvalueWeaken<GOneByte>);
static_assert(!HasConstWeaken<GMoveOnly>);
static_assert(HasRvalueWeaken<GMoveOnly>);

// The function is inline rather than constexpr so that its body is
// checked against runtime semantics.  The contract predicates in the
// operations below then run with non-constant arguments, which a
// static_assert-only test never reaches.
inline void runtime_smoke_test() {
    OneByteValue value{42};
    GOneByte initial{value, false};
    GOneByte widened = initial.weaken(true);
    GOneByte composed = initial.compose(widened);
    GOneByte moved = std::move(widened).weaken(true);
    GOneByte mcomposed = std::move(initial).compose(composed);

    [[maybe_unused]] bool g1 = composed.grade();
    [[maybe_unused]] bool g2 = moved.grade();
    [[maybe_unused]] bool g3 = mcomposed.grade();
    [[maybe_unused]] auto v1 = composed.peek().c;
    [[maybe_unused]] auto v2 = std::move(mcomposed).consume().c;

    GOneByte prim_noarg = GOneByte::at_bottom();
    GOneByte prim_value = GOneByte::at_bottom(OneByteValue{static_cast<char>(value.c + 1)});
    [[maybe_unused]] bool gb1 = prim_noarg.grade();
    [[maybe_unused]] auto vb1 = prim_value.peek().c;

    // The two specializations publish at_bottom() alone.  The checked
    // form runs here with a non-constant argument, where the witness
    // predicate is evaluated under runtime semantics rather than folded
    // away.
    GBoolElement same_noarg = GBoolElement::at_bottom();
    GBoolElement same_checked{TrivialBoolLattice::bottom(), TrivialBoolLattice::bottom()};
    [[maybe_unused]] bool gs1 = same_noarg.grade();
    [[maybe_unused]] bool gs2 = same_checked.grade();

    GDerivedSeq der_noarg = GDerivedSeq::at_bottom();
    GDerivedSeq der_checked{MiniContainer{static_cast<std::size_t>(0)}, MiniDerivedLattice::bottom()};
    [[maybe_unused]] std::size_t gd1 = der_noarg.grade();
    [[maybe_unused]] std::size_t gd2 = der_checked.grade();

    // The stored-grade bounds check with a non-constant grade, so the
    // predicate runs under runtime semantics rather than being folded.
    unsigned char chain_grade = static_cast<unsigned char>(value.c % 4);  // 42 % 4 == 2, inside [0, 3]
    GChainOneByte chain_checked{value, chain_grade};
    [[maybe_unused]] unsigned char gc1 = chain_checked.grade();
    [[maybe_unused]] unsigned char gc2 = chain_checked.weaken(static_cast<unsigned char>(3)).grade();
}

}  // namespace detail::graded_self_test

// IsGraded is strict identity: it holds for a Graded specialization
// itself, not for a class that wraps one.

namespace detail {

template <typename>
inline constexpr bool is_graded_v_impl = false;

template <ModalityKind M, Lattice L, typename V>
inline constexpr bool is_graded_v_impl<Graded<M, L, V>> = true;

}  // namespace detail

template <typename T>
inline constexpr bool is_graded_v = detail::is_graded_v_impl<std::remove_cvref_t<T>>;

template <typename T>
concept IsGraded = is_graded_v<T>;

namespace detail::is_graded_self_test {

using GraderAB =
    Graded<ModalityKind::Absolute, ::foundation::algebra::detail::lattice_self_test::TrivialBoolLattice, bool>;

static_assert(IsGraded<GraderAB>);
static_assert(IsGraded<GraderAB const>);
static_assert(IsGraded<GraderAB&>);
static_assert(IsGraded<GraderAB&&>);

static_assert(!IsGraded<int>);
static_assert(!IsGraded<void>);
static_assert(!IsGraded<::foundation::algebra::detail::lattice_self_test::TrivialBoolLattice>);

}  // namespace detail::is_graded_self_test

}  // namespace foundation::algebra
