#pragma once

#include <crucible/safety/EpochVersioned.h>

#include <type_traits>

namespace crucible::safety::extract {

namespace detail {

template <typename T>
struct is_epoch_versioned_impl : std::false_type {
    using value_type = void;
};

template <typename U>
struct is_epoch_versioned_impl<::crucible::safety::EpochVersioned<U>> : std::true_type {
    using value_type = U;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_epoch_versioned_v = detail::is_epoch_versioned_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsEpochVersioned = is_epoch_versioned_v<T>;

template <typename T>
    requires is_epoch_versioned_v<T>
using epoch_versioned_value_t = typename detail::is_epoch_versioned_impl<std::remove_cvref_t<T>>::value_type;

namespace detail::is_epoch_versioned_self_test {

using EV_int = ::crucible::safety::EpochVersioned<int>;
using EV_double = ::crucible::safety::EpochVersioned<double>;
using EV_char = ::crucible::safety::EpochVersioned<char>;
using EV_uint64 = ::crucible::safety::EpochVersioned<std::uint64_t>;

static_assert(is_epoch_versioned_v<EV_int>);
static_assert(is_epoch_versioned_v<EV_double>);
static_assert(is_epoch_versioned_v<EV_char>);
static_assert(is_epoch_versioned_v<EV_uint64>);

static_assert(is_epoch_versioned_v<EV_int&>);
static_assert(is_epoch_versioned_v<EV_int const&>);

static_assert(!is_epoch_versioned_v<int>);
static_assert(!is_epoch_versioned_v<int*>);
static_assert(!is_epoch_versioned_v<void>);

struct LookalikeEpochVersioned {
    int value;
    std::uint64_t epoch_field;
    std::uint64_t generation_field;
};
static_assert(!is_epoch_versioned_v<LookalikeEpochVersioned>);

static_assert(!is_epoch_versioned_v<EV_int*>);

static_assert(IsEpochVersioned<EV_int>);
static_assert(!IsEpochVersioned<int>);

static_assert(std::is_same_v<epoch_versioned_value_t<EV_int>, int>);
static_assert(std::is_same_v<epoch_versioned_value_t<EV_double>, double>);
static_assert(std::is_same_v<epoch_versioned_value_t<EV_uint64>, std::uint64_t>);

static_assert(sizeof(EV_int) >= sizeof(int) + 16);
static_assert(sizeof(EV_double) >= sizeof(double) + 16);
static_assert(sizeof(EV_uint64) == 24);

}  // namespace detail::is_epoch_versioned_self_test

inline bool is_epoch_versioned_smoke_test() noexcept {
    using namespace detail::is_epoch_versioned_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && is_epoch_versioned_v<EV_int>;
        ok = ok && is_epoch_versioned_v<EV_double>;
        ok = ok && !is_epoch_versioned_v<int>;
        ok = ok && IsEpochVersioned<EV_int&&>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
