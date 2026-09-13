#pragma once

// There is deliberately no implicit conversion to T. A refined value
// must not pass silently into a function that takes the bare type.
//
// Where contracts are compiled out, the constructor's precondition
// becomes an unconditional assumption handed to the optimizer rather
// than a check. A Refined therefore carries its invariant as a promise
// to the optimizer, not as a guard. That is correct and free for a
// value the surrounding code structurally guarantees. It is unsound
// for a value taken straight from an untrusted source: a malformed
// value satisfies the assumption vacuously and propagates downstream
// as a false invariant, feeding, say, an unreachable default arm.
//
// So at a trust boundary the value gets a real branch of its own,
// outside the contract system, before the Refined is constructed. The
// constructor then stands as typed defence in depth: its precondition
// holds by construction and never fires, while the type keeps carrying
// the invariant for the optimizer and for every later reader.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/BoolLattice.h>
#include <crucible/safety/Linear.h>

#include <bit>
#include <compare>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// Every predicate is stateless, so that it can serve as a template
// argument.

inline constexpr auto positive = [](auto x) constexpr noexcept { return x > decltype(x){0}; };

inline constexpr auto non_negative = [](auto x) constexpr noexcept { return x >= decltype(x){0}; };

// For an unsigned type this coincides with positive. The two are kept
// apart because they state different intents: non_zero reserves a
// sentinel, positive claims a sign class.
inline constexpr auto non_zero = [](const auto& x) constexpr noexcept {
    if constexpr (requires { x.raw(); })
        return x.raw() != 0;
    else
        return x != decltype(x){0};
};

// The dual of non_zero. It holds where a wire or disk format reserves
// zero as the only valid payload for a field, so that the must-be-zero
// invariant lives in the type instead of being discovered by reading a
// write routine and noticing the zero literal.
inline constexpr auto is_zero = [](const auto& x) constexpr noexcept {
    if constexpr (requires { x.raw(); })
        return x.raw() == 0;
    else
        return x == decltype(x){0};
};

inline constexpr auto non_null = [](auto* p) constexpr noexcept { return p != nullptr; };

inline constexpr auto power_of_two = [](auto x) constexpr noexcept {
    using U = decltype(x);
    return x != U{0} && (x & (x - U{1})) == U{0};
};

inline constexpr auto non_empty = [](const auto& c) constexpr noexcept { return !c.empty(); };

// Each parameterised predicate is a named struct template rather than
// a variable template of lambdas. A lambda produces a distinct
// unnamed closure type per parameter, and the compiler cannot
// pattern-match that back to the parameter, so the implication trait
// at the foot of this file could not partial-specialise on it. Callers
// are unaffected, since the paired variable template still reads as a
// call.
//
// Each predicate therefore ships in two pieces: the struct template is
// the type that the implication trait specialises on, and the variable
// template is the value that call sites pass.

template <std::size_t Alignment>
struct Aligned {
    constexpr bool operator()(auto* p) const noexcept {
        return (std::bit_cast<std::uintptr_t>(p) & (Alignment - 1)) == 0;
    }
};

template <std::size_t Alignment>
inline constexpr Aligned<Alignment> aligned{};

template <auto Lo, auto Hi>
struct InRange {
    constexpr bool operator()(auto x) const noexcept { return x >= decltype(x)(Lo) && x <= decltype(x)(Hi); }
};

template <auto Lo, auto Hi>
inline constexpr InRange<Lo, Hi> in_range{};

template <auto Max>
struct BoundedAbove {
    constexpr bool operator()(auto x) const noexcept { return x <= decltype(x)(Max); }
};

template <auto Max>
inline constexpr BoundedAbove<Max> bounded_above{};

template <std::size_t N>
struct LengthGe {
    constexpr bool operator()(const auto& c) const noexcept { return c.size() >= N; }
};

template <std::size_t N>
inline constexpr LengthGe<N> length_ge{};

// This gates the construction path, never the class template itself.
// The subsort machinery reasons over a Refined type without ever
// constructing one, so a requires-clause on the class template would
// make those types unnameable and break that discipline. The
// constructor is also the only place the predicate is actually
// evaluated, and gating it turns what would be a SFINAE cascade inside
// the contract clause into one concept-violation message at the call
// site.
template <auto Pred, typename T>
concept PredicateInvocableOn = requires(T const& v) {
    { Pred(v) } -> std::convertible_to<bool>;
};

template <auto Pred, typename T>
class [[nodiscard]] Refined {
public:
    using value_type = T;
    using predicate_type = decltype(Pred);
    // The lattice takes the predicate's type, and the const strip
    // matters: an inline constexpr predicate variable is const at file
    // scope while the template argument that binds it is not.
    using lattice_type = ::crucible::algebra::lattices::BoolLattice<std::remove_cv_t<decltype(Pred)>>;

    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;

    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;

private:
    graded_type impl_;

public:
    // Skips the predicate check. Use it only where the caller has
    // already proven the invariant.
    struct Trusted {};

    constexpr explicit Refined(T v) noexcept(std::is_nothrow_move_constructible_v<T>)
        requires PredicateInvocableOn<Pred, T>
    pre(Pred(v)) : impl_{std::move(v), typename lattice_type::element_type{}} {}

    // No invocability requirement here: this path bypasses the
    // predicate entirely, so even a predicate that cannot be called on
    // T is admissible and the caller owns the invariant.
    constexpr Refined(T v, Trusted) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(v), typename lattice_type::element_type{}} {}

    // The refinement is a property of the value, so copying or moving
    // preserves it and neither needs to re-check.
    Refined(const Refined&) = default;
    Refined(Refined&&) = default;
    Refined& operator=(const Refined&) = default;
    Refined& operator=(Refined&&) = default;

    [[nodiscard]] constexpr const T& value() const noexcept { return impl_.peek(); }

    [[nodiscard]] constexpr T into() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return std::move(impl_).consume();
    }

    friend constexpr bool operator==(const Refined& a,
                                     const Refined& b) noexcept(noexcept(a.impl_.peek() == b.impl_.peek())) {
        return a.impl_.peek() == b.impl_.peek();
    }

    friend constexpr auto operator<=>(const Refined& a,
                                      const Refined& b) noexcept(noexcept(a.impl_.peek() <=> b.impl_.peek()))
        requires std::three_way_comparable<T>
    {
        return a.impl_.peek() <=> b.impl_.peek();
    }

    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept { return graded_type::lattice_name(); }
};

static_assert(sizeof(Refined<positive, int>) == sizeof(int));
static_assert(sizeof(Refined<non_null, void*>) == sizeof(void*));
static_assert(sizeof(Refined<power_of_two, std::size_t>) == sizeof(std::size_t));

// The parameterised predicates are empty classes, so they too collapse
// inside the wrapper.
static_assert(sizeof(Refined<aligned<64>, void*>) == sizeof(void*));
static_assert(sizeof(Refined<bounded_above<1024u>, int>) == sizeof(int));
static_assert(sizeof(Refined<in_range<0, 100>, int>) == sizeof(int));
static_assert(sizeof(Refined<length_ge<1>, void*>) == sizeof(void*));

// Every factory that mints an authoritative value is named mint_, so
// that one search finds every authorization point. Constructing a
// Refined directly compiles but escapes that search, so code admitting
// a value into the refinement system goes through here.

template <auto Pred, typename T>
    requires PredicateInvocableOn<Pred, T>
[[nodiscard]] constexpr Refined<Pred, T> mint_refined(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
    return Refined<Pred, T>{std::move(value)};
}

// Every load-bearing predicate gets a named alias, so that it takes
// part in review rather than drifting into an anonymous refinement at
// each call site.

template <typename T>
using NonNull = Refined<non_null, T>;
template <typename T>
using Positive = Refined<positive, T>;
template <typename T>
using NonNegative = Refined<non_negative, T>;
template <typename T>
using PowerOfTwo = Refined<power_of_two, T>;
template <typename T>
using NonZero = Refined<non_zero, T>;
template <typename T>
using NonEmpty = Refined<non_empty, T>;

// This uses length_ge<1> rather than non_empty because length_ge
// participates in the implication lattice below, so a non-empty span
// strengthens to a longer-minimum one without being re-validated.
// non_empty stays useful for a container whose size is not cheap to
// compute, which a span's is.
template <typename T>
using NonEmptySpan = Refined<length_ge<std::size_t{1}>, std::span<T>>;

// N of zero is permitted here as a degenerate any-length alias,
// because the underlying predicate is vacuous at zero. The implication
// lattice below correctly refuses to bridge that case to non_empty.
template <std::size_t N, typename T>
using MinLength = Refined<length_ge<N>, T>;
template <auto Max, typename T>
using MaxBounded = Refined<bounded_above<Max>, T>;

// Two traps neither alias rejects. An N that is not a power of two
// still compiles and turns the alignment test into an arbitrary
// low-bit mask. An inverted bound with Lo above Hi also still
// compiles, and the predicate becomes vacuously false, so the failure
// surfaces at construction rather than at compile time.
template <std::size_t N, typename T>
using AlignedTo = Refined<aligned<N>, T>;
template <auto Lo, auto Hi, typename T>
using WithinRange = Refined<in_range<Lo, Hi>, T>;

// The two nesting orders mean different things, so the aliases exist
// to keep the choice deliberate rather than reorderable.
//
// In LinearRefined the value is refined and the wrapper is linear: the
// predicate is about the underlying T and ownership is
// single-consumer. This is the common case, because most resources
// are a handle to a value satisfying an invariant.
//
// In RefinedLinear the wrapper is refined and the inner value is
// linear: the predicate is about the ownership state itself, not about
// T. This is rare.

template <auto Pred, typename T>
using LinearRefined = Linear<Refined<Pred, T>>;

template <auto Pred, typename T>
using RefinedLinear = Refined<Pred, Linear<T>>;

static_assert(sizeof(LinearRefined<non_null, void*>) == sizeof(void*), "LinearRefined must collapse to sizeof(T)");

// One representative shape per alias: a scalar, a container that has
// both an emptiness test and a size, and a parameterised length bound
// over that container.
static_assert(sizeof(NonZero<int>) == sizeof(int));
static_assert(sizeof(NonEmpty<std::span<int>>) == sizeof(std::span<int>));
static_assert(sizeof(NonEmptySpan<int>) == sizeof(std::span<int>));

// implies_v<P, Q> reads: every value satisfying P also satisfies Q.
// P is therefore at least as strong as Q, and P's truth set is
// contained in Q's. Reversing the reading inverts every axiom below.
//
// Extend by specialising the type-level trait, never the value-level
// one. Template-argument deduction cannot recover the parameters of a
// bound value's type, so a partial specialisation written against the
// values fails as non-deducible. Pattern matching works on types, and
// the value-level trait only forwards through decltype.
//
// Each specialisation strips const from the predicate's type: an
// inline constexpr predicate variable is const in the enclosing scope
// while the template argument that binds it is not.
//
// The primary template is false and deliberately does not install
// reflexivity. The subsort machinery already supplies it from a
// same-type fall-through, and stating it twice invites the two to
// drift apart.

template <typename PType, typename QType>
struct predicate_implies : std::false_type {};

template <auto P, auto Q>
inline constexpr bool implies_v = predicate_implies<decltype(P), decltype(Q)>::value;

// positive ⇒ non_negative, because x > 0 gives x ≥ 0.
// positive ⇒ non_zero, because x > 0 gives x ≠ 0.
// power_of_two ⇒ non_zero, because the definition excludes zero.

template <>
struct predicate_implies<std::remove_cv_t<decltype(positive)>, std::remove_cv_t<decltype(non_negative)>>
    : std::true_type {};

template <>
struct predicate_implies<std::remove_cv_t<decltype(positive)>, std::remove_cv_t<decltype(non_zero)>> : std::true_type {
};

template <>
struct predicate_implies<std::remove_cv_t<decltype(power_of_two)>, std::remove_cv_t<decltype(non_zero)>>
    : std::true_type {};

// non_null and non_zero imply each other. For a pointer, the zero
// value of the pointer type is the null pointer, so the two predicates
// evaluate identically. The pair is safe to state in both directions
// because non_null only accepts a pointer argument, so for any
// non-pointer type the opposite direction names a type that cannot be
// formed at all.

template <>
struct predicate_implies<std::remove_cv_t<decltype(non_null)>, std::remove_cv_t<decltype(non_zero)>> : std::true_type {
};

template <>
struct predicate_implies<std::remove_cv_t<decltype(non_zero)>, std::remove_cv_t<decltype(non_null)>> : std::true_type {
};

// Aligned<N> ⇒ Aligned<M> when N ≥ M and M divides N, so a
// cache-line-aligned pointer is also word-aligned.
template <std::size_t N, std::size_t M>
    requires(N >= M && M > 0 && (N % M == 0))
struct predicate_implies<Aligned<N>, Aligned<M>> : std::true_type {};

// A smaller ceiling implies a larger one.
template <auto N, auto M>
    requires(N <= M)
struct predicate_implies<BoundedAbove<N>, BoundedAbove<M>> : std::true_type {};

// A tighter range implies a looser one.
template <auto L1, auto H1, auto L2, auto H2>
    requires(L2 <= L1 && H1 <= H2)
struct predicate_implies<InRange<L1, H1>, InRange<L2, H2>> : std::true_type {};

// A range ceiling is an upper bound.
template <auto L, auto H>
struct predicate_implies<InRange<L, H>, BoundedAbove<H>> : std::true_type {};

// A longer minimum implies a shorter one.
template <std::size_t N, std::size_t M>
    requires(N >= M)
struct predicate_implies<LengthGe<N>, LengthGe<M>> : std::true_type {};

// A container's emptiness test equals a size of zero, so a minimum
// length of one or more implies non-emptiness. The clause excluding
// zero is load-bearing: a size is unsigned, so a minimum of zero is
// vacuously true and an empty container satisfies it.
template <std::size_t N>
    requires(N >= 1)
struct predicate_implies<LengthGe<N>, std::remove_cv_t<decltype(non_empty)>> : std::true_type {};

// The lower-bound conjunct gives x ≥ L, so a non-negative L implies a
// non-negative x. A negative L admits negative values and is excluded
// by the clause. L keeps its own type, so the test holds for a signed
// and an unsigned bound alike.
template <auto L, auto H>
    requires(L >= 0)
struct predicate_implies<InRange<L, H>, std::remove_cv_t<decltype(non_negative)>> : std::true_type {};

// This trait does not compose transitively. The subsorting rule that
// consumes it demands a direct implication, so a conclusion reachable
// only by chaining two axioms is not reachable through it at all.
// Every hop production code relies on therefore needs a specialisation
// of its own, which is why this bridge exists alongside the pair it
// could be derived from.
//
// A lower bound of one or more gives x ≥ 1 and so x > 0. A bound of
// zero admits zero, which is not positive, hence the clause.
template <auto L, auto H>
    requires(L >= 1)
struct predicate_implies<InRange<L, H>, std::remove_cv_t<decltype(positive)>> : std::true_type {};

// non_zero is a union of two half-lines rather than one, so the gate
// here is a disjunction: either bound alone suffices to keep zero out
// of the range. A lower bound of one or more excludes it from above,
// and an upper bound of minus one or less excludes it from below. The
// second branch is what admits a wholly negative range, which the
// positive branch alone would miss.
//
// A bound strictly between zero and one is conservatively excluded,
// which under-asserts rather than risking a truncation.
template <auto L, auto H>
    requires(L >= 1 || H <= -1)
struct predicate_implies<InRange<L, H>, std::remove_cv_t<decltype(non_zero)>> : std::true_type {};

// Each axiom above is witnessed here, positively and at the boundary
// where its soundness clause bites. Because the trait does not compose
// transitively, each hop of a chain is witnessed on its own line
// rather than inferred from its neighbours.

static_assert(implies_v<non_null, non_zero>, "non_null ⇒ non_zero: a non-null pointer is a non-zero pointer.");
static_assert(implies_v<non_zero, non_null>, "non_zero ⇒ non_null: the implication is bidirectional for a pointer.");
static_assert(implies_v<length_ge<1>, non_empty>, "length_ge<1> ⇒ non_empty: a size of at least one is not empty.");
static_assert(implies_v<length_ge<8>, non_empty>, "length_ge<N> ⇒ non_empty for every N of at least one.");
static_assert(!implies_v<length_ge<0>, non_empty>, "length_ge<0> is vacuous and must NOT imply non_empty: an empty "
                                                   "container satisfies it.");

static_assert(implies_v<length_ge<8>, length_ge<1>>, "length_ge<8> ⇒ length_ge<1>: the longer minimum is stronger.");

static_assert(implies_v<in_range<0, 100>, non_negative>, "in_range<0, 100> ⇒ non_negative: the lower bound is zero.");
static_assert(implies_v<in_range<5, 100>, non_negative>,
              "in_range<5, 100> ⇒ non_negative: the lower bound is positive.");
static_assert(implies_v<in_range<0u, 255u>, non_negative>, "An unsigned bound carries non_negative trivially.");
static_assert(!implies_v<in_range<-5, 100>, non_negative>,
              "in_range<-5, 100> admits negative values and must NOT imply "
              "non_negative: the lower-bound clause is load-bearing.");

static_assert(implies_v<in_range<5, 100>, in_range<0, 200>>,
              "in_range<5, 100> ⇒ in_range<0, 200>: a tighter range implies a looser one.");

static_assert(implies_v<in_range<1, 100>, positive>, "in_range<1, 100> ⇒ positive at the boundary lower bound.");
static_assert(implies_v<in_range<5, 100>, positive>, "in_range<5, 100> ⇒ positive at an interior lower bound.");
static_assert(!implies_v<in_range<0, 100>, positive>, "in_range<0, 100> must NOT imply positive: it admits zero, "
                                                      "which is why the lower-bound clause is load-bearing.");
static_assert(!implies_v<in_range<-5, 100>, positive>, "in_range<-5, 100> must NOT imply positive: it admits negative "
                                                       "values.");

static_assert(implies_v<in_range<1, 100>, non_zero>, "in_range<1, 100> ⇒ non_zero at the boundary lower bound.");
static_assert(implies_v<in_range<5, 100>, non_zero>, "in_range<5, 100> ⇒ non_zero at an interior lower bound.");

static_assert(implies_v<in_range<-100, -1>, non_zero>, "in_range<-100, -1> ⇒ non_zero at the boundary upper bound.");
static_assert(implies_v<in_range<-100, -5>, non_zero>, "in_range<-100, -5> ⇒ non_zero at an interior upper bound.");

static_assert(!implies_v<in_range<0, 100>, non_zero>, "in_range<0, 100> must NOT imply non_zero: it admits zero, and "
                                                      "neither branch of the disjunctive clause holds.");
static_assert(!implies_v<in_range<-5, 5>, non_zero>, "in_range<-5, 5> must NOT imply non_zero: the range straddles "
                                                     "zero.");
static_assert(!implies_v<in_range<-100, 0>, non_zero>,
              "in_range<-100, 0> must NOT imply non_zero: it admits zero at the "
              "upper bound.");

namespace detail::refined_self_test {

inline void runtime_smoke_test() {
    int seed = 42;

    Refined<positive, int> p{seed};
    if (p.value() != 42) std::abort();

    auto pm = mint_refined<positive, int>(seed);
    if (pm.value() != 42) std::abort();

    int sentinel = -1;  // fails the predicate, which Trusted admits
    Refined<positive, int> tp{sentinel, Refined<positive, int>::Trusted{}};
    if (tp.value() != -1) std::abort();

    Refined<positive, int> p2{seed};
    if (!(p == p2)) std::abort();
    Refined<positive, int> p3{seed + 1};
    if ((p <=> p3) != std::strong_ordering::less) std::abort();

    int extracted = std::move(p).into();
    if (extracted != 42) std::abort();

    Refined<bounded_above<128u>, unsigned int> ba{static_cast<unsigned int>(seed)};
    if (ba.value() != 42u) std::abort();

    Refined<in_range<0, 100>, int> ir{seed};
    if (ir.value() != 42) std::abort();

    int arr[3] = {1, 2, 3};
    std::span<int> sp{arr};
    Refined<length_ge<1>, std::span<int>> ls{sp};
    if (ls.value().size() != 3) std::abort();

    LinearRefined<positive, int> lr{Refined<positive, int>{seed}};
    if (lr.peek().value() != 42) std::abort();
    int lr_extracted = std::move(lr).consume().into();
    if (lr_extracted != 42) std::abort();

    NonZero<int> nz{seed};
    if (nz.value() != 42) std::abort();

    NonEmpty<std::span<int>> ne{sp};
    if (ne.value().size() != 3) std::abort();

    NonEmptySpan<int> nes{sp};
    if (nes.value().size() != 3) std::abort();
}

}  // namespace detail::refined_self_test

}  // namespace crucible::safety
