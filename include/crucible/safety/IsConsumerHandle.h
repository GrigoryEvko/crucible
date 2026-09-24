#pragma once

#include <cstddef>
#include <optional>
#include <type_traits>

namespace crucible::safety::extract {

namespace detail {

template <typename M>
struct consumer_signature_decomp {
    static constexpr bool matches = false;
    using payload = void;
};

template <typename C, typename P>
struct consumer_signature_decomp<std::optional<P> (C::*)()> {
    static constexpr bool matches = true;
    using payload = P;
};

template <typename C, typename P>
struct consumer_signature_decomp<std::optional<P> (C::*)() noexcept> {
    static constexpr bool matches = true;
    using payload = P;
};

template <typename T, typename = void>
struct try_pop_shape {
    static constexpr bool matches = false;
    using payload = void;
};

template <typename T>
struct try_pop_shape<T, std::void_t<decltype(&T::try_pop)>> {
    using mptr_t = decltype(&T::try_pop);
    using decomp = consumer_signature_decomp<mptr_t>;
    static constexpr bool matches = decomp::matches;
    using payload = typename decomp::payload;
};

template <typename T, typename = void>
struct has_try_push : std::false_type {};

template <typename T>
struct has_try_push<T, std::void_t<decltype(&T::try_push)>> : std::true_type {};

}  // namespace detail

// The optional return is what carries the empty signal, so a try_pop that
// returns bool and fills an out-parameter is a different endpoint shape and is
// deliberately not admitted. A type exposing both try_pop and try_push is also
// rejected, because nothing then decides which pole it is.

template <typename T>
inline constexpr bool is_consumer_handle_v =
    detail::try_pop_shape<std::remove_cvref_t<T>>::matches && !detail::has_try_push<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsConsumerHandle = is_consumer_handle_v<T>;

template <typename T>
    requires is_consumer_handle_v<T>
using consumer_handle_value_t = typename detail::try_pop_shape<std::remove_cvref_t<T>>::payload;

namespace detail::is_consumer_handle_self_test {

struct synthetic_consumer {
    [[nodiscard]] std::optional<int> try_pop() noexcept { return {}; }
};

struct synthetic_producer {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

struct synthetic_hybrid {
    [[nodiscard]] std::optional<int> try_pop() noexcept { return {}; }
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

struct synthetic_bool_pop {
    [[nodiscard]] bool try_pop() noexcept { return false; }
};

struct synthetic_value_pop {
    [[nodiscard]] int try_pop() noexcept { return 0; }
};

struct synthetic_overloaded_pop {
    [[nodiscard]] std::optional<int> try_pop() noexcept { return {}; }
    [[nodiscard]] std::optional<int> try_pop(int) noexcept { return {}; }
};

struct synthetic_double_consumer {
    [[nodiscard]] std::optional<double> try_pop() noexcept { return {}; }
};

static_assert(is_consumer_handle_v<synthetic_consumer>);
static_assert(IsConsumerHandle<synthetic_consumer>);
static_assert(is_consumer_handle_v<synthetic_double_consumer>);

static_assert(is_consumer_handle_v<synthetic_consumer&>);
static_assert(is_consumer_handle_v<synthetic_consumer&&>);
static_assert(is_consumer_handle_v<synthetic_consumer const&>);

static_assert(!is_consumer_handle_v<int>);
static_assert(!is_consumer_handle_v<int*>);
static_assert(!is_consumer_handle_v<void>);
static_assert(!is_consumer_handle_v<synthetic_producer>);
static_assert(!is_consumer_handle_v<synthetic_hybrid>);
static_assert(!is_consumer_handle_v<synthetic_bool_pop>);
static_assert(!is_consumer_handle_v<synthetic_value_pop>);
static_assert(!is_consumer_handle_v<synthetic_overloaded_pop>);

static_assert(!is_consumer_handle_v<synthetic_consumer*>);

static_assert(std::is_same_v<consumer_handle_value_t<synthetic_consumer>, int>);
static_assert(std::is_same_v<consumer_handle_value_t<synthetic_double_consumer>, double>);

static_assert(std::is_same_v<consumer_handle_value_t<synthetic_consumer&>, int>);
static_assert(std::is_same_v<consumer_handle_value_t<synthetic_consumer const&>, int>);

}  // namespace detail::is_consumer_handle_self_test

inline bool is_consumer_handle_smoke_test() noexcept {
    using namespace detail::is_consumer_handle_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_consumer_handle_v<synthetic_consumer>;
        ok = ok && IsConsumerHandle<synthetic_consumer&&>;
        ok = ok && !is_consumer_handle_v<int>;
        ok = ok && !is_consumer_handle_v<synthetic_producer>;
        ok = ok && !is_consumer_handle_v<synthetic_hybrid>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
