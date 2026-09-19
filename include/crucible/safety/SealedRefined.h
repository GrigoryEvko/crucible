#pragma once

// A refinement with no way to extract the value back out.  Every
// change to a sealed value therefore goes through a fresh
// construction, which re-runs the predicate.  That closes the pattern
// of extracting a value, mutating it behind the predicate's back and
// quietly re-wrapping it.
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

#include <crucible/Platform.h>
#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/lattices/_BoolLattice.h>
#include <crucible/safety/Refined.h>

#include <compare>
#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

template <auto Pred, typename T>
class [[nodiscard]] SealedRefined {
public:
    using value_type = T;
    using predicate_type = decltype(Pred);
    using lattice_type = ::crucible::algebra::lattices::BoolLattice<std::remove_cv_t<decltype(Pred)>>;
    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;
    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;

private:
    graded_type impl_;

public:
    // Names a construction whose caller has already proven the
    // invariant, so the predicate is not run again.
    struct Trusted {};

    // The invocability concept is what turns a predicate that does not
    // accept a T into a readable diagnostic at the call site, instead
    // of a substitution failure inside the contract clause.
    constexpr explicit SealedRefined(T v) noexcept(std::is_nothrow_move_constructible_v<T>)
        requires PredicateInvocableOn<Pred, T>
    pre(Pred(v)) : impl_{std::move(v), typename lattice_type::element_type{}} {}

    constexpr SealedRefined(T v, Trusted) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(v), typename lattice_type::element_type{}} {}

    // No check is needed here: the source's own invariant is the proof.
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

static_assert(sizeof(SealedRefined<positive, int>) == sizeof(int));
static_assert(sizeof(SealedRefined<non_null, void*>) == sizeof(void*));

// Production code admits a value into the sealed refinement here, so
// that a search finds every such admission.
template <auto Pred, typename T>
    requires PredicateInvocableOn<Pred, T>
[[nodiscard]] constexpr SealedRefined<Pred, T>
mint_sealed_refined(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
    return SealedRefined<Pred, T>{std::move(value)};
}

namespace detail::sealed_refined_self_test {

inline void runtime_smoke_test() {
    int seed = 5;

    SealedRefined<positive, int> sp{seed};
    if (sp.value() != 5) std::abort();

    auto spm = mint_sealed_refined<positive, int>(seed);
    if (spm.value() != 5) std::abort();

    // The trusted path admits a value the predicate would reject.
    int sentinel = -3;
    SealedRefined<positive, int> tp{sentinel, SealedRefined<positive, int>::Trusted{}};
    if (tp.value() != -3) std::abort();

    Refined<positive, int> r{seed * 2};
    SealedRefined<positive, int> from_r{std::move(r)};
    if (from_r.value() != 10) std::abort();

    SealedRefined<positive, int> sp_eq{seed};
    if (!(sp == sp_eq)) std::abort();
    SealedRefined<positive, int> sp_lt{seed - 1};
    if ((sp_lt <=> sp) != std::strong_ordering::less) std::abort();

    SealedRefined<positive, int> sp_copy = sp;
    if (sp_copy.value() != 5) std::abort();
    SealedRefined<positive, int> sp_move = std::move(sp_copy);
    if (sp_move.value() != 5) std::abort();
}

}  // namespace detail::sealed_refined_self_test

}  // namespace crucible::safety
