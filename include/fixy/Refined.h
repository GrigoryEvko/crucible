#pragma once

// There is deliberately no implicit conversion to T. A refined value
// must not pass silently into a function that takes the bare type.
//
// Where contracts are compiled out, the mint's precondition becomes an
// unconditional assumption handed to the optimizer rather than a
// check. A Refined therefore carries its invariant as a promise to the
// optimizer, not as a guard. That is correct and free for a value the
// surrounding code structurally guarantees. It is unsound for a value
// taken straight from an untrusted source: a malformed value satisfies
// the assumption vacuously and propagates downstream as a false
// invariant, feeding, say, an unreachable default arm.
//
// So at a trust boundary the value gets a real branch of its own,
// outside the contract system, before the Refined is minted. The mint
// then stands as typed defence in depth: its precondition holds by
// construction and never fires, while the type keeps carrying the
// invariant for the optimizer and for every later reader.
//
// The wrapper has one door. Every constructor that takes a bare T is
// private, and the two friend mints are the only callers:
// mint_refined runs the predicate, mint_refined_trusted does not. A
// search for the trusted spelling therefore finds every site that
// asks the type system to take an invariant on faith.
//
// Old spelling: include/crucible/safety/Refined.h,
// include/crucible/safety/RefinedAlgebra.h and
// include/crucible/safety/SealedRefined.h, which this header joins.
// The implication relation those headers stated as an open trait is a
// closed namespace here, so the three had to become one.

#include <fixy/Qtt.h>
#include <foundation/Platform.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/GradedTrait.h>
#include <foundation/algebra/lattices/BoolLattice.h>
#include <foundation/contracts/Pre.h>
#include <foundation/diag/FailClosed.h>
#include <foundation/reflect/Instance.h>

#include <array>
#include <bit>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <meta>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace fixy {

// Every predicate is stateless, so that it can serve as a template
// argument.

inline constexpr auto positive = [](auto x) constexpr noexcept { return x > decltype(x){0}; };

inline constexpr auto non_negative = [](auto x) constexpr noexcept { return x >= decltype(x){0}; };

// For an unsigned type this coincides with positive. The two are kept
// apart because they state different intents: non_zero reserves a
// sentinel, positive claims a sign class.
//
// The zero is value-initialised rather than written as a literal, so
// that the zero value of a pointer type is spelled as the null pointer
// it is.  For an arithmetic type the two spellings are the same value.
inline constexpr auto non_zero = [](const auto& x) constexpr noexcept {
    if constexpr (requires { x.raw(); })
        return x.raw() != 0;
    else
        return x != decltype(x){};
};

// The dual of non_zero. It holds where a wire or disk format reserves
// zero as the only valid payload for a field, so that the must-be-zero
// invariant lives in the type instead of being discovered by reading a
// write routine and noticing the zero literal.
inline constexpr auto is_zero = [](const auto& x) constexpr noexcept {
    if constexpr (requires { x.raw(); })
        return x.raw() == 0;
    else
        return x == decltype(x){};
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
// pattern-match that back to the parameter, so the implication rules
// at the foot of this file could not deduce against it. Callers are
// unaffected, since the paired variable template still reads as a
// call.
//
// Each predicate therefore ships in two pieces: the struct template is
// the type that the implication rules deduce against, and the variable
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

template <std::size_t N>
struct ExactSize {
    constexpr bool operator()(auto const& c) const noexcept { return c.size() == N; }
};

template <std::size_t N>
inline constexpr ExactSize<N> exact_size{};

template <auto Min>
struct BoundedBelow {
    constexpr bool operator()(auto x) const noexcept { return x >= decltype(x)(Min); }
};

template <auto Min>
inline constexpr BoundedBelow<Min> bounded_below{};

// This is divisibility of a count, distinct from the byte-alignment of
// an address that `aligned` tests.
template <auto Divisor>
struct DivisibleBy {
    static_assert(Divisor != decltype(Divisor){0}, "DivisibleBy<0> is undefined (modulo by zero).  Pick a non-"
                                                   "zero divisor or omit the predicate.");
    constexpr bool operator()(auto x) const noexcept { return (x % decltype(x)(Divisor)) == decltype(x){0}; }
};

template <auto Divisor>
inline constexpr DivisibleBy<Divisor> divisible_by{};

// A conjunction of two predicates can already be spelled two other
// ways: as two nested refinement wrappers, or as a hand-rolled struct
// combining them. Both lose. Nesting produces a type the implication
// relation cannot see through, so every subsumption chain stops at
// the outer wrapper, and both forms grow at each call site. A
// combinator is one predicate type instead, which composes with any
// other and still collapses inside the wrapper.
//
// Each combinator is itself a predicate, so they nest freely.

namespace refined_algebra {

template <auto... Preds>
struct AllOf {
    constexpr bool operator()(auto const& v) const noexcept {
        if constexpr (sizeof...(Preds) == 0)
            return true;
        else
            return (... && Preds(v));
    }
};

template <auto... Preds>
inline constexpr AllOf<Preds...> all_of{};

template <auto... Preds>
struct AnyOf {
    constexpr bool operator()(auto const& v) const noexcept {
        if constexpr (sizeof...(Preds) == 0)
            return false;
        else
            return (... || Preds(v));
    }
};

template <auto... Preds>
inline constexpr AnyOf<Preds...> any_of{};

template <auto Pred>
struct Negate {
    constexpr bool operator()(auto const& v) const noexcept { return !Pred(v); }
};

template <auto Pred>
inline constexpr Negate<Pred> negate{};

template <auto Pre, auto Post>
struct Implies {
    constexpr bool operator()(auto const& v) const noexcept { return !Pre(v) || Post(v); }
};

template <auto Pre, auto Post>
inline constexpr Implies<Pre, Post> implies{};

}  // namespace refined_algebra

// The atomic predicates already live one namespace out, so the
// combinators are re-exported beside them and the whole vocabulary
// reads the same at a call site.
using refined_algebra::AllOf;
using refined_algebra::AnyOf;
using refined_algebra::Negate;
using refined_algebra::Implies;
using refined_algebra::all_of;
using refined_algebra::any_of;
using refined_algebra::negate;
using refined_algebra::implies;

namespace refined {

// The type of a predicate value, stripped of const.  An inline
// constexpr predicate variable is const at file scope while the
// template argument that binds it is not, so every place that reasons
// about a predicate by type goes through this one alias.
template <auto Pred>
using predicate_t = std::remove_cv_t<decltype(Pred)>;

}  // namespace refined

// This gates the mints, never the class template itself.  The subsort
// machinery reasons over a Refined type without ever constructing one,
// so a requires-clause on the class template would make those types
// unnameable and break that discipline.  The checked mint is also the
// only place the predicate is actually evaluated, and gating it turns
// what would be a SFINAE cascade inside the contract clause into one
// concept-violation message at the call site.
template <auto Pred, typename T>
concept PredicateInvocableOn = requires(T const& v) {
    { Pred(v) } -> std::convertible_to<bool>;
};

template <auto Pred, typename T>
class Refined;

template <auto Pred, typename T>
class SealedRefined;

// Every factory that mints an authoritative value is named mint_, so
// that one search finds every authorization point.  The constructors
// are private, so these four are the only doors, and the trusted pair
// is the grep target for every site that skips the predicate.

template <auto Pred, typename T>
    requires PredicateInvocableOn<Pred, T>
[[nodiscard]] constexpr Refined<Pred, T> mint_refined(T value) noexcept(std::is_nothrow_move_constructible_v<T>);

// No invocability requirement here: this path bypasses the predicate
// entirely, so even a predicate that cannot be called on T is
// admissible and the caller owns the invariant.
template <auto Pred, typename T>
    requires std::move_constructible<T>
[[nodiscard]] constexpr Refined<Pred, T>
mint_refined_trusted(T value) noexcept(std::is_nothrow_move_constructible_v<T>);

template <auto Pred, typename T>
    requires PredicateInvocableOn<Pred, T>
[[nodiscard]] constexpr SealedRefined<Pred, T>
mint_sealed_refined(T value) noexcept(std::is_nothrow_move_constructible_v<T>);

template <auto Pred, typename T>
    requires std::move_constructible<T>
[[nodiscard]] constexpr SealedRefined<Pred, T>
mint_sealed_refined_trusted(T value) noexcept(std::is_nothrow_move_constructible_v<T>);

template <auto Pred, typename T>
class [[nodiscard]] Refined {
public:
    using value_type = T;
    using predicate_type = decltype(Pred);
    // The lattice takes the predicate's type, and the const strip
    // matters: an inline constexpr predicate variable is const at file
    // scope while the template argument that binds it is not.
    using lattice_type = ::foundation::algebra::lattices::BoolLattice<refined::predicate_t<Pred>>;

    static constexpr ::foundation::algebra::ModalityKind modality = ::foundation::algebra::ModalityKind::Absolute;

    using graded_type = ::foundation::algebra::Graded<::foundation::algebra::ModalityKind::Absolute, lattice_type, T>;

private:
    graded_type impl_;

    // The two doors.  Each is reachable only through the friend mint
    // that names it.
    struct checked_door_ {};
    struct trusted_door_ {};

    // The predicate runs on the value before it moves into the
    // substrate, so a predicate written over a reference never sees a
    // moved-from object.  The macro fires at consteval as well as at
    // runtime, and leaves the invariant behind as an assumption.
    [[nodiscard]] static constexpr T admit_(T v) noexcept(std::is_nothrow_move_constructible_v<T>)
        requires PredicateInvocableOn<Pred, T>
    {
        CRUCIBLE_PRE(Pred(v));
        return v;
    }

    constexpr Refined(checked_door_, T v) noexcept(std::is_nothrow_move_constructible_v<T>)
        requires PredicateInvocableOn<Pred, T>
        : impl_{admit_(std::move(v)), typename lattice_type::element_type{}} {}

    constexpr Refined(trusted_door_, T v) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(v), typename lattice_type::element_type{}} {}

    template <auto P, typename U>
        requires PredicateInvocableOn<P, U>
    friend constexpr Refined<P, U> mint_refined(U value) noexcept(std::is_nothrow_move_constructible_v<U>);

    template <auto P, typename U>
        requires std::move_constructible<U>
    friend constexpr Refined<P, U> mint_refined_trusted(U value) noexcept(std::is_nothrow_move_constructible_v<U>);

public:
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

template <auto Pred, typename T>
    requires PredicateInvocableOn<Pred, T>
[[nodiscard]] constexpr Refined<Pred, T> mint_refined(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
    return Refined<Pred, T>{typename Refined<Pred, T>::checked_door_{}, std::move(value)};
}

template <auto Pred, typename T>
    requires std::move_constructible<T>
[[nodiscard]] constexpr Refined<Pred, T>
mint_refined_trusted(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
    return Refined<Pred, T>{typename Refined<Pred, T>::trusted_door_{}, std::move(value)};
}

// A refinement with no way to extract the value back out.  Every
// change to a sealed value therefore goes through a fresh mint, which
// re-runs the predicate.  That closes the pattern of extracting a
// value, mutating it behind the predicate's back and quietly
// re-wrapping it.
//
// Reach for it when the predicate is an invariant downstream code
// relies on continuously rather than only at construction, and
// especially when the wrapped type has a mutation surface of its own.
//
// A const-qualified ordinary refinement is not the same discipline.
// Const on a parameter does not propagate to the caller's own value,
// and the extractor is rvalue-qualified, so any caller can still move
// from it and pull the value out.  Removing the extractor from the
// type is what makes the discipline unavoidable.

template <auto Pred, typename T>
class [[nodiscard]] SealedRefined {
public:
    using value_type = T;
    using predicate_type = decltype(Pred);
    using lattice_type = ::foundation::algebra::lattices::BoolLattice<refined::predicate_t<Pred>>;
    static constexpr ::foundation::algebra::ModalityKind modality = ::foundation::algebra::ModalityKind::Absolute;
    using graded_type = ::foundation::algebra::Graded<::foundation::algebra::ModalityKind::Absolute, lattice_type, T>;

private:
    graded_type impl_;

    struct checked_door_ {};
    struct trusted_door_ {};

    [[nodiscard]] static constexpr T admit_(T v) noexcept(std::is_nothrow_move_constructible_v<T>)
        requires PredicateInvocableOn<Pred, T>
    {
        CRUCIBLE_PRE(Pred(v));
        return v;
    }

    constexpr SealedRefined(checked_door_, T v) noexcept(std::is_nothrow_move_constructible_v<T>)
        requires PredicateInvocableOn<Pred, T>
        : impl_{admit_(std::move(v)), typename lattice_type::element_type{}} {}

    constexpr SealedRefined(trusted_door_, T v) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(v), typename lattice_type::element_type{}} {}

    template <auto P, typename U>
        requires PredicateInvocableOn<P, U>
    friend constexpr SealedRefined<P, U> mint_sealed_refined(U value) noexcept(std::is_nothrow_move_constructible_v<U>);

    template <auto P, typename U>
        requires std::move_constructible<U>
    friend constexpr SealedRefined<P, U>
    mint_sealed_refined_trusted(U value) noexcept(std::is_nothrow_move_constructible_v<U>);

public:
    // No check is needed here: the source's own invariant is the proof,
    // so this is a transfer between two doors and not a door of its
    // own.
    constexpr explicit SealedRefined(Refined<Pred, T>&& r) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(r).into(), typename lattice_type::element_type{}} {}

    // Moving is allowed.  The destination carries the same bytes, and
    // they still satisfy the predicate.  What is forbidden is
    // extraction, not movement.
    SealedRefined(const SealedRefined&) = default;
    SealedRefined(SealedRefined&&) = default;
    SealedRefined& operator=(const SealedRefined&) = default;
    SealedRefined& operator=(SealedRefined&&) = default;

    // The only way to observe the value.  There is deliberately no
    // extractor and no mutable accessor.
    [[nodiscard]] constexpr const T& value() const noexcept { return impl_.peek(); }

    friend constexpr bool operator==(const SealedRefined& a,
                                     const SealedRefined& b) noexcept(noexcept(a.impl_.peek() == b.impl_.peek())) {
        return a.impl_.peek() == b.impl_.peek();
    }

    friend constexpr auto operator<=>(const SealedRefined& a,
                                      const SealedRefined& b) noexcept(noexcept(a.impl_.peek() <=> b.impl_.peek()))
        requires std::three_way_comparable<T>
    {
        return a.impl_.peek() <=> b.impl_.peek();
    }

    // The lattice name is shared with the unsealed refinement, since
    // the substrate is the same.  What tells the two apart is the
    // wrapper's own identity.
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept { return graded_type::lattice_name(); }
};

template <auto Pred, typename T>
    requires PredicateInvocableOn<Pred, T>
[[nodiscard]] constexpr SealedRefined<Pred, T>
mint_sealed_refined(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
    return SealedRefined<Pred, T>{typename SealedRefined<Pred, T>::checked_door_{}, std::move(value)};
}

template <auto Pred, typename T>
    requires std::move_constructible<T>
[[nodiscard]] constexpr SealedRefined<Pred, T>
mint_sealed_refined_trusted(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
    return SealedRefined<Pred, T>{typename SealedRefined<Pred, T>::trusted_door_{}, std::move(value)};
}

// Every load-bearing predicate gets a named alias, so that it takes
// part in review rather than drifting into an anonymous refinement at
// each call site.  Naming each shape also keeps two spellings of the
// same refinement from drifting apart across call sites.

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
// participates in the implication relation below, so a non-empty span
// strengthens to a longer-minimum one without being re-validated.
// non_empty stays useful for a container whose size is not cheap to
// compute, which a span's is.
template <typename T>
using NonEmptySpan = Refined<length_ge<std::size_t{1}>, std::span<T>>;

// N of zero is permitted here as a degenerate any-length alias,
// because the underlying predicate is vacuous at zero. The implication
// relation below correctly refuses to bridge that case to non_empty.
template <std::size_t N, typename T>
using MinLength = Refined<length_ge<N>, T>;
template <auto Max, typename T>
using MaxBounded = Refined<bounded_above<Max>, T>;

// Two traps neither alias rejects. An N that is not a power of two
// still compiles and turns the alignment test into an arbitrary
// low-bit mask. An inverted bound with Lo above Hi also still
// compiles, and the predicate becomes vacuously false, so the failure
// surfaces at the mint rather than at compile time.  Bounded below is
// the alias that closes the second trap.
template <std::size_t N, typename T>
using AlignedTo = Refined<aligned<N>, T>;
template <auto Lo, auto Hi, typename T>
using WithinRange = Refined<in_range<Lo, Hi>, T>;

template <std::size_t N, class S>
using Sized = Refined<exact_size<N>, S>;

// An inverted range is empty and can never be satisfied, so it is
// always a mistake. The trampoline below exists to catch it once at
// alias instantiation instead of at every value's construction.
namespace detail {
template <auto Lo, auto Hi, class T>
struct bounded_alias {
    static_assert(Lo <= Hi, "Bounded<Lo, Hi, T>: range is empty (Lo > Hi).  No value "
                            "of T can satisfy this refinement.  Swap the arguments "
                            "or use Capped<Hi, T> / Floored<Lo, T> for single-sided "
                            "bounds.");
    using type = Refined<in_range<Lo, Hi>, T>;
};
}  // namespace detail

template <auto Lo, auto Hi, class T>
using Bounded = typename detail::bounded_alias<Lo, Hi, T>::type;

template <auto Max, class T>
using Capped = Refined<bounded_above<Max>, T>;

template <auto Min, class T>
using Floored = Refined<bounded_below<Min>, T>;

template <std::size_t N, class S>
using MinSize = Refined<length_ge<N>, S>;

template <auto N, class T>
using DivisibleByN = Refined<divisible_by<N>, T>;

template <class T>
using CacheLineAligned = AlignedTo<64, T*>;

template <class T>
using HugePageAligned = AlignedTo<2 * 1024 * 1024, T*>;

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

// The traits that other layers use to take a refinement apart without
// naming the wrapper.  Both wrappers answer through the one reflection
// query in foundation/reflect/Instance.h, and the cv-ref strip is that
// query's.

template <typename T>
inline constexpr bool is_refined_v = ::foundation::reflect::is_instance_of_v<T, ^^Refined>
                                  || ::foundation::reflect::is_instance_of_v<T, ^^SealedRefined>;

template <typename T>
concept IsRefined = is_refined_v<T>;

template <typename T>
    requires is_refined_v<T>
using refined_value_t = typename std::remove_cvref_t<T>::value_type;

template <typename T>
    requires is_refined_v<T>
using refined_predicate_type_t = typename std::remove_cvref_t<T>::predicate_type;

template <typename T>
    requires is_refined_v<T>
inline constexpr bool refined_is_sealed_v = ::foundation::reflect::is_instance_of_v<T, ^^SealedRefined>;

// implies_v<P, Q> reads: every value satisfying P also satisfies Q.
// P is therefore at least as strong as Q, and P's truth set is
// contained in Q's. Reversing the reading inverts every axiom below.
//
// The relation is closed.  Its members are the variables declared in
// the namespace admitted_implications and nothing else: an edge for
// each pair of atomic predicates, and a rule for each parameterised
// family.  There is no primary template to specialise, so a pair the
// namespace does not admit stays refused wherever the check runs.
// Declare every member before the first check against the relation,
// which the self-test at the foot of this file performs.
//
// The relation does not compose transitively. The subsorting rule
// that consumes it demands a direct implication, so a conclusion
// reachable only by chaining two axioms is not reachable through it at
// all. Every hop production code relies on therefore needs a member of
// its own, which is why several bridges below exist alongside the
// pairs they could be derived from.
//
// Reflexivity is deliberately absent. The subsort machinery already
// supplies it from a same-type fall-through, and stating it twice
// invites the two to drift apart.

namespace refined {

// A parameterised family of implications.  The declaration
// `inline constexpr rule<Family> name{};` inside admitted_implications
// is the whole opt-in, and Family::admits<P, Q>() decides each pair.
// The families deduce against the predicate struct templates through
// overload resolution on a null pointer of each type, which is what
// keeps a family closed: an overload set cannot be extended from
// outside its class.
template <class Family>
struct rule {
    using rule_type = Family;
};

template <class X>
concept IsRuleMarker = requires { typename X::rule_type; } && std::same_as<X, rule<typename X::rule_type>>;

template <class Family, class P, class Q>
concept RuleDecides = requires {
    { Family::template admits<P, Q>() } -> std::same_as<bool>;
};

// What every family shares.  A family states one holds_ overload whose
// parameters are pointers to the predicate shapes it relates, and
// leaves the rest here: a pair that deduces against that overload is
// decided by its body, and a pair that does not is refused.
template <class Family>
struct rule_family {
    template <class P, class Q>
    static consteval bool admits() noexcept {
        if constexpr (requires { Family::holds_(static_cast<P*>(nullptr), static_cast<Q*>(nullptr)); }) {
            return Family::holds_(static_cast<P*>(nullptr), static_cast<Q*>(nullptr));
        } else {
            return false;
        }
    }
};

// The relation over predicate types.  Declared here because the
// conjunction family below reaches a conclusion through what its
// conjuncts imply, and defined once the namespace is complete.
template <class PType, class QType>
[[nodiscard]] consteval bool implies_types() noexcept;

namespace admitted_implications {

// positive ⇒ non_negative, because x > 0 gives x ≥ 0.
// positive ⇒ non_zero, because x > 0 gives x ≠ 0.
// power_of_two ⇒ non_zero, because the definition excludes zero.

inline constexpr ::foundation::fail_closed::edge<predicate_t<positive>, predicate_t<non_negative>>
    positive_implies_non_negative{};

inline constexpr ::foundation::fail_closed::edge<predicate_t<positive>, predicate_t<non_zero>>
    positive_implies_non_zero{};

inline constexpr ::foundation::fail_closed::edge<predicate_t<power_of_two>, predicate_t<non_zero>>
    power_of_two_implies_non_zero{};

// non_null and non_zero imply each other. For a pointer, the zero
// value of the pointer type is the null pointer, so the two predicates
// evaluate identically. The pair is safe to state in both directions
// because non_null only accepts a pointer argument, so for any
// non-pointer type the opposite direction names a type that cannot be
// formed at all.

inline constexpr ::foundation::fail_closed::edge<predicate_t<non_null>, predicate_t<non_zero>>
    non_null_implies_non_zero{};

inline constexpr ::foundation::fail_closed::edge<predicate_t<non_zero>, predicate_t<non_null>>
    non_zero_implies_non_null{};

// Aligned<N> ⇒ Aligned<M> when N ≥ M and M divides N, so a
// cache-line-aligned pointer is also word-aligned.
struct aligned_weakens : rule_family<aligned_weakens> {
    template <std::size_t N, std::size_t M>
    static consteval bool holds_(Aligned<N>*, Aligned<M>*) noexcept {
        return N >= M && M > 0 && (N % M == 0);
    }
};
inline constexpr rule<aligned_weakens> aligned_weakens_rule{};

// A smaller ceiling implies a larger one.
struct bounded_above_weakens : rule_family<bounded_above_weakens> {
    template <auto N, auto M>
    static consteval bool holds_(BoundedAbove<N>*, BoundedAbove<M>*) noexcept {
        return N <= M;
    }
};
inline constexpr rule<bounded_above_weakens> bounded_above_weakens_rule{};

// A tighter range implies a looser one.
struct in_range_weakens : rule_family<in_range_weakens> {
    template <auto L1, auto H1, auto L2, auto H2>
    static consteval bool holds_(InRange<L1, H1>*, InRange<L2, H2>*) noexcept {
        return L2 <= L1 && H1 <= H2;
    }
};
inline constexpr rule<in_range_weakens> in_range_weakens_rule{};

// A range ceiling is an upper bound.
struct in_range_is_bounded_above : rule_family<in_range_is_bounded_above> {
    template <auto L, auto H>
    static consteval bool holds_(InRange<L, H>*, BoundedAbove<H>*) noexcept {
        return true;
    }
};
inline constexpr rule<in_range_is_bounded_above> in_range_is_bounded_above_rule{};

// A longer minimum implies a shorter one.
struct length_ge_weakens : rule_family<length_ge_weakens> {
    template <std::size_t N, std::size_t M>
    static consteval bool holds_(LengthGe<N>*, LengthGe<M>*) noexcept {
        return N >= M;
    }
};
inline constexpr rule<length_ge_weakens> length_ge_weakens_rule{};

// A container's emptiness test equals a size of zero, so a minimum
// length of one or more implies non-emptiness. The clause excluding
// zero is load-bearing: a size is unsigned, so a minimum of zero is
// vacuously true and an empty container satisfies it.
struct length_ge_is_non_empty : rule_family<length_ge_is_non_empty> {
    template <std::size_t N>
    static consteval bool holds_(LengthGe<N>*, predicate_t<non_empty>*) noexcept {
        return N >= 1;
    }
};
inline constexpr rule<length_ge_is_non_empty> length_ge_is_non_empty_rule{};

// The lower-bound conjunct gives x ≥ L, so a non-negative L implies a
// non-negative x. A negative L admits negative values and is excluded
// by the clause. L keeps its own type, so the test holds for a signed
// and an unsigned bound alike.
struct in_range_is_non_negative : rule_family<in_range_is_non_negative> {
    template <auto L, auto H>
    static consteval bool holds_(InRange<L, H>*, predicate_t<non_negative>*) noexcept {
        return L >= 0;
    }
};
inline constexpr rule<in_range_is_non_negative> in_range_is_non_negative_rule{};

// A lower bound of one or more gives x ≥ 1 and so x > 0. A bound of
// zero admits zero, which is not positive, hence the clause.  This is
// one of the bridges that exists because the relation does not chain.
struct in_range_is_positive : rule_family<in_range_is_positive> {
    template <auto L, auto H>
    static consteval bool holds_(InRange<L, H>*, predicate_t<positive>*) noexcept {
        return L >= 1;
    }
};
inline constexpr rule<in_range_is_positive> in_range_is_positive_rule{};

// non_zero is a union of two half-lines rather than one, so the gate
// here is a disjunction: either bound alone suffices to keep zero out
// of the range. A lower bound of one or more excludes it from above,
// and an upper bound of minus one or less excludes it from below. The
// second branch is what admits a wholly negative range, which the
// positive branch alone would miss.
//
// A bound strictly between zero and one is conservatively excluded,
// which under-asserts rather than risking a truncation.
struct in_range_is_non_zero : rule_family<in_range_is_non_zero> {
    template <auto L, auto H>
    static consteval bool holds_(InRange<L, H>*, predicate_t<non_zero>*) noexcept {
        return L >= 1 || H <= -1;
    }
};
inline constexpr rule<in_range_is_non_zero> in_range_is_non_zero_rule{};

// A conjunction implies each of its conjuncts, and each disjunct
// implies the disjunction.  Wiring both through the relation lets a
// composed refinement subsume exactly as its parts do.
//
// The conjunction also reaches a conclusion through what its atomic
// parts already imply, rather than only through a literal match.  That
// is what lets a composed predicate weaken to a predicate none of its
// conjuncts spells.
struct all_of_implies_conjunct : rule_family<all_of_implies_conjunct> {
    template <auto... Preds, class Q>
    static consteval bool holds_(AllOf<Preds...>*, Q*) noexcept {
        return ((std::is_same_v<predicate_t<Preds>, Q> || implies_types<predicate_t<Preds>, Q>()) || ...);
    }
};
inline constexpr rule<all_of_implies_conjunct> all_of_implies_conjunct_rule{};

struct disjunct_implies_any_of : rule_family<disjunct_implies_any_of> {
    template <class P, auto... Preds>
    static consteval bool holds_(P*, AnyOf<Preds...>*) noexcept {
        return (std::is_same_v<predicate_t<Preds>, P> || ...);
    }
};
inline constexpr rule<disjunct_implies_any_of> disjunct_implies_any_of_rule{};

// The lower-bound axioms mirror the upper-bound ones but with the
// inequality flipped: a tighter floor is a larger N, whereas a tighter
// ceiling is a smaller one. A range's floor is itself a lower bound.
// The bridges out to the unparameterised predicates are gated: a
// negative floor still admits negative values, and a floor of zero
// still admits zero, so neither reaches non_negative or positive
// respectively. A floor strictly between zero and one is
// conservatively excluded too.
struct bounded_below_weakens : rule_family<bounded_below_weakens> {
    template <auto N, auto M>
    static consteval bool holds_(BoundedBelow<N>*, BoundedBelow<M>*) noexcept {
        return N >= M;
    }
};
inline constexpr rule<bounded_below_weakens> bounded_below_weakens_rule{};

struct in_range_is_bounded_below : rule_family<in_range_is_bounded_below> {
    template <auto L, auto H>
    static consteval bool holds_(InRange<L, H>*, BoundedBelow<L>*) noexcept {
        return true;
    }
};
inline constexpr rule<in_range_is_bounded_below> in_range_is_bounded_below_rule{};

struct bounded_below_is_non_negative : rule_family<bounded_below_is_non_negative> {
    template <auto N>
    static consteval bool holds_(BoundedBelow<N>*, predicate_t<non_negative>*) noexcept {
        return N >= 0;
    }
};
inline constexpr rule<bounded_below_is_non_negative> bounded_below_is_non_negative_rule{};

struct bounded_below_is_positive : rule_family<bounded_below_is_positive> {
    template <auto N>
    static consteval bool holds_(BoundedBelow<N>*, predicate_t<positive>*) noexcept {
        return N >= 1;
    }
};
inline constexpr rule<bounded_below_is_positive> bounded_below_is_positive_rule{};

// The relation does not chain, so this direct bridge is needed even
// though the same conclusion follows by chaining the floor bridge to
// positive with positive implying non-zero. A floor of one or more is
// sound for both categories the predicate accepts: for a number the
// value is at least one, and for a pointer the address is, so neither
// can be the zero value of its type.
struct bounded_below_is_non_zero : rule_family<bounded_below_is_non_zero> {
    template <auto N>
    static consteval bool holds_(BoundedBelow<N>*, predicate_t<non_zero>*) noexcept {
        return N >= 1;
    }
};
inline constexpr rule<bounded_below_is_non_zero> bounded_below_is_non_zero_rule{};

// The same shape on the size axis. An exact size of N satisfies any
// minimum up to N, and satisfies non-emptiness once N is at least one.
// A size of zero is excluded from the second, since it means the
// container is empty, the very opposite of the conclusion.
//
// The non-emptiness bridge is direct rather than derived, because the
// relation does not chain and the route through the minimum-size
// axioms would not be reachable.
struct exact_size_is_length_ge : rule_family<exact_size_is_length_ge> {
    template <std::size_t N, std::size_t M>
    static consteval bool holds_(ExactSize<N>*, LengthGe<M>*) noexcept {
        return N >= M;
    }
};
inline constexpr rule<exact_size_is_length_ge> exact_size_is_length_ge_rule{};

struct exact_size_is_non_empty : rule_family<exact_size_is_non_empty> {
    template <std::size_t N>
    static consteval bool holds_(ExactSize<N>*, predicate_t<non_empty>*) noexcept {
        return N >= 1;
    }
};
inline constexpr rule<exact_size_is_non_empty> exact_size_is_non_empty_rule{};

// The same relation as pointer alignment, on the modulo axis. If x is
// a multiple of N and N is a multiple of M, then x is a multiple of M.
// The clause excluding a zero M matches the predicate's own assertion,
// keeping modulo by zero out before it can be evaluated. The clause
// requiring N at least M already follows from N being a multiple of a
// positive M, and is stated anyway so the clause reads in one pass.
struct divisible_by_weakens : rule_family<divisible_by_weakens> {
    template <auto N, auto M>
    static consteval bool holds_(DivisibleBy<N>*, DivisibleBy<M>*) noexcept {
        return N >= M && M > decltype(M){0} && (N % M == decltype(N){0});
    }
};
inline constexpr rule<divisible_by_weakens> divisible_by_weakens_rule{};

}  // namespace admitted_implications

// True when some rule variable of Ns decides the pair.  Every other
// member of Ns is skipped, the same way the edge check skips them.
template <std::meta::info Ns, class P, class Q>
[[nodiscard]] consteval bool rule_admits() noexcept {
    static_assert(std::meta::is_namespace(Ns), "refined::rule_admits<Ns, P, Q>: Ns must be the reflection of "
                                               "a namespace, written ^^name.");
    static constexpr auto members =
        std::define_static_array(std::meta::members_of(Ns, std::meta::access_context::unchecked()));
    // -Wshadow fires on the expansion-statement induction variable.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto m : members) {
        if constexpr (std::meta::is_variable(m)) {
            using Marker = [:std::meta::remove_cvref(std::meta::type_of(m)):];
            if constexpr (IsRuleMarker<Marker>) {
                using Family = typename Marker::rule_type;
                static_assert(RuleDecides<Family, P, Q>, "a rule<Family> in admitted_implications must expose "
                                                         "Family::admits<P, Q>() returning bool.");
                if (Family::template admits<P, Q>()) {
                    return true;
                }
            }
        }
    }
#pragma GCC diagnostic pop
    return false;
}

template <class PType, class QType>
[[nodiscard]] consteval bool implies_types() noexcept {
    return ::foundation::fail_closed::Admitted<^^admitted_implications, PType, QType>
        || rule_admits<^^admitted_implications, PType, QType>();
}

}  // namespace refined

template <auto P, auto Q>
inline constexpr bool implies_v = refined::implies_types<refined::predicate_t<P>, refined::predicate_t<Q>>();

// Each axiom above is witnessed here, positively and at the boundary
// where its soundness clause bites. Because the relation does not
// chain, each hop of a chain is witnessed on its own line rather than
// inferred from its neighbours.  A refusal cannot be enumerated from
// the namespace, which is why these stay written by hand.

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

static_assert(implies_v<bounded_below<10>, bounded_below<5>>,
              "bounded_below<10> ⇒ bounded_below<5>: a tighter floor implies a looser one.");
static_assert(implies_v<bounded_below<5>, bounded_below<5>>, "bounded_below<N> ⇒ bounded_below<N>.");
static_assert(!implies_v<bounded_below<5>, bounded_below<10>>,
              "bounded_below<5> must NOT imply bounded_below<10>: a looser floor "
              "does not imply a tighter one.");
static_assert(implies_v<in_range<5, 100>, bounded_below<5>>,
              "in_range<5, 100> ⇒ bounded_below<5>: a range floor is a lower bound.");
static_assert(implies_v<in_range<0, 200>, bounded_below<0>>, "in_range<0, 200> ⇒ bounded_below<0> at a zero floor.");
static_assert(implies_v<bounded_below<0>, non_negative>,
              "bounded_below<0> ⇒ non_negative: the two predicates coincide at a zero floor.");
static_assert(implies_v<bounded_below<5>, non_negative>, "bounded_below<5> ⇒ non_negative: the floor is positive.");
static_assert(!implies_v<bounded_below<-5>, non_negative>,
              "bounded_below<-5> must NOT imply non_negative: it admits negative "
              "values, which is why the floor clause is load-bearing.");
static_assert(implies_v<bounded_below<1>, positive>, "bounded_below<1> ⇒ positive at the boundary floor.");
static_assert(implies_v<bounded_below<10>, positive>, "bounded_below<10> ⇒ positive at a tighter floor.");
static_assert(!implies_v<bounded_below<0>, positive>, "bounded_below<0> must NOT imply positive: it admits zero, "
                                                      "which is why the floor clause is load-bearing.");

static_assert(implies_v<bounded_below<1>, non_zero>, "bounded_below<1> ⇒ non_zero at the boundary floor.");
static_assert(implies_v<bounded_below<10>, non_zero>, "bounded_below<10> ⇒ non_zero at a tighter floor.");
static_assert(!implies_v<bounded_below<0>, non_zero>, "bounded_below<0> must NOT imply non_zero: it admits zero.");

static_assert(implies_v<exact_size<8>, length_ge<8>>, "exact_size<8> ⇒ length_ge<8>: an exact size satisfies its own "
                                                      "minimum.");
static_assert(implies_v<exact_size<8>, length_ge<4>>, "exact_size<8> ⇒ length_ge<4>: the exact size exceeds a looser "
                                                      "minimum.");
static_assert(implies_v<exact_size<1>, length_ge<0>>, "exact_size<1> ⇒ length_ge<0>: a size is never below zero.");
static_assert(!implies_v<exact_size<4>, length_ge<8>>, "exact_size<4> must NOT imply length_ge<8>: a size of four is "
                                                       "not at least eight.");

static_assert(implies_v<exact_size<1>, non_empty>, "exact_size<1> ⇒ non_empty at the boundary size.");
static_assert(implies_v<exact_size<8>, non_empty>, "exact_size<8> ⇒ non_empty at a larger size.");
static_assert(!implies_v<exact_size<0>, non_empty>, "exact_size<0> must NOT imply non_empty: a size of zero means the "
                                                    "container is empty.");

static_assert(implies_v<divisible_by<4>, divisible_by<4>>, "divisible_by<N> ⇒ divisible_by<N>.");
static_assert(implies_v<divisible_by<8>, divisible_by<4>>,
              "divisible_by<8> ⇒ divisible_by<4>: a multiple of eight is a "
              "multiple of four.");
static_assert(implies_v<divisible_by<16>, divisible_by<4>>,
              "divisible_by<16> ⇒ divisible_by<4>: a wider lane count implies a "
              "narrower one.");
static_assert(implies_v<divisible_by<16>, divisible_by<8>>,
              "divisible_by<16> ⇒ divisible_by<8>: a wider lane count implies a "
              "narrower one.");

static_assert(!implies_v<divisible_by<6>, divisible_by<4>>,
              "divisible_by<6> must NOT imply divisible_by<4>: six is not a "
              "multiple of four, and six itself is a counterexample.");
static_assert(!implies_v<divisible_by<4>, divisible_by<8>>,
              "divisible_by<4> must NOT imply divisible_by<8>: a looser divisor "
              "does not imply a tighter one, and four itself is a "
              "counterexample.");

namespace detail::refined_self_test {

// The atomic edges are folded over by reflection rather than listed:
// every edge variable in the admitted namespace is read back, checked
// against the relation it belongs to, and then checked against the
// predicates it names on a roster of sample values.  An edge that
// admits a value its target rejects fails here, and so does a
// reflexive edge, which the relation deliberately never states.

inline constexpr std::array<int, 9> signed_samples{-3, -1, 0, 1, 2, 3, 8, 42, 1024};
inline constexpr std::array<unsigned, 7> unsigned_samples{0u, 1u, 2u, 3u, 8u, 1024u, 4294967295u};

// Every value that P admits, Q admits too.
template <class P, class Q, class Sample>
[[nodiscard]] consteval bool edge_sound_on(Sample const& samples) noexcept {
    for (auto const& v : samples) {
        if (P{}(v) && !Q{}(v)) return false;
    }
    return true;
}

// A pair that both accept an integer is sampled on the two arithmetic
// rosters.  Any other pair names a pointer predicate, and is sampled
// on a null and a non-null pointer instead: an arithmetic predicate
// given a pointer would order it against null, which is no constant
// expression, so the two rosters never mix.
template <class P, class Q>
[[nodiscard]] consteval bool edge_sound() noexcept {
    if constexpr (requires(int v) {
                      P{}(v);
                      Q{}(v);
                  }) {
        return edge_sound_on<P, Q>(signed_samples) && edge_sound_on<P, Q>(unsigned_samples);
    } else {
        int const some_object = 7;
        std::array<int const*, 2> const pointer_samples{nullptr, &some_object};
        return edge_sound_on<P, Q>(pointer_samples);
    }
}

// Walks admitted_implications once.  is_edge and ends_of are the
// relation's own readers, so an edge is whatever the relation calls
// one.  A member that is neither an edge nor a rule is refused, since
// a stray variable there would be a member the relation ignores.
[[nodiscard]] consteval bool every_edge_holds() noexcept {
    static constexpr auto members = std::define_static_array(
        std::meta::members_of(^^refined::admitted_implications, std::meta::access_context::unchecked()));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto m : members) {
        if constexpr (::foundation::fail_closed::is_edge(m)) {
            constexpr auto ends = ::foundation::fail_closed::ends_of(m);
            using From = [:ends.from:];
            using To = [:ends.to:];
            static_assert(refined::implies_types<From, To>(), "an edge declared in admitted_implications must be "
                                                              "admitted by the relation");
            static_assert(!std::is_same_v<From, To>, "a reflexive edge is never stated; the subsort machinery "
                                                     "supplies reflexivity");
            static_assert(std::is_empty_v<From> && std::is_empty_v<To>, "an edge names two stateless predicates");
            static_assert(edge_sound<From, To>(), "an admitted edge is unsound on the sample roster: a value "
                                                  "satisfies the source predicate and fails the target");
        } else if constexpr (std::meta::is_variable(m)) {
            using Marker = [:std::meta::remove_cvref(std::meta::type_of(m)):];
            static_assert(refined::IsRuleMarker<Marker>, "admitted_implications holds edges and rules only");
        }
    }
#pragma GCC diagnostic pop
    return true;
}

static_assert(every_edge_holds());

// The count is derived from the namespace, so a new edge is a
// two-place edit that a reviewer sees.
static_assert(::foundation::fail_closed::edge_count<^^refined::admitted_implications>() == 5,
              "the five atomic edges: positive ⇒ non_negative, positive ⇒ non_zero, "
              "power_of_two ⇒ non_zero, and non_null ⇔ non_zero");

// The layout witnesses are one fold over a roster of wrapper types
// rather than one line per type.  The roster is the only hand list.
template <class... Ws>
[[nodiscard]] consteval bool all_collapse_to_value() noexcept {
    return ((sizeof(Ws) == sizeof(typename Ws::value_type)) && ...);
}

template <class... Ws>
[[nodiscard]] consteval bool all_graded_wrappers() noexcept {
    return (::foundation::algebra::GradedWrapper<Ws> && ...);
}

// One representative shape per alias: a scalar, a container that has
// both an emptiness test and a size, a parameterised length bound over
// that container, and the parameterised predicates, which are empty
// classes and so collapse inside the wrapper too.
static_assert(all_collapse_to_value<
              Refined<positive, int>, Refined<non_null, void*>, Refined<power_of_two, std::size_t>,
              Refined<aligned<64>, void*>, Refined<bounded_above<1024u>, int>, Refined<in_range<0, 100>, int>,
              Refined<length_ge<1>, void*>, NonZero<int>, NonEmpty<std::span<int>>, NonEmptySpan<int>,
              SealedRefined<positive, int>, SealedRefined<non_null, void*>,
              Refined<all_of<positive, bounded_above<1024>>, int>, Refined<any_of<positive, non_zero>, int>,
              Refined<negate<positive>, int>, Refined<implies<positive, non_zero>, int>, AlignedTo<64, void*>,
              Sized<8, std::array<int, 8>>, Bounded<0, 100, int>, Capped<255, std::uint32_t>, Floored<1, int>,
              MinSize<8, std::array<int, 16>>, DivisibleByN<4, std::size_t>, CacheLineAligned<int>,
              HugePageAligned<std::byte>>());

static_assert(sizeof(LinearRefined<non_null, void*>) == sizeof(void*), "LinearRefined must collapse to sizeof(T)");

// The atomic and the composed refinements alike satisfy the wrapper
// concept, so that a future revision of a combinator that disturbs the
// substrate's lattice, value or modality contract fires here.
static_assert(
    all_graded_wrappers<
        Refined<positive, int>, Refined<non_null, void*>, SealedRefined<positive, int>, Refined<all_of<positive>, int>,
        Refined<all_of<positive, bounded_above<100>>, int>, Refined<any_of<positive, non_zero>, int>,
        Refined<negate<positive>, int>, Refined<implies<positive, non_zero>, int>,
        Refined<all_of<all_of<positive>, bounded_above<1024>>, int>, AlignedTo<64, void*>, Bounded<0, 100, int>,
        Capped<255, std::uint32_t>, Floored<1, int>, DivisibleByN<4, std::size_t>>(),
    "every Refined and SealedRefined shape must satisfy GradedWrapper, nested combinators included");

// The substrate's byte-level interchangeability claim, pinned over the
// three arithmetic and pointer shapes.
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Positive, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(Positive, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NonNull, void*);

template <typename T>
using SealedPositive = SealedRefined<positive, T>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SealedPositive, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(SealedPositive, double);

// One door: no public constructor takes a bare value.
static_assert(!std::is_constructible_v<Refined<positive, int>, int>);
static_assert(!std::is_default_constructible_v<Refined<positive, int>>);
static_assert(!std::is_constructible_v<SealedRefined<positive, int>, int>);
static_assert(!std::is_default_constructible_v<SealedRefined<positive, int>>);
static_assert(std::is_constructible_v<SealedRefined<positive, int>, Refined<positive, int>&&>);

// The mints run in a constant expression, and the checked one runs the
// predicate there.
static_assert(mint_refined<positive>(42).value() == 42);
static_assert(mint_refined_trusted<positive>(-1).value() == -1);
static_assert(mint_sealed_refined<positive>(42).value() == 42);
static_assert(mint_sealed_refined_trusted<positive>(-1).value() == -1);

static_assert(std::is_same_v<typename AlignedTo<64, void*>::predicate_type, std::remove_cv_t<decltype(aligned<64>)>>);
static_assert(
    std::is_same_v<typename Bounded<0, 100, int>::predicate_type, std::remove_cv_t<decltype(in_range<0, 100>)>>);
static_assert(std::is_same_v<typename Capped<255, std::uint32_t>::predicate_type,
                             std::remove_cv_t<decltype(bounded_above<255>)>>);

// The extraction traits see through cv and reference, reject a
// lookalike with the same member aliases, and tell the two wrappers
// apart.
struct LookalikeRefined {
    using value_type = int;
    using predicate_type = decltype(positive);
};

static_assert(is_refined_v<Refined<positive, int>>);
static_assert(is_refined_v<Refined<non_negative, int>>);
static_assert(is_refined_v<SealedRefined<positive, int>>);
static_assert(is_refined_v<Refined<positive, int>&>);
static_assert(is_refined_v<Refined<positive, int>&&>);
static_assert(is_refined_v<Refined<positive, int> const>);
static_assert(is_refined_v<Refined<positive, int> const&>);
static_assert(is_refined_v<Refined<positive, int> volatile>);
static_assert(!is_refined_v<int>);
static_assert(!is_refined_v<int*>);
static_assert(!is_refined_v<Refined<positive, int>*>);
static_assert(!is_refined_v<void>);
static_assert(!is_refined_v<LookalikeRefined>);
static_assert(IsRefined<Refined<positive, int>>);
static_assert(IsRefined<SealedRefined<positive, int> const&>);
static_assert(!IsRefined<int>);
static_assert(std::is_same_v<refined_value_t<Refined<positive, int>>, int>);
static_assert(std::is_same_v<refined_value_t<SealedRefined<positive, int> const&>, int>);
static_assert(std::is_same_v<refined_predicate_type_t<Refined<positive, int>>, refined::predicate_t<positive>>);
static_assert(!refined_is_sealed_v<Refined<positive, int>>);
static_assert(refined_is_sealed_v<SealedRefined<positive, int>>);

inline void runtime_smoke_test() {
    int seed = 42;

    Refined<positive, int> p = mint_refined<positive>(seed);
    if (p.value() != 42) std::abort();

    auto pm = mint_refined<positive, int>(seed);
    if (pm.value() != 42) std::abort();

    int sentinel = -1;  // fails the predicate, which the trusted mint admits
    Refined<positive, int> tp = mint_refined_trusted<positive>(sentinel);
    if (tp.value() != -1) std::abort();

    Refined<positive, int> p2 = mint_refined<positive>(seed);
    if (!(p == p2)) std::abort();
    Refined<positive, int> p3 = mint_refined<positive>(seed + 1);
    if ((p <=> p3) != std::strong_ordering::less) std::abort();

    int extracted = std::move(p).into();
    if (extracted != 42) std::abort();

    Refined<bounded_above<128u>, unsigned int> ba = mint_refined<bounded_above<128u>>(static_cast<unsigned int>(seed));
    if (ba.value() != 42u) std::abort();

    Refined<in_range<0, 100>, int> ir = mint_refined<in_range<0, 100>>(seed);
    if (ir.value() != 42) std::abort();

    int arr[3] = {1, 2, 3};
    std::span<int> sp{arr};
    Refined<length_ge<1>, std::span<int>> ls = mint_refined<length_ge<1>>(sp);
    if (ls.value().size() != 3) std::abort();

    LinearRefined<positive, int> lr = mint_linear<Refined<positive, int>>(mint_refined<positive>(seed));
    if (lr.peek().value() != 42) std::abort();
    int lr_extracted = std::move(lr).consume().into();
    if (lr_extracted != 42) std::abort();

    NonZero<int> nz = mint_refined<non_zero>(seed);
    if (nz.value() != 42) std::abort();

    NonEmpty<std::span<int>> ne = mint_refined<non_empty>(sp);
    if (ne.value().size() != 3) std::abort();

    NonEmptySpan<int> nes = mint_refined<length_ge<std::size_t{1}>>(sp);
    if (nes.value().size() != 3) std::abort();
}

}  // namespace detail::refined_self_test

namespace detail::sealed_refined_self_test {

inline void runtime_smoke_test() {
    int seed = 5;

    SealedRefined<positive, int> sp = mint_sealed_refined<positive>(seed);
    if (sp.value() != 5) std::abort();

    auto spm = mint_sealed_refined<positive, int>(seed);
    if (spm.value() != 5) std::abort();

    // The trusted path admits a value the predicate would reject.
    int sentinel = -3;
    SealedRefined<positive, int> tp = mint_sealed_refined_trusted<positive>(sentinel);
    if (tp.value() != -3) std::abort();

    Refined<positive, int> r = mint_refined<positive>(seed * 2);
    SealedRefined<positive, int> from_r{std::move(r)};
    if (from_r.value() != 10) std::abort();

    SealedRefined<positive, int> sp_eq = mint_sealed_refined<positive>(seed);
    if (!(sp == sp_eq)) std::abort();
    SealedRefined<positive, int> sp_lt = mint_sealed_refined<positive>(seed - 1);
    if ((sp_lt <=> sp) != std::strong_ordering::less) std::abort();

    SealedRefined<positive, int> sp_copy = sp;
    if (sp_copy.value() != 5) std::abort();
    SealedRefined<positive, int> sp_move = std::move(sp_copy);
    if (sp_move.value() != 5) std::abort();
}

}  // namespace detail::sealed_refined_self_test

namespace detail::refined_algebra_self_test {

constexpr auto pos_capped = all_of<positive, bounded_above<100>>;
static_assert(pos_capped(50));
static_assert(!pos_capped(0));
static_assert(!pos_capped(101));

constexpr auto trivially_true = all_of<>;
static_assert(trivially_true(42));
static_assert(trivially_true(-1));
static_assert(trivially_true(0));

constexpr auto just_positive = all_of<positive>;
static_assert(just_positive(1));
static_assert(!just_positive(0));

constexpr auto zero_or_huge =
    any_of<[](int x) constexpr noexcept { return x == 0; }, [](int x) constexpr noexcept { return x >= 1024; }>;
static_assert(zero_or_huge(0));
static_assert(zero_or_huge(2048));
static_assert(!zero_or_huge(50));

constexpr auto trivially_false = any_of<>;
static_assert(!trivially_false(42));

constexpr auto neg_pos = negate<positive>;
static_assert(neg_pos(0));
static_assert(neg_pos(-1));
static_assert(!neg_pos(1));

constexpr auto neg_neg_pos = negate<negate<positive>>;
static_assert(neg_neg_pos(1));
static_assert(!neg_neg_pos(0));

constexpr auto pos_implies_nonzero = implies<positive, non_zero>;
static_assert(pos_implies_nonzero(5));
static_assert(pos_implies_nonzero(0));
static_assert(pos_implies_nonzero(-1));

// At zero the antecedent holds and the consequent fails, which is the
// one input the implication rejects.
constexpr auto false_implies_anything = implies<negate<positive>, positive>;
static_assert(false_implies_anything(1));
static_assert(!false_implies_anything(0));

constexpr std::array<int, 8> a8{};
constexpr std::array<int, 7> a7{};
static_assert(exact_size<8>(a8));
static_assert(!exact_size<7>(a8));
static_assert(exact_size<7>(a7));

static_assert(bounded_below<10>(15));
static_assert(bounded_below<10>(10));
static_assert(!bounded_below<10>(9));

static_assert(divisible_by<4>(0));
static_assert(divisible_by<4>(16));
static_assert(!divisible_by<4>(13));
static_assert(divisible_by<8>(2097152));

// Minting through the trusted door keeps this a test of whether the
// composed-predicate type is constructible, without also depending on
// the predicate being evaluated at compile time.
using PositiveCapped = Refined<all_of<positive, bounded_above<100>>, int>;
using AlignedNonNullPtr = Refined<all_of<non_null, aligned<64>>, void*>;

[[maybe_unused]] constexpr auto pc1_witness = mint_refined_trusted<all_of<positive, bounded_above<100>>, int>(42);
[[maybe_unused]] constexpr auto pc2_witness = mint_refined_trusted<all_of<positive, bounded_above<100>>, int>(1);

static_assert(implies_v<all_of<positive, bounded_above<100>>, positive>);
static_assert(implies_v<all_of<positive, bounded_above<100>>, bounded_above<100>>);
static_assert(implies_v<all_of<non_null, aligned<64>>, non_null>);
static_assert(implies_v<all_of<non_null, aligned<64>>, aligned<64>>);

static_assert(implies_v<positive, any_of<positive, non_zero>>);
static_assert(implies_v<non_zero, any_of<positive, non_zero>>);

static_assert(!implies_v<all_of<positive, bounded_above<100>>, non_null>);

// The conclusions below hold through a conjunct's own implications,
// not through a literal match against a conjunct.
static_assert(implies_v<all_of<positive, bounded_above<100>>, non_negative>);
static_assert(implies_v<all_of<positive>, non_zero>);
static_assert(implies_v<all_of<power_of_two, bounded_above<1024u>>, non_zero>);
// A disjunction entails none of its branches.
static_assert(!implies_v<any_of<positive, non_zero>, positive>);

static_assert(divisible_by<1>(0));
static_assert(divisible_by<8>(64));

// The checked mint in a constant expression runs the predicate there,
// and the alias trampoline admits the boundary where Lo equals Hi.
[[maybe_unused]] constexpr Bounded<0, 100, int> b_normal_witness = mint_refined<in_range<0, 100>, int>(50);
[[maybe_unused]] constexpr Bounded<5, 5, int> b_equal_witness = mint_refined<in_range<5, 5>, int>(5);

// Driving the combinators with non-constant arguments and a move-only
// payload catches the consteval, substitution and inline-body bugs
// that a block of compile-time assertions alone would mask.

[[gnu::cold]] inline void runtime_smoke_test() noexcept {
    int volatile vol = 42;  // defeats constant folding
    int x = vol;
    constexpr auto p = all_of<positive, bounded_above<100>>;
    bool ok = p(x);
    static_cast<void>(ok);

    Refined<all_of<positive, bounded_above<100>>, int> r = mint_refined<all_of<positive, bounded_above<100>>>(x);
    static_cast<void>(r);

    // The wrapper must not demand a copyable payload, which a
    // combinator could reintroduce by accident. Minting through the
    // trusted door keeps this about type composition rather than
    // about the predicate being callable on a move-only value.
    struct MoveOnly {
        int v_ = 0;
        constexpr MoveOnly() noexcept = default;
        constexpr explicit MoveOnly(int v) noexcept : v_{v} {}
        MoveOnly(const MoveOnly&) = delete;
        MoveOnly(MoveOnly&&) noexcept = default;
        MoveOnly& operator=(const MoveOnly&) = delete;
        MoveOnly& operator=(MoveOnly&&) noexcept = default;
    };
    static_assert(!std::is_copy_constructible_v<MoveOnly>);
    static_assert(std::is_move_constructible_v<MoveOnly>);

    using RmoT = Refined<positive, MoveOnly>;
    MoveOnly mo{vol};
    RmoT rmo = mint_refined_trusted<positive>(std::move(mo));
    static_assert(sizeof(RmoT) == sizeof(MoveOnly), "Refined<P, MoveOnly> must EBO-collapse to sizeof(MoveOnly) "
                                                    "regardless of T's copyability");
    static_cast<void>(rmo);

    alignas(64) int buf[16] = {};
    AlignedTo<64, int*> ap = mint_refined<aligned<64>>(static_cast<int*>(buf));
    static_cast<void>(ap);

    std::array<int, 8> arr8_runtime{};
    Sized<8, std::array<int, 8>> sized = mint_refined<exact_size<8>>(arr8_runtime);
    static_cast<void>(sized);

    Bounded<0, 100, int> bd = mint_refined<in_range<0, 100>>(x);
    static_cast<void>(bd);

    Capped<255, std::uint32_t> cap = mint_refined<bounded_above<255>>(static_cast<std::uint32_t>(vol));
    static_cast<void>(cap);

    Floored<1, int> fl = mint_refined<bounded_below<1>>(x);
    static_cast<void>(fl);

    std::size_t volatile big = 1024;
    DivisibleByN<4, std::size_t> dN = mint_refined<divisible_by<4>>(big);
    static_cast<void>(dN);

    // The composed predicate accepts a pointer argument because both
    // of its conjuncts do.
    Refined<all_of<non_null, aligned<64>>, void*> aligned_nonnull_ptr =
        mint_refined<all_of<non_null, aligned<64>>>(static_cast<void*>(buf));
    static_cast<void>(aligned_nonnull_ptr);
}

}  // namespace detail::refined_algebra_self_test

}  // namespace fixy
