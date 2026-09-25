#pragma once

// Detection goes through a derived-to-base pointer conversion, which only
// succeeds for public inheritance. A handle that inherited the base privately
// would read as not a session handle at all.

#include <crucible/sessions/Session.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace crucible::safety::extract {

namespace detail {

// Neither overload is ever called. Only the return type that overload
// resolution picks is read, so no definition is needed for either one.
template <typename Proto, typename Derived>
::crucible::safety::proto::SessionHandleBase<Proto, Derived>*
to_session_base(::crucible::safety::proto::SessionHandleBase<Proto, Derived>*) noexcept;

void* to_session_base(...) noexcept;

template <typename T>
using session_base_probe_t = decltype(detail::to_session_base(std::declval<T*>()));

template <typename T>
struct is_session_handle_impl {
    static constexpr bool value = !std::is_same_v<session_base_probe_t<T>, void*>;
};

template <typename M>
struct session_base_decomp {
    using proto = void;
    using derived = void;
    static constexpr bool matches = false;
};

template <typename Proto, typename Derived>
struct session_base_decomp<::crucible::safety::proto::SessionHandleBase<Proto, Derived>*> {
    using proto = Proto;
    using derived = Derived;
    static constexpr bool matches = true;
};

}  // namespace detail

template <typename T>
inline constexpr bool is_session_handle_v = detail::is_session_handle_impl<std::remove_cvref_t<T>>::value;

template <typename T>
concept IsSessionHandle = is_session_handle_v<T>;

template <typename T>
    requires is_session_handle_v<T>
using session_handle_proto_t =
    typename detail::session_base_decomp<detail::session_base_probe_t<std::remove_cvref_t<T>>>::proto;

// Only the negative side is checked here. Building a real handle would pull a
// large protocol surface into every consumer of this header, so the positive
// matrix lives in a separate translation unit.

namespace detail::is_session_handle_self_test {

static_assert(!is_session_handle_v<int>);
static_assert(!is_session_handle_v<int*>);
static_assert(!is_session_handle_v<void>);
static_assert(!is_session_handle_v<char>);

struct foreign_type {
    int x;
};
static_assert(!is_session_handle_v<foreign_type>);

struct foreign_with_unrelated_base {
    int y;
};
static_assert(!is_session_handle_v<foreign_with_unrelated_base*>);

static_assert(!IsSessionHandle<int>);
static_assert(!IsSessionHandle<foreign_type>);

}  // namespace detail::is_session_handle_self_test

// The volatile bound defeats constant folding, so the predicate and the
// concept are evaluated outside a constant expression as well.
inline bool is_session_handle_smoke_test() noexcept {
    using namespace detail::is_session_handle_self_test;

    volatile std::size_t const cap = 4;
    bool ok = true;
    for (std::size_t i = 0; i < cap; ++i) {
        ok = ok && !is_session_handle_v<int>;
        ok = ok && !is_session_handle_v<void>;
        ok = ok && !is_session_handle_v<foreign_type>;
        ok = ok && !IsSessionHandle<foreign_type>;
    }
    return ok;
}

}  // namespace crucible::safety::extract
