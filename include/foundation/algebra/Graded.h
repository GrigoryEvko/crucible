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
//
// Up is the weaker claim here, and weaken() and compose() can only
// promise less.  The order of a grade stored beside the value must have
// that orientation (ClaimOrientation.h).  The template head refuses a
// lattice that states the opposite orientation or states none.  A
// version counter in its numeric order is refused, and its dual is
// accepted.  Where the grade is the value, or grade_of derives it from
// the value, the grade cannot be false, and the head admits any
// orientation.
//
// A grade is a claim about the value, and a claim needs a key.  Every
// door that pairs a value with a grade that Graded does not derive takes
// a grade_key<Authority>: the two-argument constructor, inject, and the
// mutable reference from peek_mut.  Only the members of Authority can
// build that key, so each place that asserts a grade names its
// authority, and a search for grade_key< lists every one.  A lattice
// whose grade says nothing about the bytes (ClaimSubject::slot in
// ClaimOrientation.h) opens peek_mut, the default constructor and
// at_bottom() without a key.  Where the grade is the value, or is
// derived from it, construction from the value alone cannot pair it with
// a false grade and takes no key.

#include <foundation/algebra/ClaimOrientation.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/Modality.h>
#include <foundation/contracts/Pre.h>
#include <foundation/reflect/Instance.h>

#include <concepts>
#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace foundation::algebra {

// The passkey that pairs a value with a grade.
//
// The constructor is private and Authority is the one friend, so a key
// exists only inside a member of Authority.  Authority is the audit
// identity: a wrapper passes grade_key<Wrapper>, a door passes the class
// that holds the door.  The doors take the key by reference, so nothing
// outside Authority copies one either.
//
// Both constructors are user-provided.  A key whose copy is trivial is
// trivially copyable, and std::bit_cast builds one from any byte.  A key
// with a trivial constructor is an implicit-lifetime type, and a
// lifetime started over a buffer builds one.  Neither route names a
// constructor, so neither sees the access check.
template <typename Authority>
    requires std::is_class_v<Authority>
class [[nodiscard]] grade_key {
    friend Authority;
    constexpr grade_key() noexcept {}
    constexpr grade_key(grade_key const&) noexcept {}

public:
    grade_key& operator=(grade_key const&) = delete("a grade key lives for one expression.  Build a new one in the "
                                                   "authority.");
};

template <typename L, typename T>
concept LatticeDerivesGrade = requires(T const& v) {
    { L::grade_of(v) } -> std::same_as<typename L::element_type>;
};

// L grades a value of type T.  A grade stored beside the value is a
// claim about it.  weaken() and compose() move that grade up, and up in L
// must be the weaker claim.  A grade that is the value, or that grade_of
// derives from it, names the value itself and claims nothing that can be
// false.  Any orientation of L is permitted for such a grade.
template <typename L, typename T>
concept LatticeGradesValue = Lattice<L> && (GradableLattice<L> || std::same_as<LatticeElement<L>, T>
                                            || LatticeDerivesGrade<L, T>);

namespace detail::graded {

enum class Regime : std::uint8_t {
    stored,
    element,
    derived,
};

template <typename L, typename T>
[[nodiscard]] consteval Regime regime_of() noexcept {
    if constexpr (std::is_same_v<LatticeElement<L>, T>) {
        return Regime::element;
    } else if constexpr (LatticeDerivesGrade<L, T>) {
        return Regime::derived;
    } else {
        return Regime::stored;
    }
}

// The grade member of a regime whose grade is not stored.
struct no_stored_grade {};

// A carrier can hold values that no lattice element names: an enum cast
// from an integer, a byte above the top of a chain.  Every later leq
// would then compare against a grade outside the order.
template <typename L>
[[nodiscard]] constexpr bool inside_order(LatticeElement<L> const& grade) noexcept {
    bool inside = true;
    if constexpr (BoundedBelowLattice<L>) inside = inside && L::leq(L::bottom(), grade);
    if constexpr (BoundedAboveLattice<L>) inside = inside && L::leq(grade, L::top());
    return inside;
}

// A grade handed in beside a value whose grade is the value or is
// derived from it: it must name that grade, inside the order.
template <typename L>
[[nodiscard]] constexpr bool witnessed(LatticeElement<L> const& actual, LatticeElement<L> const& claimed) noexcept {
    return inside_order<L>(actual) && equivalent<L>(actual, claimed);
}

template <typename Self, typename T>
using forwarded_t = decltype(std::forward_like<Self>(std::declval<T&>()));

}  // namespace detail::graded

template <ModalityKind M, typename L, typename T>
    requires LatticeGradesValue<L, T>
class [[nodiscard]] Graded {
    static_assert(IsModality<M>, "Graded<M, L, T>: M must be one of Comonad / RelativeMonad / "
                                 "Absolute / Relative / Stepping.");

    static constexpr detail::graded::Regime regime_ = detail::graded::regime_of<L, T>();
    static constexpr bool stores_grade_ = regime_ == detail::graded::Regime::stored;
    static constexpr bool grade_is_value_ = regime_ == detail::graded::Regime::element;
    static constexpr bool grade_is_derived_ = regime_ == detail::graded::Regime::derived;

public:
    static constexpr ModalityKind modality = M;

    using modality_kind_type = ModalityKind;
    using lattice_type = L;
    using value_type = T;
    using grade_type = LatticeElement<L>;

private:
    using stored_grade_type = std::conditional_t<stores_grade_, grade_type, detail::graded::no_stored_grade>;

    // Writing the value in place leaves the grade where it was.  That is
    // sound for an Absolute grade and for an empty one, and only for
    // those: a Comonad or RelativeMonad grade over a non-empty element
    // names the specific value, and no key reopens it.
    static constexpr bool mutation_admitted_ = AbsoluteModality<M> || std::is_empty_v<grade_type>;

    // A rebuild of this value at another grade, from an object of
    // category Self.  Where the grade is the value, the value is not read.
    template <typename Self>
    static constexpr bool can_regrade_ =
        grade_is_value_ || std::is_constructible_v<T, detail::graded::forwarded_t<Self, T>>;
    template <typename Self>
    static constexpr bool regrade_is_nothrow_ =
        (grade_is_value_ || std::is_nothrow_constructible_v<T, detail::graded::forwarded_t<Self, T>>)
        && std::is_nothrow_copy_constructible_v<grade_type> && std::is_nothrow_move_constructible_v<grade_type>;

    static constexpr bool grade_is_nothrow_ = [] {
        if constexpr (stores_grade_) return std::is_nothrow_copy_constructible_v<grade_type>;
        else if constexpr (grade_is_value_) return std::is_nothrow_copy_constructible_v<T>;
        else return noexcept(L::grade_of(std::declval<T const&>()));
    }();

    [[no_unique_address]] T value_{};
    [[no_unique_address]] stored_grade_type grade_{};

    [[nodiscard]] static constexpr stored_grade_type stored_(grade_type& grade) noexcept(
        std::is_nothrow_move_constructible_v<grade_type>) {
        if constexpr (stores_grade_) {
            return std::move(grade);
        } else {
            return stored_grade_type{};
        }
    }

    // weaken and compose each rebuild the value at a higher grade.  Self
    // can be a class derived from Graded, so the value is named through
    // Graded, which is the one class that can reach it.
    template <typename Self>
    [[nodiscard]] static constexpr Graded regrade_(Self&& self, grade_type grade) noexcept(regrade_is_nothrow_<Self>) {
        if constexpr (grade_is_value_) {
            return Graded{std::move(grade)};
        } else {
            return Graded{grade_key<Graded>{}, std::forward_like<Self>(self.Graded::value_), std::move(grade)};
        }
    }

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

    // A default value is paired with a stored grade that nothing
    // witnesses, so the stored regime builds one only for a grade that
    // any bytes satisfy.  The other two regimes derive the grade.
    constexpr Graded()
        requires(!stores_grade_ || GradeIgnoresBytes<L>)
    = default;
    constexpr Graded(const Graded&) = default;
    constexpr Graded(Graded&&) = default;
    constexpr Graded& operator=(const Graded&) = default;
    constexpr Graded& operator=(Graded&&) = default;
    ~Graded() = default;

    // The door that pairs a value with a grade.  In the stored regime the
    // grade is the claim, and it is checked against the order.  In the
    // other two it is a witness: it must name the grade that the value
    // already has.
    //
    // The checks are CRUCIBLE_PRE, which stops a constant evaluation
    // under every evaluation semantic and checks at run time under
    // enforce and observe.  A pre() clause would not do: its predicate
    // reads a member, and GCC skips a pre() clause that reads a member
    // during the constant evaluation of a foldable body.
    template <typename Authority>
    constexpr Graded(grade_key<Authority> const&, T value,
                     grade_type grade) noexcept(std::is_nothrow_move_constructible_v<T>
                                                && std::is_nothrow_move_constructible_v<grade_type>)
        : value_{std::move(value)}, grade_{stored_(grade)} {
        if constexpr (stores_grade_) {
            CRUCIBLE_PRE(detail::graded::inside_order<L>(grade_));
        } else {
            CRUCIBLE_PRE(detail::graded::witnessed<L>(this->grade(), grade));
        }
    }

    // Construction from the value alone, where the grade is the value or
    // is derived from it.  Neither pairing can be false, so no key is
    // asked.  A value that is its own grade is still checked against the
    // order.
    constexpr explicit Graded(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        requires(!stores_grade_)
        : value_{std::move(value)} {
        if constexpr (grade_is_value_) {
            CRUCIBLE_PRE(detail::graded::inside_order<L>(value_));
        }
    }

    // The bottom-graded default value.  In the derived regime the check
    // is not tautological: grade_of reads the value, so a T whose default
    // state grades above bottom fires it.
    //
    // There is no at_bottom(T).  In the stored regime it would be the
    // strongest claim about a value the caller chose, which is the keyed
    // constructor with L::bottom().  In the other two it could only
    // discard the argument or invert grade_of.
    [[nodiscard]] static constexpr Graded at_bottom() noexcept(std::is_nothrow_move_constructible_v<T>
                                                               && (grade_is_value_
                                                                   || std::is_nothrow_default_constructible_v<T>))
        requires BoundedBelowLattice<L> && (grade_is_value_ || std::default_initializable<T>)
              && (!stores_grade_ || GradeIgnoresBytes<L>)
    {
        if constexpr (grade_is_value_) {
            return Graded{L::bottom()};
        } else if constexpr (grade_is_derived_) {
            Graded result{T{}};
            CRUCIBLE_PRE(equivalent<L>(result.grade(), L::bottom()));
            return result;
        } else {
            return Graded{grade_key<Graded>{}, T{}, L::bottom()};
        }
    }

    // grade() recomputes on every call in the derived regime.  A caller
    // that needs it more than once should hold the result, because the
    // cost of grade_of is the lattice's to choose.
    [[nodiscard]] constexpr T const& peek() const& noexcept { return value_; }
    [[nodiscard]] constexpr T consume() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return std::move(value_);
    }
    [[nodiscard]] constexpr grade_type grade() const noexcept(grade_is_nothrow_) {
        if constexpr (stores_grade_) {
            return grade_;
        } else if constexpr (grade_is_value_) {
            return value_;
        } else {
            return L::grade_of(value_);
        }
    }

    // A mutable reference replaces the bytes under the grade, and the
    // grade stays.  Only a grade that any bytes satisfy survives that, so
    // the keyless form exists for such a grade alone.  The keyed form
    // leaves the grade's truth to the authority that holds the key, and
    // where the grade is derived, keeping its movement inside the lattice
    // order is the authority's obligation too.
    [[nodiscard]] constexpr T& peek_mut() & noexcept
        requires mutation_admitted_ && GradeIgnoresBytes<L>
    {
        return value_;
    }

    template <typename Authority>
    [[nodiscard]] constexpr T& peek_mut(grade_key<Authority> const&) & noexcept
        requires mutation_admitted_
    {
        return value_;
    }

    // Each value leaves with its own grade, so a swap keeps every pairing
    // it was given.
    constexpr void swap(Graded& other) noexcept(std::is_nothrow_swappable_v<T>
                                                && std::is_nothrow_swappable_v<stored_grade_type>)
        requires mutation_admitted_
    {
        using std::swap;
        swap(value_, other.value_);
        swap(grade_, other.grade_);
    }

    friend constexpr void swap(Graded& a, Graded& b) noexcept(std::is_nothrow_swappable_v<T>
                                                              && std::is_nothrow_swappable_v<stored_grade_type>)
        requires mutation_admitted_
    {
        a.swap(b);
    }

    [[nodiscard]] constexpr T extract() && noexcept(std::is_nothrow_move_constructible_v<T>)
        requires ComonadModality<M>
    {
        return std::move(value_);
    }

    // The unit of a relative monad: a bare value in at a chosen grade.
    // The grade is a claim, so the unit takes the key the constructor
    // takes.
    template <typename Authority>
    [[nodiscard]] static constexpr Graded inject(grade_key<Authority> const& key, T value,
                                                 grade_type grade) noexcept(std::is_nothrow_move_constructible_v<T>
                                                                            && std::is_nothrow_move_constructible_v<
                                                                                grade_type>)
        requires RelativeMonadModality<M>
    {
        return Graded{key, std::move(value), std::move(grade)};
    }

    // Weakening moves up the lattice and never down.  That is the whole
    // of what Graded promises, so the guard below is the promise.  It is
    // an in-body CRUCIBLE_PRE for the reason the keyed constructor gives.
    //
    // Neither operation exists where the grade is derived from the value.
    // Both produce a value at a grade the caller names, which would need
    // an inverse of grade_of, and none exists in general.  Where one does
    // it belongs to the wrapper, which mutates the value and lets the
    // derived grade follow.
    //
    // A const or lvalue object copies its value and an rvalue moves it,
    // so a move-only T weakens only from an rvalue.
    template <typename Self>
    [[nodiscard]] constexpr Graded weaken(this Self&& self, grade_type new_grade) noexcept(regrade_is_nothrow_<Self>)
        requires(!grade_is_derived_) && can_regrade_<Self>
    {
        CRUCIBLE_PRE(L::leq(self.Graded::grade(), new_grade));
        return regrade_(std::forward<Self>(self), std::move(new_grade));
    }

    template <typename Self>
    [[nodiscard]] constexpr Graded compose(this Self&& self, Graded const& other) noexcept(regrade_is_nothrow_<Self>)
        requires(!grade_is_derived_) && can_regrade_<Self>
    {
        grade_type joined = L::join(self.Graded::grade(), other.grade());
        return regrade_(std::forward<Self>(self), std::move(joined));
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

// The authority for the cells below.  It hands its key out, which an
// authority in production code never does.
struct self_test_authority {
    [[nodiscard]] static constexpr grade_key<self_test_authority> key() noexcept {
        return grade_key<self_test_authority>{};
    }
};

// A class that is not an authority for anything.
struct stranger {};

static_assert(!std::is_default_constructible_v<grade_key<stranger>>, "only the authority builds its key");
static_assert(!std::is_copy_constructible_v<grade_key<self_test_authority>>);
static_assert(!std::is_trivially_copyable_v<grade_key<self_test_authority>>, "std::bit_cast must not build a key");
static_assert(!std::is_implicit_lifetime_v<grade_key<self_test_authority>>,
              "a lifetime started over a buffer must not build a key");

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

// The same two-point order, read as a claim about the slot.  It is the
// lattice under which Graded opens its keyless doors.
struct TrivialSlotLattice : TrivialBoolLattice {
    static constexpr ClaimSubject claim_subject = ClaimSubject::slot;
    [[nodiscard]] static consteval std::string_view name() noexcept { return "TrivialSlot"; }
};
static_assert(BoundedLattice<TrivialSlotLattice> && GradeIgnoresBytes<TrivialSlotLattice>);
static_assert(!GradeIgnoresBytes<TrivialBoolLattice>);

// A chain whose carrier can hold values outside the order.  The bounds
// check in the keyed constructor exists for exactly this shape: 9 is a
// perfectly good unsigned char and no element of the chain.
struct TrivialChainLattice {
    using element_type = unsigned char;
    static constexpr ClaimOrientation claim_orientation = ClaimOrientation::weaker_is_higher;
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
using GSlotOneByte = Graded<ModalityKind::Absolute, TrivialSlotLattice, OneByteValue>;
static_assert(std::is_default_constructible_v<GSlotOneByte>);
constexpr GSlotOneByte g_slot_bot = GSlotOneByte::at_bottom();
static_assert(g_slot_bot.grade() == false);
static_assert(g_slot_bot.peek().c == 0);

// A stored grade inside the order constructs at compile time; one
// outside it fails the constructor's check and is not a constant
// expression.  The negative direction is
// test/foundation/neg/neg_graded_stored_grade_outside_the_order.cpp.
using GChainOneByte = Graded<ModalityKind::Absolute, TrivialChainLattice, OneByteValue>;
constexpr GChainOneByte g_chain_in_order{self_test_authority::key(), OneByteValue{}, static_cast<unsigned char>(2)};
static_assert(g_chain_in_order.grade() == 2);
static_assert(g_chain_in_order.weaken(static_cast<unsigned char>(3)).grade() == 3);

// The same chain as its own grade.  The value is checked against the
// order as the stored grade is; the negative direction is
// test/foundation/neg/neg_graded_element_grade_outside_the_order.cpp.
using GChainElement = Graded<ModalityKind::Absolute, TrivialChainLattice, unsigned char>;
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
static_assert(!CanNameGraded<UnstatedChainLattice, OneByteValue>, "an unstated orientation is refused as a stored grade");
static_assert(CanNameGraded<TrivialChainLattice, OneByteValue>);

// The reachability tests go through named concepts.  An inline
// requires-expression against a member-function constraint is a hard
// error rather than a substitution failure.
template <typename G>
concept CanExtract = requires(G g) { std::move(g).extract(); };
template <typename G>
concept CanInject =
    requires { G::inject(self_test_authority::key(), typename G::value_type{}, typename G::grade_type{}); };
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

// peek_mut without a key exists for a grade that any bytes satisfy.  The
// keyed form exists wherever a write in place is sound at all.
template <typename G>
concept CanPeekMut = requires(G& g) { g.peek_mut(); };
template <typename G>
concept CanPeekMutWithKey = requires(G& g) { g.peek_mut(self_test_authority::key()); };

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
using GBoolElement = Graded<ModalityKind::Absolute, TrivialBoolLattice, bool>;
static_assert(CanAtBottomNoArg<GBoolElement>);
static_assert(!CanAtBottomValue<GBoolElement>,
              "at_bottom(T) on a lattice whose element type is T could only honour the request by "
              "discarding the argument.");
static_assert(std::is_default_constructible_v<GBoolElement>);

// The keyed constructor's witness is the assertion that this value is
// already at bottom, and it is not tautological: passing true fails it.
constexpr GBoolElement g_bool_bot_checked{self_test_authority::key(), false, TrivialBoolLattice::bottom()};
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

// IsGraded is strict identity: it holds for a Graded specialization
// itself, not for a class that wraps one or derives from one.  It is
// the reflection query of foundation/reflect/Instance.h asked of this
// template, so a trait or a variable template specialized from another
// translation unit changes nothing it reads.  An explicit
// specialization of Graded itself is an instance of the template, and
// the query admits it.  Such a specialization declares whatever members
// it likes, a public value and no key among them, and nothing in the
// language refuses it.

template <typename T>
concept IsGraded = ::foundation::reflect::IsInstanceOf<T, ^^Graded>;

template <typename T>
inline constexpr bool is_graded_v = IsGraded<T>;

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
