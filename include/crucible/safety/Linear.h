#pragma once

// Move-only wrapper enforcing "consumed exactly once".
//
// Wrap every resource-carrying type in it: a file handle, an mmap
// region, an arena-owned object with drop semantics, a channel
// endpoint.
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

// Permission and SharedPermission are already move-only exactly-once
// tokens, so wrapping one adds no guarantee and costs its collapse to
// a single byte.  They are forward declared here so the rejection
// below fires without dragging their definitions into every consumer
// of this header.
template <typename Tag>
class Permission;
template <typename Tag>
class SharedPermission;

// A type is already linear when its own discipline encodes the
// exactly-once obligation.  A new token of that family adds its
// specialization here rather than being wrapped at a call site.
//
// The cv-ref strip is what closes the slip-through: partial
// specialization does not match a cv-qualified or reference-qualified
// argument, so `Linear<const Permission<Tag>>` would otherwise pass
// the rejection.
namespace detail {

template <typename T>
struct is_already_linear_impl : std::false_type {};

template <typename Tag>
struct is_already_linear_impl<Permission<Tag>> : std::true_type {};

template <typename Tag>
struct is_already_linear_impl<SharedPermission<Tag>> : std::true_type {};

}  // namespace detail

template <typename T>
struct is_already_linear : detail::is_already_linear_impl<std::remove_cvref_t<T>> {};

template <typename T>
inline constexpr bool is_already_linear_v = is_already_linear<T>::value;

template <typename T>
class [[nodiscard]] Linear {
    // Placed in the class body so the diagnostic surfaces at the user's
    // instantiation site rather than inside the substrate.
    static_assert(!is_already_linear_v<T>, "Linear<Permission<Tag>> / Linear<SharedPermission<Tag>> is "
                                           "redundant: Permission IS already a move-only linearity token "
                                           "(deleted copy, [[nodiscard]], sizeof = 1, EBO-collapsible).  "
                                           "Wrapping it stacks two disciplines without adding a new bug "
                                           "class.  Use Permission<Tag> directly.");

public:
    using value_type = T;
    using lattice_type = ::crucible::algebra::lattices::QttSemiring::At<::crucible::algebra::lattices::QttGrade::One>;

    static constexpr ::crucible::algebra::ModalityKind modality = ::crucible::algebra::ModalityKind::Absolute;

    using graded_type = ::crucible::algebra::Graded<::crucible::algebra::ModalityKind::Absolute, lattice_type, T>;

private:
    graded_type impl_;

    [[nodiscard]] static constexpr typename lattice_type::element_type grade_one() noexcept {
        return typename lattice_type::element_type{};
    }

public:
    constexpr explicit Linear(T v) noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(v), grade_one()} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit Linear(std::in_place_t, Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>
                                                                        && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), grade_one()} {}

    Linear(const Linear&) = delete("Linear<T> is move-only; use std::move or drop()");
    Linear& operator=(const Linear&) = delete("Linear<T> is move-only; use std::move or drop()");
    Linear(Linear&&) = default;
    Linear& operator=(Linear&&) = default;
    ~Linear() = default;

    [[nodiscard]] constexpr T consume() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return std::move(impl_).consume();
    }

    [[nodiscard]] constexpr const T& peek() const& noexcept { return impl_.peek(); }

    // Prefer consume and reconstruct over this when the change is
    // semantic rather than incidental.
    [[nodiscard]] constexpr T& peek_mut() & noexcept { return impl_.peek_mut(); }

    constexpr void swap(Linear& other) noexcept(std::is_nothrow_swappable_v<T>) { impl_.swap(other.impl_); }

    friend constexpr void swap(Linear& a, Linear& b) noexcept(std::is_nothrow_swappable_v<T>) { a.swap(b); }

    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept { return graded_type::lattice_name(); }
};

template <typename T>
Linear(T) -> Linear<T>;

template <typename T, typename... Args>
    requires std::is_constructible_v<T, Args...>
[[nodiscard]] constexpr Linear<T> mint_linear(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    return Linear<T>{std::in_place, std::forward<Args>(args)...};
}

template <typename T>
constexpr void drop(Linear<T>&& x) noexcept(std::is_nothrow_move_constructible_v<T>) {
    (void)std::move(x).consume();
}

static_assert(sizeof(Linear<int>) == sizeof(int));
static_assert(sizeof(Linear<void*>) == sizeof(void*));
static_assert(sizeof(Linear<long long>) == sizeof(long long));

namespace detail::linear_self_test {

inline void runtime_smoke_test() {
    int seed = 41;
    Linear<int> a{std::in_place, seed + 1};
    if (a.peek() != 42) std::abort();

    a.peek_mut() = 100;
    if (a.peek() != 100) std::abort();

    Linear<int> b{std::in_place, 7};
    swap(a, b);
    if (a.peek() != 7 || b.peek() != 100) std::abort();

    a.swap(b);
    if (a.peek() != 100 || b.peek() != 7) std::abort();

    Linear<int> c = std::move(a);
    int extracted = std::move(c).consume();
    if (extracted != 100) std::abort();

    auto m = mint_linear<int>(seed);
    if (m.peek() != 41) std::abort();

    struct only_move {
        only_move(int v) : p{std::make_unique<int>(v)} {}
        only_move(only_move&&) = default;
        only_move& operator=(only_move&&) = default;
        only_move(const only_move&) = delete;
        only_move& operator=(const only_move&) = delete;
        std::unique_ptr<int> p;
    };
    Linear<only_move> mo{std::in_place, 123};
    if (*mo.peek().p != 123) std::abort();

    Linear<int> to_drop{std::in_place, 999};
    drop(std::move(to_drop));
}

}  // namespace detail::linear_self_test

}  // namespace crucible::safety
