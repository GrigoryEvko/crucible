#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace crucible::safety::extract {

namespace detail {

template <typename M>
struct publish_signature_decomp {
    static constexpr bool matches = false;
    using payload = void;
};

template <typename C, typename P>
struct publish_signature_decomp<void (C::*)(P const&)> {
    static constexpr bool matches = true;
    using payload = P;
};

template <typename C, typename P>
struct publish_signature_decomp<void (C::*)(P const&) noexcept> {
    static constexpr bool matches = true;
    using payload = P;
};

template <typename T, typename = void>
struct publish_shape {
    static constexpr bool matches = false;
    using payload = void;
};

template <typename T>
struct publish_shape<T, std::void_t<decltype(&T::publish)>> {
    using mptr_t = decltype(&T::publish);
    using decomp = publish_signature_decomp<mptr_t>;
    static constexpr bool matches = decomp::matches;
    using payload = typename decomp::payload;
};

template <typename M>
struct load_signature_decomp {
    static constexpr bool matches = false;
    using payload = void;
};

template <typename C, typename P>
struct load_signature_decomp<P (C::*)() const> {
    static constexpr bool matches = true;
    using payload = P;
};

template <typename C, typename P>
struct load_signature_decomp<P (C::*)() const noexcept> {
    static constexpr bool matches = true;
    using payload = P;
};

template <typename T, typename = void>
struct load_shape {
    static constexpr bool matches = false;
    using payload = void;
};

template <typename T>
struct load_shape<T, std::void_t<decltype(&T::load)>> {
    using mptr_t = decltype(&T::load);
    using decomp = load_signature_decomp<mptr_t>;
    static constexpr bool matches = decomp::matches && !std::is_void_v<typename decomp::payload>;
    using payload = typename decomp::payload;
};

template <typename T, typename = void>
struct has_publish : std::false_type {};

template <typename T>
struct has_publish<T, std::void_t<decltype(&T::publish)>> : std::true_type {};

template <typename T, typename = void>
struct has_load : std::false_type {};

template <typename T>
struct has_load<T, std::void_t<decltype(&T::load)>> : std::true_type {};

}  // namespace detail

// The void return on publish is part of the shape, not an accident. A publish
// that returns bool belongs to a capacity-limited endpoint that can refuse a
// value, which routes differently, so it is not admitted here. The two
// predicates exclude each other: a type carrying both members has no
// determined pole.

template <typename T>
inline constexpr bool is_swmr_writer_v =
    detail::publish_shape<std::remove_cvref_t<T>>::matches && !detail::has_load<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsSwmrWriter = is_swmr_writer_v<T>;

template <typename T>
    requires is_swmr_writer_v<T>
using swmr_writer_value_t = typename detail::publish_shape<std::remove_cvref_t<T>>::payload;

template <typename T>
inline constexpr bool is_swmr_reader_v =
    detail::load_shape<std::remove_cvref_t<T>>::matches && !detail::has_publish<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsSwmrReader = is_swmr_reader_v<T>;

template <typename T>
    requires is_swmr_reader_v<T>
using swmr_reader_value_t = typename detail::load_shape<std::remove_cvref_t<T>>::payload;

namespace detail::is_swmr_handle_self_test {

struct synthetic_writer {
    void publish(int const&) noexcept {}
};

struct synthetic_reader {
    [[nodiscard]] int load() const noexcept { return 0; }
};

struct synthetic_swmr_hybrid {
    void publish(int const&) noexcept {}
    [[nodiscard]] int load() const noexcept { return 0; }
};

struct synthetic_bool_publish {
    [[nodiscard]] bool publish(int const&) noexcept { return true; }
};

struct synthetic_by_value_publish {
    void publish(int) noexcept {}
};

struct synthetic_non_const_load {
    [[nodiscard]] int load() noexcept { return 0; }
};

struct synthetic_void_load {
    void load() const noexcept {}
};

struct synthetic_producer_handle {
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

struct synthetic_consumer_handle {
    [[nodiscard]] int try_pop() noexcept { return 0; }
};

struct synthetic_double_writer {
    void publish(double const&) noexcept {}
};

struct synthetic_double_reader {
    [[nodiscard]] double load() const noexcept { return 0.0; }
};

static_assert(is_swmr_writer_v<synthetic_writer>);
static_assert(IsSwmrWriter<synthetic_writer>);
static_assert(is_swmr_writer_v<synthetic_double_writer>);
static_assert(is_swmr_writer_v<synthetic_writer&>);
static_assert(is_swmr_writer_v<synthetic_writer&&>);
static_assert(is_swmr_writer_v<synthetic_writer const&>);

static_assert(!is_swmr_writer_v<int>);
static_assert(!is_swmr_writer_v<void>);
static_assert(!is_swmr_writer_v<synthetic_reader>);
static_assert(!is_swmr_writer_v<synthetic_swmr_hybrid>);
static_assert(!is_swmr_writer_v<synthetic_bool_publish>);
static_assert(!is_swmr_writer_v<synthetic_by_value_publish>);
static_assert(!is_swmr_writer_v<synthetic_producer_handle>);
static_assert(!is_swmr_writer_v<synthetic_consumer_handle>);
static_assert(!is_swmr_writer_v<synthetic_writer*>);

static_assert(is_swmr_reader_v<synthetic_reader>);
static_assert(IsSwmrReader<synthetic_reader>);
static_assert(is_swmr_reader_v<synthetic_double_reader>);
static_assert(is_swmr_reader_v<synthetic_reader&>);
static_assert(is_swmr_reader_v<synthetic_reader&&>);
static_assert(is_swmr_reader_v<synthetic_reader const&>);

static_assert(!is_swmr_reader_v<int>);
static_assert(!is_swmr_reader_v<void>);
static_assert(!is_swmr_reader_v<synthetic_writer>);
static_assert(!is_swmr_reader_v<synthetic_swmr_hybrid>);
static_assert(!is_swmr_reader_v<synthetic_non_const_load>);
static_assert(!is_swmr_reader_v<synthetic_void_load>);
static_assert(!is_swmr_reader_v<synthetic_producer_handle>);
static_assert(!is_swmr_reader_v<synthetic_consumer_handle>);
static_assert(!is_swmr_reader_v<synthetic_reader*>);

static_assert(std::is_same_v<swmr_writer_value_t<synthetic_writer>, int>);
static_assert(std::is_same_v<swmr_writer_value_t<synthetic_double_writer>, double>);
static_assert(std::is_same_v<swmr_writer_value_t<synthetic_writer const&>, int>);

static_assert(std::is_same_v<swmr_reader_value_t<synthetic_reader>, int>);
static_assert(std::is_same_v<swmr_reader_value_t<synthetic_double_reader>, double>);
static_assert(std::is_same_v<swmr_reader_value_t<synthetic_reader const&>, int>);

}  // namespace detail::is_swmr_handle_self_test

inline bool is_swmr_handle_smoke_test() noexcept {
    using namespace detail::is_swmr_handle_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_swmr_writer_v<synthetic_writer>;
        ok = ok && IsSwmrWriter<synthetic_writer&&>;
        ok = ok && is_swmr_reader_v<synthetic_reader>;
        ok = ok && IsSwmrReader<synthetic_reader&&>;
        ok = ok && !is_swmr_writer_v<synthetic_reader>;
        ok = ok && !is_swmr_reader_v<synthetic_writer>;
        ok = ok && !is_swmr_writer_v<synthetic_swmr_hybrid>;
        ok = ok && !is_swmr_reader_v<synthetic_swmr_hybrid>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
