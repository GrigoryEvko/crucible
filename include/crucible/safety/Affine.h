#pragma once

// Move-only wrapper enforcing "consumed at most once".
//
// A value is consumed once or never.  Letting it go out of scope
// unconsumed is a first-class outcome, not an error.  Use it where the
// value is a resource that must not be duplicated but whose owner is
// free to abandon it: a speculative result the primary path obviates,
// a prefetch that is never observed, a branch that gets pruned.
//
// The grade is a static property of the wrapper and is not derived
// from the bytes of T, so peek_mut and swap cannot violate it.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/QttSemiring.h>

#include <cstdlib>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// Permission and SharedPermission carry an exactly-once obligation.
// Affine's bound is weaker, so wrapping one in the other would make a
// required consume optional.  They are forward declared here so the
// rejection below fires without dragging their definitions into every
// consumer of this header.
template <typename Tag>
class Permission;
template <typename Tag>
class SharedPermission;

// A type is consume-disciplined when it already encodes a use bound at
// least as tight as at-most-once.  A new token of that family adds its
// specialization here rather than being wrapped at a call site.
//
// The cv-ref strip is what closes the slip-through: partial
// specialization does not match a cv-qualified or reference-qualified
// argument, so `Affine<const Permission<Tag>>` would otherwise pass
// the rejection.
namespace detail {

template <typename T>
struct is_already_consume_disciplined_impl : std::false_type {};

template <typename Tag>
struct is_already_consume_disciplined_impl<Permission<Tag>> : std::true_type {};

template <typename Tag>
struct is_already_consume_disciplined_impl<SharedPermission<Tag>> : std::true_type {};

}  // namespace detail

template <typename T>
struct is_already_consume_disciplined : detail::is_already_consume_disciplined_impl<std::remove_cvref_t<T>> {};

template <typename T>
inline constexpr bool is_already_consume_disciplined_v = is_already_consume_disciplined<T>::value;

template <typename T>
class [[nodiscard]] Affine {
    // Placed in the class body so the diagnostic surfaces at the user's
    // instantiation site rather than inside the substrate.
    static_assert(!is_already_consume_disciplined_v<T>, "Affine<Permission<Tag>> / Affine<SharedPermission<Tag>> is "
                                                        "unsound: Permission carries an EXACTLY-ONCE obligation; "
                                                        "wrapping in Affine downgrades that to at-most-once, making "
                                                        "the consume OPTIONAL when it is REQUIRED.  Use "
                                                        "Permission<Tag> directly.");

public:
    using value_type = T;
    using lattice_type = ::crucible::algebra::lattices::QttSemiring::At<::crucible::algebra::lattices::QttGrade::Zero>;

    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;

    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;

private:
    graded_type impl_;

    [[nodiscard]] static constexpr typename lattice_type::element_type grade_zero() noexcept {
        return typename lattice_type::element_type{};
    }

public:
    constexpr explicit Affine(T v) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(v), grade_zero()} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit Affine(std::in_place_t, Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                                        && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), grade_zero()} {}

    Affine(const Affine&) = delete("Affine<T> is move-only; use std::move or drop()");
    Affine& operator=(const Affine&) = delete("Affine<T> is move-only; use std::move or drop()");
    Affine(Affine&&) = default;
    Affine& operator=(Affine&&) = default;
    ~Affine() = default;

    [[nodiscard]] constexpr T consume() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return std::move(impl_).consume();
    }

    [[nodiscard]] constexpr const T& peek() const& noexcept { return impl_.peek(); }

    [[nodiscard]] constexpr T& peek_mut() & noexcept { return impl_.peek_mut(); }

    constexpr void swap(Affine& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }

    friend constexpr void swap(Affine& a, Affine& b) noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept { return graded_type::lattice_name(); }
};

template <typename T>
Affine(T) -> Affine<T>;

template <typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr Affine<T> mint_affine(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    return Affine<T>{std::in_place, std::forward<Args>(args)...};
}

// A no-op that records the deliberate choice not to consume, so that a
// search distinguishes intentional discards from consume sites.
template <typename T>
constexpr void drop(Affine<T>&& x) noexcept {
    (void)x;
}

static_assert(sizeof(Affine<int>) == sizeof(int));
static_assert(sizeof(Affine<void*>) == sizeof(void*));
static_assert(sizeof(Affine<long long>) == sizeof(long long));

static_assert(Affine<int>::modality == ::crucible::algebra::ModalityKind::Absolute);
static_assert(
    std::is_same_v<Affine<int>::lattice_type,
                   ::crucible::algebra::lattices::QttSemiring::At<::crucible::algebra::lattices::QttGrade::Zero>>);

namespace detail::affine_self_test {

inline void runtime_smoke_test() {
    int seed = 41;
    Affine<int> a{std::in_place, seed + 1};
    if (a.peek() != 42) std::abort();

    a.peek_mut() = 100;
    if (a.peek() != 100) std::abort();

    Affine<int> b{std::in_place, 7};
    swap(a, b);
    if (a.peek() != 7 || b.peek() != 100) std::abort();

    a.swap(b);
    if (a.peek() != 100 || b.peek() != 7) std::abort();

    Affine<int> c = std::move(a);
    int extracted = std::move(c).consume();
    if (extracted != 100) std::abort();

    auto m = mint_affine<int>(seed);
    if (m.peek() != 41) std::abort();

    struct only_move {
        only_move(int v) : p{std::make_unique<int>(v)} {}
        only_move(only_move&&) = default;
        only_move& operator=(only_move&&) = default;
        only_move(const only_move&) = delete;
        only_move& operator=(const only_move&) = delete;
        std::unique_ptr<int> p;
    };
    Affine<only_move> mo{std::in_place, 123};
    if (*mo.peek().p != 123) std::abort();

    Affine<int> to_drop{std::in_place, 999};
    drop(std::move(to_drop));

    // The zero-uses corner of the at-most-once bound.
    {
        Affine<int> silent{std::in_place, 314};
        if (silent.peek() != 314) std::abort();
    }
}

}  // namespace detail::affine_self_test

}  // namespace crucible::safety
