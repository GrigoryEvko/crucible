#pragma once

#include <cstddef>
#include <type_traits>

namespace crucible::safety::extract {

namespace detail {

template <typename M>
struct producer_signature_decomp {
    static constexpr bool matches = false;
    using payload = void;
};

template <typename C, typename P>
struct producer_signature_decomp<bool (C::*)(P const&)> {
    static constexpr bool matches = true;
    using payload = P;
};

template <typename C, typename P>
struct producer_signature_decomp<bool (C::*)(P const&) noexcept> {
    static constexpr bool matches = true;
    using payload = P;
};

template <typename T, typename = void>
struct try_push_shape {
    static constexpr bool matches = false;
    using payload = void;
};

template <typename T>
struct try_push_shape<T, std::void_t<decltype(&T::try_push)>> {
    using mptr_t = decltype(&T::try_push);
    using decomp = producer_signature_decomp<mptr_t>;
    static constexpr bool matches = decomp::matches;
    using payload = typename decomp::payload;
};

template <typename T, typename = void>
struct has_try_pop : std::false_type {};

template <typename T>
struct has_try_pop<T, std::void_t<decltype(&T::try_pop)>> : std::true_type {};

}  // namespace detail

// Detection goes through the address of the member rather than through an
// expression such as `t.try_push(v)`, because that form needs the payload type
// up front and this predicate exists to recover it. The cost is that an
// overloaded try_push is rejected: the address of an overload set is
// ill-formed. A type exposing both try_push and try_pop is also rejected,
// because nothing then decides which pole it is.

template <typename T>
inline constexpr bool is_producer_handle_v =
    detail::try_push_shape<std::remove_cvref_t<T>>::matches && !detail::has_try_pop<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsProducerHandle = is_producer_handle_v<T>;

template <typename T>
    requires is_producer_handle_v<T>
using producer_handle_value_t = typename detail::try_push_shape<std::remove_cvref_t<T>>::payload;

namespace detail::is_producer_handle_self_test {

struct synthetic_producer {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

struct synthetic_consumer {
    [[nodiscard]] int try_pop() noexcept { return 0; }
};

struct synthetic_hybrid {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
    [[nodiscard]] int try_pop() noexcept { return 0; }
};

struct synthetic_void_push {
    void try_push(int const&) noexcept {}
};

struct synthetic_overloaded_push {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
    [[nodiscard]] bool try_push(float const&) noexcept { return true; }
};

struct synthetic_non_const_ref_push {
    [[nodiscard]] bool try_push(int&) noexcept { return true; }
};

struct synthetic_by_value_push {
    [[nodiscard]] bool try_push(int) noexcept { return true; }
};

struct synthetic_double_producer {
    [[nodiscard]] bool try_push(double const&) noexcept { return true; }
};

static_assert(is_producer_handle_v<synthetic_producer>);
static_assert(IsProducerHandle<synthetic_producer>);
static_assert(is_producer_handle_v<synthetic_double_producer>);

static_assert(is_producer_handle_v<synthetic_producer&>);
static_assert(is_producer_handle_v<synthetic_producer&&>);
static_assert(is_producer_handle_v<synthetic_producer const&>);

static_assert(!is_producer_handle_v<int>);
static_assert(!is_producer_handle_v<int*>);
static_assert(!is_producer_handle_v<void>);
static_assert(!is_producer_handle_v<synthetic_consumer>);
static_assert(!is_producer_handle_v<synthetic_hybrid>);
static_assert(!is_producer_handle_v<synthetic_void_push>);
static_assert(!is_producer_handle_v<synthetic_overloaded_push>);
static_assert(!is_producer_handle_v<synthetic_non_const_ref_push>);
static_assert(!is_producer_handle_v<synthetic_by_value_push>);

static_assert(!is_producer_handle_v<synthetic_producer*>);

static_assert(std::is_same_v<producer_handle_value_t<synthetic_producer>, int>);
static_assert(std::is_same_v<producer_handle_value_t<synthetic_double_producer>, double>);

static_assert(std::is_same_v<producer_handle_value_t<synthetic_producer&>, int>);
static_assert(std::is_same_v<producer_handle_value_t<synthetic_producer const&>, int>);

}  // namespace detail::is_producer_handle_self_test

inline bool is_producer_handle_smoke_test() noexcept {
    using namespace detail::is_producer_handle_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_producer_handle_v<synthetic_producer>;
        ok = ok && IsProducerHandle<synthetic_producer&&>;
        ok = ok && !is_producer_handle_v<int>;
        ok = ok && !is_producer_handle_v<synthetic_consumer>;
        ok = ok && !is_producer_handle_v<synthetic_hybrid>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
