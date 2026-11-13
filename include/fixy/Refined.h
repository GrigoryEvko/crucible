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
// This header holds the refinement and the sealed refinement.
// fixy/Predicates.h holds the predicates, their combinators and the
// closed implication relation between them.

#include <fixy/GradedFacade.h>
#include <fixy/Predicates.h>
#include <fixy/Qtt.h>
#include <foundation/Lifetime.h>
#include <foundation/Platform.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/GradedTrait.h>
#include <foundation/algebra/lattices/BoolLattice.h>
#include <foundation/contracts/Pre.h>
#include <foundation/core/Choice.h>
#include <foundation/reflect/Instance.h>

#include <compare>
#include <concepts>
#include <cstddef>
#include <meta>
#include <span>
#include <type_traits>
#include <utility>

namespace fixy {

namespace refined {

// The claim a sealed refinement makes that its lattice does not see.
// Both refinements grade on BoolLattice over the predicate, so without
// this identity a sealed value and an open one over the same predicate
// take one cache slot, although only one of them can be mutated in
// place.  The open form publishes nothing, so no discipline enters its
// hash.
namespace row_discipline {
struct sealed_refinement;
}  // namespace row_discipline

namespace detail {

template <bool Sealed>
struct sealed_row_discipline {};

template <>
struct sealed_row_discipline<true> {
    using row_discipline = ::fixy::refined::row_discipline::sealed_refinement;
};

}  // namespace detail

}  // namespace refined

// One template carries both refinements.  They differ in exactly one
// place: the sealed one has no extractor.  The Sealed argument is that
// one difference.
//
// The two names are distinct types, because Refinement<Pred, T, false>
// and Refinement<Pred, T, true> are distinct types.  Nothing that holds
// a Refined can be handed a SealedRefined, and the hidden-friend
// comparisons refuse to compare across the two.
template <auto Pred, typename T, bool Sealed>
class Refinement;

template <auto Pred, typename T>
using Refined = Refinement<Pred, T, false>;

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
using SealedRefined = Refinement<Pred, T, true>;

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

// The annotation refuses the checked lifetime start over bytes, and the
// user-provided assignments below keep the class from being trivially
// copyable, so std::bit_cast<Refined<positive, int>>(-1) does not build a
// positive int that holds -1.  The constructors stay trivial, so a refined
// value still passes in a register.
template <auto Pred, typename T, bool Sealed>
class [[nodiscard]][[= ::foundation::lifetime::no_start_over_bytes{}]] Refinement
    : public graded_facade<::foundation::algebra::ModalityKind::Absolute,
                           ::foundation::algebra::lattices::BoolLattice<refined::predicate_t<Pred>>, T>,
      public refined::detail::sealed_row_discipline<Sealed> {
public:
    using predicate_type = decltype(Pred);

    // The lattice is keyed by the predicate's type, so the type must be the
    // whole predicate: a stateless class.  A function pointer or a class
    // with state gives two predicates one type, and so one row hash.
    static_assert(std::is_class_v<refined::predicate_t<Pred>> && std::is_empty_v<refined::predicate_t<Pred>>,
                  "fixy::Refined: the predicate must be a stateless class, such as fixy::positive or "
                  "fixy::in_range<0, 9>.  A function pointer or a class with state gives two predicates one "
                  "type, so two different refinements would share one row hash.");

    // value_type, modality and the two name forwarders arrive from
    // graded_facade.  The base is dependent, so the two names this
    // class body uses unqualified are re-declared here rather than
    // found by lookup.  The lattice takes the predicate's type, and the
    // const strip matters: an inline constexpr predicate variable is
    // const at file scope while the template argument that binds it is
    // not.
    using facade_ = graded_facade<::foundation::algebra::ModalityKind::Absolute,
                                  ::foundation::algebra::lattices::BoolLattice<refined::predicate_t<Pred>>, T>;
    using typename facade_::graded_type;
    using typename facade_::lattice_type;

    // The one difference between the two refinements, readable off the
    // type.  refined_is_sealed_v is a view of this.
    static constexpr bool is_sealed = Sealed;

private:
    graded_type impl_;

    using key_ = ::foundation::algebra::grade_key<Refinement>;

    // The two doors.  Each is reachable only through the friend mints
    // that name it.
    struct checked_door_ {};
    struct trusted_door_ {};

    // The predicate runs on the value before it moves into the
    // substrate, so a predicate written over a reference never sees a
    // moved-from object.  The macro fires at consteval as well as at
    // runtime.  Where contracts are compiled out, it leaves the
    // invariant behind as an assumption.
    [[nodiscard]] static constexpr T admit_(T v) noexcept(std::is_nothrow_move_constructible_v<T>)
        requires PredicateInvocableOn<Pred, T>
    {
        CRUCIBLE_PRE(Pred(v));
        return v;
    }

    constexpr Refinement(checked_door_, T v) noexcept(std::is_nothrow_move_constructible_v<T>)
        requires PredicateInvocableOn<Pred, T>
        : impl_{key_{}, admit_(std::move(v)), typename lattice_type::element_type{}} {}

    constexpr Refinement(trusted_door_, T v) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{key_{}, std::move(v), typename lattice_type::element_type{}} {}

    template <auto P, typename U>
        requires PredicateInvocableOn<P, U>
    friend constexpr Refined<P, U> mint_refined(U value) noexcept(std::is_nothrow_move_constructible_v<U>);

    template <auto P, typename U>
        requires std::move_constructible<U>
    friend constexpr Refined<P, U> mint_refined_trusted(U value) noexcept(std::is_nothrow_move_constructible_v<U>);

    template <auto P, typename U>
        requires PredicateInvocableOn<P, U>
    friend constexpr SealedRefined<P, U> mint_sealed_refined(U value) noexcept(std::is_nothrow_move_constructible_v<U>);

    template <auto P, typename U>
        requires std::move_constructible<U>
    friend constexpr SealedRefined<P, U>
    mint_sealed_refined_trusted(U value) noexcept(std::is_nothrow_move_constructible_v<U>);

public:
    // Sealing an ordinary refinement needs no check: the source's own
    // invariant is the proof, so this is a transfer between two doors
    // and not a door of its own.
    constexpr explicit Refinement(Refinement<Pred, T, false>&& r) noexcept(std::is_nothrow_move_constructible_v<T>)
        requires Sealed
        : impl_{key_{}, std::move(r).into(), typename lattice_type::element_type{}} {}

    // The refinement is a property of the value, so copying or moving
    // preserves it and neither needs to re-check.  What a sealed
    // refinement forbids is extraction, not movement.
    //
    // These four are load-bearing and cannot be dropped as implicit.
    // The sealing constructor above takes Refinement<Pred, T, false>&&,
    // which for the unsealed instantiation is Refinement&& — a
    // user-declared move constructor, even though a requires-clause
    // makes it unusable there.  Declaring one deletes the implicit copy
    // constructor, and removing this block leaves Refined<Pred, T>
    // uncopyable.  Measured: without it, mint_linear over a Refined
    // fails on a deleted copy constructor.
    //
    // The two assignments are user-provided, so the class is not
    // trivially copyable and no byte route builds a refined value.  The
    // constructors stay trivial.
    Refinement(const Refinement&) = default;
    Refinement(Refinement&&) = default;
    constexpr Refinement& operator=(const Refinement& other) noexcept(std::is_nothrow_copy_assignable_v<T>) {
        impl_ = other.impl_;
        return *this;
    }
    constexpr Refinement& operator=(Refinement&& other) noexcept(std::is_nothrow_move_assignable_v<T>) {
        impl_ = std::move(other.impl_);
        return *this;
    }

    // For a sealed refinement this is the only way to observe the
    // value.  There is deliberately no mutable accessor on either.
    [[nodiscard]] constexpr const T& value() const noexcept { return impl_.peek(); }

    [[nodiscard]] constexpr T into() && noexcept(std::is_nothrow_move_constructible_v<T>)
        requires(!Sealed)
    {
        return std::move(impl_).consume();
    }

    friend constexpr bool operator==(const Refinement& a,
                                     const Refinement& b) noexcept(noexcept(a.impl_.peek() == b.impl_.peek())) {
        return a.impl_.peek() == b.impl_.peek();
    }

    friend constexpr auto operator<=>(const Refinement& a,
                                      const Refinement& b) noexcept(noexcept(a.impl_.peek() <=> b.impl_.peek()))
        requires std::three_way_comparable<T>
    {
        return a.impl_.peek() <=> b.impl_.peek();
    }
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

// The trust-boundary door.  The predicate runs as a real branch, outside
// the contract system, and a value it refuses comes back as the caller's
// error.  Only a value the branch admitted reaches mint_refined, so the
// mint's precondition holds by construction and never fires.  It is the
// door for a raw value that arrives from outside (a probe, a wire, a
// caller) where a refusal is an expected outcome rather than a defect.
template <auto Pred, typename T, typename Error>
    requires PredicateInvocableOn<Pred, T> && std::is_scoped_enum_v<Error>
[[nodiscard]] constexpr ::foundation::core::Result<Refined<Pred, T>, Error>
admit_refined(T value, Error refusal) noexcept(std::is_nothrow_move_constructible_v<T>) {
    if (!Pred(value)) {
        return ::foundation::core::err(refusal);
    }
    return mint_refined<Pred>(std::move(value));
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
// participates in the implication relation of fixy/Predicates.h, so a
// non-empty span strengthens to a longer-minimum one without being
// re-validated.  non_empty stays useful for a container whose size is
// not cheap to compute, which a span's is.
template <typename T>
using NonEmptySpan = Refined<length_ge<std::size_t{1}>, std::span<T>>;

// N of zero is permitted here as a degenerate any-length alias,
// because the underlying predicate is vacuous at zero. The implication
// relation of fixy/Predicates.h correctly refuses to bridge that case
// to non_empty.
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
// naming the wrapper.  Both refinements are one template, so one
// reflection query in foundation/reflect/Instance.h answers for both,
// and the cv-ref strip is that query's.  Refined and SealedRefined are
// alias templates, which a reflection query cannot name: the query
// dealiases to the class template either way, so ^^Refinement is the
// only spelling that works and the only one needed.

// The concept is the question; the value spelling is derived from it
// and read by nothing that gates.
template <typename T>
concept IsRefined = ::foundation::reflect::IsInstanceOf<T, ^^Refinement>;

template <typename T>
inline constexpr bool is_refined_v = IsRefined<T>;

template <typename T>
    requires IsRefined<T>
using refined_value_t = typename std::remove_cvref_t<T>::value_type;

template <typename T>
    requires IsRefined<T>
using refined_predicate_type_t = typename std::remove_cvref_t<T>::predicate_type;

// The sealed-ness is a template argument, not a separate class
// template, so this reads the member the class publishes.  The other
// traits beside it read members the same way.
template <typename T>
    requires IsRefined<T>
inline constexpr bool refined_is_sealed_v = std::remove_cvref_t<T>::is_sealed;

}  // namespace fixy
