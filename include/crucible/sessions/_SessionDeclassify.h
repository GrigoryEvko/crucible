#pragma once

// The policy tag lives in the protocol type instead of at each send site. A
// search for the wrapper name then finds every wire-classification point, and
// the tag states why the value leaves classification there.
//
// The framework never calls declassify_for_wire. The transport callback calls
// it, so every extraction of wire bytes stays explicit at a named site.
//
// Recv does not reclassify. The recipient decides whether to wrap the received
// value again.

#include <crucible/Platform.h>
#include <crucible/safety/_Secret.h>
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionSubtype.h>

#include <type_traits>
#include <utility>

namespace crucible::safety {

template <typename T, typename Policy>
    requires DeclassificationPolicy<Policy>
class [[nodiscard]] DeclassifyOnSend {
    Secret<T> value_;

public:
    using value_type = T;
    using policy_type = Policy;
    using secret_type = Secret<T>;

    constexpr explicit DeclassifyOnSend(Secret<T> s) noexcept(std::is_nothrow_move_constructible_v<Secret<T>>)
        : value_{std::move(s)} {}

    constexpr explicit DeclassifyOnSend(T raw) noexcept(std::is_nothrow_move_constructible_v<T>)
        : value_{Secret<T>{std::move(raw)}} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit DeclassifyOnSend(std::in_place_t,
                                        Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
        : value_{Secret<T>{std::in_place, std::forward<Args>(args)...}} {}

    DeclassifyOnSend(const DeclassifyOnSend&) =
        delete("DeclassifyOnSend wraps Secret<T>; classified values cannot silently duplicate");
    DeclassifyOnSend& operator=(const DeclassifyOnSend&) =
        delete("DeclassifyOnSend wraps Secret<T>; classified values cannot silently duplicate");
    DeclassifyOnSend(DeclassifyOnSend&&) noexcept = default;
    DeclassifyOnSend& operator=(DeclassifyOnSend&&) noexcept = default;
    ~DeclassifyOnSend() = default;

    [[nodiscard]] constexpr T declassify_for_wire() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return std::move(value_).template declassify<Policy>();
    }

    // The size is not classified content, so it escapes without a policy. A
    // serializer needs it to size the wire buffer before extraction.
    [[nodiscard]] constexpr auto size() const noexcept
        requires requires(const T& t) { t.size(); }
    {
        return value_.size();
    }
};

template <typename T>
struct is_declassify_on_send : std::false_type {};

template <typename T, typename Policy>
struct is_declassify_on_send<DeclassifyOnSend<T, Policy>> : std::true_type {};

template <typename T>
inline constexpr bool is_declassify_on_send_v = is_declassify_on_send<T>::value;

template <typename T>
concept DeclassifyOnSendable = is_declassify_on_send_v<T>;

template <typename T>
struct wire_payload_type {
    using type = T;
};

template <typename T, typename Policy>
struct wire_payload_type<DeclassifyOnSend<T, Policy>> {
    using type = T;
};

template <typename T>
using wire_payload_type_t = typename wire_payload_type<T>::type;

template <typename T>
struct wire_policy;

template <typename T, typename Policy>
struct wire_policy<DeclassifyOnSend<T, Policy>> {
    using type = Policy;
};

template <typename T>
using wire_policy_t = typename wire_policy<T>::type;

namespace detail::declassify_size_test {

struct OneByteToken {
    char x;
};
struct FourByteToken {
    int x;
};

static_assert(sizeof(DeclassifyOnSend<OneByteToken, secret_policy::WireSerialize>) == sizeof(OneByteToken),
              "DeclassifyOnSend must add zero bytes beyond the wrapped Secret<T>.");

static_assert(sizeof(DeclassifyOnSend<FourByteToken, secret_policy::WireSerialize>) == sizeof(FourByteToken),
              "DeclassifyOnSend must add zero bytes beyond the wrapped Secret<T>.");

}  // namespace detail::declassify_size_test

#ifdef CRUCIBLE_SESSION_SELF_TESTS
namespace detail::declassify_self_test {

struct Token {
    int v = 0;
};
struct Other {};

using TokenWire = DeclassifyOnSend<Token, secret_policy::WireSerialize>;
using TokenAudit = DeclassifyOnSend<Token, secret_policy::AuditedLogging>;

static_assert(is_declassify_on_send_v<TokenWire>);
static_assert(is_declassify_on_send_v<TokenAudit>);
static_assert(!is_declassify_on_send_v<Token>);
static_assert(!is_declassify_on_send_v<Secret<Token>>);
static_assert(!is_declassify_on_send_v<int>);

static_assert(DeclassifyOnSendable<TokenWire>);
static_assert(!DeclassifyOnSendable<Token>);

static_assert(std::is_same_v<wire_payload_type_t<TokenWire>, Token>);
static_assert(std::is_same_v<wire_payload_type_t<TokenAudit>, Token>);
static_assert(std::is_same_v<wire_payload_type_t<Token>, Token>);
static_assert(std::is_same_v<wire_payload_type_t<int>, int>);

static_assert(std::is_same_v<wire_policy_t<TokenWire>, secret_policy::WireSerialize>);
static_assert(std::is_same_v<wire_policy_t<TokenAudit>, secret_policy::AuditedLogging>);

static_assert(!std::is_same_v<TokenWire, TokenAudit>);

static_assert(!std::is_copy_constructible_v<TokenWire>);
static_assert(!std::is_copy_assignable_v<TokenWire>);
static_assert(std::is_move_constructible_v<TokenWire>);
static_assert(std::is_move_assignable_v<TokenWire>);

using namespace crucible::safety::proto;

static_assert(is_subtype_sync_v<Send<TokenWire, End>, Send<TokenWire, End>>);

static_assert(!is_subtype_sync_v<Send<TokenWire, End>, Send<TokenAudit, End>>);

// No payload subsort axiom relates the wrapper to the bare payload, in either
// direction, and that absence is deliberate. Stripping the tag at a subtype
// boundary would hide a wire-classification site. Gaining it would let an
// unclassified value pass as a classified one.
static_assert(!is_subtype_sync_v<Send<TokenWire, End>, Send<Token, End>>);

static_assert(!is_subtype_sync_v<Send<Token, End>, Send<TokenWire, End>>);

static_assert(!is_subtype_sync_v<Recv<TokenWire, End>, Recv<Token, End>>);
static_assert(!is_subtype_sync_v<Recv<Token, End>, Recv<TokenWire, End>>);

}  // namespace detail::declassify_self_test
#endif  // CRUCIBLE_SESSION_SELF_TESTS

}  // namespace crucible::safety
