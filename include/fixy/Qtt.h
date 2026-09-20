#pragma once

// Move-only wrapper over one QTT usage grade.  Linear<T> enforces
// "consumed exactly once" and Affine<T> enforces "consumed at most
// once"; the two are Qtt<Grade, T> at the grades One and Zero.
//
// Wrap every resource-carrying type in Linear: a file handle, an mmap
// region, an arena-owned object with drop semantics, a channel
// endpoint.
//
// An Affine value is consumed once or never.  Letting it go out of
// scope unconsumed is a first-class outcome, not an error.  Use it
// where the value is a resource that must not be duplicated but whose
// owner is free to abandon it: a speculative result the primary path
// obviates, a prefetch that is never observed, a branch that gets
// pruned.
//
// The grade is a static property of the wrapper and is not derived
// from the bytes of T, so peek_mut and swap cannot violate it.
//
// The deleted copy and the rvalue-qualified consume are the whole
// linearity guarantee: a second consume of the same lvalue after
// std::move is not detected at runtime.  A Debug-only consumed flag was
// rejected because it makes sizeof(Linear<T>) depend on NDEBUG, and the
// release preset links a library built with NDEBUG against tests built
// without it.

#include <fixy/GradedFacade.h>
#include <foundation/Platform.h>
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/lattices/QttSemiring.h>
#include <foundation/reflect/Instance.h>

#include <concepts>
#include <cstdlib>
#include <memory>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace fixy {

// A type is already linear when its own discipline encodes the
// exactly-once obligation, and consume-disciplined when it already
// encodes a use bound at least as tight as at-most-once.  Wrapping the
// first in Linear adds no guarantee; wrapping the second in Affine
// makes a required consume optional.  A new token of either family
// adds its specialization here rather than being wrapped at a call
// site.
//
// The cv-ref strip is what closes the slip-through: partial
// specialization does not match a cv-qualified or reference-qualified
// argument, so `Linear<const Token>` would otherwise pass the
// rejection.
namespace detail {

template <typename T>
struct is_already_linear_impl : std::false_type {};

template <typename T>
struct is_already_consume_disciplined_impl : std::false_type {};

// Only the two bounded grades name a consume discipline.  A value at
// Omega may be used freely, and that is the bare T.
template <auto Grade>
concept IsConsumeBound = std::same_as<decltype(Grade), ::foundation::algebra::lattices::QttGrade>
                      && (Grade == ::foundation::algebra::lattices::QttGrade::One
                          || Grade == ::foundation::algebra::lattices::QttGrade::Zero);

}  // namespace detail

template <typename T>
struct is_already_linear : detail::is_already_linear_impl<std::remove_cvref_t<T>> {};

template <typename T>
inline constexpr bool is_already_linear_v = is_already_linear<T>::value;

template <typename T>
struct is_already_consume_disciplined : detail::is_already_consume_disciplined_impl<std::remove_cvref_t<T>> {};

template <typename T>
inline constexpr bool is_already_consume_disciplined_v = is_already_consume_disciplined<T>::value;

template <auto Grade, class T>
    requires detail::IsConsumeBound<Grade>
class Qtt;

template <class T>
using Linear = Qtt<::foundation::algebra::lattices::QttGrade::One, T>;

template <class T>
using Affine = Qtt<::foundation::algebra::lattices::QttGrade::Zero, T>;

// The constructors are private, so these two are the only door.
template <class T, class... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr Linear<T> mint_linear(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>);

template <class T, class... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr Affine<T> mint_affine(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>);

template <auto Grade, class T>
    requires detail::IsConsumeBound<Grade>
class [[nodiscard]] Qtt : public graded_facade<::foundation::algebra::ModalityKind::Absolute,
                                               ::foundation::algebra::lattices::QttSemiring::At<Grade>, T> {
    // Placed in the class body so the diagnostic surfaces at the user's
    // instantiation site rather than inside the substrate.
    static_assert(Grade != ::foundation::algebra::lattices::QttGrade::One || !is_already_linear_v<T>,
                  "Linear<Token> over an already-linear token is redundant: the token IS already a "
                  "move-only linearity token (deleted copy, [[nodiscard]], sizeof = 1, EBO-collapsible).  "
                  "Wrapping it stacks two disciplines without adding a new bug class.  Use the token "
                  "directly.");
    static_assert(Grade != ::foundation::algebra::lattices::QttGrade::Zero || !is_already_consume_disciplined_v<T>,
                  "Affine<Token> over a consume-disciplined token is unsound: the token carries an "
                  "EXACTLY-ONCE obligation; wrapping in Affine downgrades that to at-most-once, making "
                  "the consume OPTIONAL when it is REQUIRED.  Use the token directly.");

public:
    // value_type, modality and the two name forwarders arrive from
    // graded_facade.  The base is dependent, so the two names this
    // class body uses unqualified are re-declared here rather than
    // found by lookup.
    using facade_ = graded_facade<::foundation::algebra::ModalityKind::Absolute,
                                  ::foundation::algebra::lattices::QttSemiring::At<Grade>, T>;
    using typename facade_::graded_type;
    using typename facade_::lattice_type;

private:
    graded_type impl_;

    [[nodiscard]] static constexpr typename lattice_type::element_type pinned_grade() noexcept {
        return typename lattice_type::element_type{};
    }

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit Qtt(std::in_place_t, Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                                     && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), pinned_grade()} {}

    template <class U, class... Args>
        requires std::is_constructible_v<U, Args...>
    friend constexpr Linear<U> mint_linear(Args&&... args) noexcept(std::is_nothrow_constructible_v<U, Args...>);

    template <class U, class... Args>
        requires std::is_constructible_v<U, Args...>
    friend constexpr Affine<U> mint_affine(Args&&... args) noexcept(std::is_nothrow_constructible_v<U, Args...>);

public:
    Qtt(const Qtt&) = delete("Linear<T> and Affine<T> are move-only; use std::move or drop()");
    Qtt& operator=(const Qtt&) = delete("Linear<T> and Affine<T> are move-only; use std::move or drop()");
    Qtt(Qtt&&) = default;
    Qtt& operator=(Qtt&&) = default;
    ~Qtt() = default;

    [[nodiscard]] constexpr T consume() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return std::move(impl_).consume();
    }

    [[nodiscard]] constexpr const T& peek() const& noexcept { return impl_.peek(); }

    // Prefer consume and reconstruct over this when the change is
    // semantic rather than incidental.
    [[nodiscard]] constexpr T& peek_mut() & noexcept { return impl_.peek_mut(); }

    constexpr void swap(Qtt& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }

    friend constexpr void swap(Qtt& a, Qtt& b) noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    // A Linear is consumed on the way out.  For an Affine this is a
    // no-op that records the deliberate choice not to consume, so that
    // a search distinguishes intentional discards from consume sites.
    friend constexpr void drop(Qtt&& x) noexcept(Grade != ::foundation::algebra::lattices::QttGrade::One
                                                 || std::is_nothrow_move_constructible_v<T>) {
        if constexpr (Grade == ::foundation::algebra::lattices::QttGrade::One) {
            (void)std::move(x).consume();
        } else {
            (void)x;
        }
    }
};

template <class T, class... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr Linear<T> mint_linear(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    return Linear<T>{std::in_place, std::forward<Args>(args)...};
}

template <class T, class... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr Affine<T> mint_affine(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    return Affine<T>{std::in_place, std::forward<Args>(args)...};
}

static_assert(sizeof(Linear<int>) == sizeof(int));
static_assert(sizeof(Linear<void*>) == sizeof(void*));
static_assert(sizeof(Linear<long long>) == sizeof(long long));

static_assert(sizeof(Affine<int>) == sizeof(int));
static_assert(sizeof(Affine<void*>) == sizeof(void*));
static_assert(sizeof(Affine<long long>) == sizeof(long long));

static_assert(Linear<int>::modality == ::foundation::algebra::ModalityKind::Absolute);
static_assert(Affine<int>::modality == ::foundation::algebra::ModalityKind::Absolute);
static_assert(std::is_same_v<Linear<int>::lattice_type, ::foundation::algebra::lattices::qtt::LinearGrade>);
static_assert(std::is_same_v<Affine<int>::lattice_type, ::foundation::algebra::lattices::qtt::Erased>);

// Qtt carries a non-type parameter, which is the case the reflection
// form in foundation/reflect/Instance.h exists for.  The cv-ref strip
// and the non-template rejection are pinned beside it.
static_assert(::foundation::reflect::is_instance_of_v<Linear<int>, ^^Qtt>);
static_assert(::foundation::reflect::is_instance_of_v<Affine<int>, ^^Qtt>);
static_assert(::foundation::reflect::is_instance_of_v<Linear<int> const&, ^^Qtt>);
static_assert(::foundation::reflect::is_instance_of_v<Linear<int>&&, ^^Qtt>);
static_assert(!::foundation::reflect::is_instance_of_v<int, ^^Qtt>);
static_assert(!::foundation::reflect::is_instance_of_v<void, ^^Qtt>);
static_assert(!::foundation::reflect::is_instance_of_v<std::unique_ptr<int>, ^^Qtt>);
static_assert(!::foundation::reflect::is_instance_of_v<Linear<int>, ^^std::unique_ptr>);

}  // namespace fixy
