#pragma once

// The wrapper holds a raw T rather than a Secret<T>. The wrapper itself is the
// discipline marker, and a caller who also wants declassification wraps the two
// the other way round.

#include <crucible/Platform.h>
#include <crucible/safety/_ConstantTime.h>
#include <crucible/safety/_Secret.h>  // for DeclassificationPolicy concept
#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionSubtype.h>

#include <cstddef>
#include <span>
#include <type_traits>
#include <utility>

namespace crucible::safety::ct {

template <typename T>
struct requires_ct : std::false_type {};

template <typename T>
inline constexpr bool requires_ct_v = requires_ct<T>::value;

// The trivial-copyability half is what makes the byte view of the object
// representation well defined.

template <typename T>
concept RequiresCT = requires_ct_v<T> && std::is_trivially_copyable_v<T>;

template <typename T>
    requires RequiresCT<T>
class [[nodiscard]] CTPayload {
    T value_;

public:
    using value_type = T;

    constexpr explicit CTPayload(T v) noexcept : value_{std::move(v)} {}

    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit CTPayload(std::in_place_t, Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
        : value_{std::forward<Args>(args)...} {}

    CTPayload(const CTPayload&) =
        delete("CTPayload wraps CT-required data; classified values cannot silently duplicate");
    CTPayload& operator=(const CTPayload&) =
        delete("CTPayload wraps CT-required data; classified values cannot silently duplicate");
    CTPayload(CTPayload&&) noexcept = default;
    CTPayload& operator=(CTPayload&&) noexcept = default;
    ~CTPayload() = default;

    bool operator==(const CTPayload&) const = delete(
        "CTPayload comparison must use crucible::safety::ct::eq() to avoid timing side-channel; operator== is deleted to enforce this at compile time");
    bool operator!=(const CTPayload&) const = delete(
        "CTPayload comparison must use crucible::safety::ct::eq() to avoid timing side-channel; operator!= is deleted to enforce this at compile time");

    // This is the only non-consuming read, and there is deliberately no element
    // accessor. Reading one element at a time invites content-dependent
    // branching, which is what the wrapper exists to prevent.
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return std::as_bytes(std::span<const T, 1>{&value_, 1});
    }

    // Policy is not read by the body. It is there so the call site has to name
    // one, which is what makes the extraction auditable.
    template <DeclassificationPolicy Policy>
    [[nodiscard]] constexpr T declassify_ct() && noexcept {
        return std::move(value_);
    }
};

// The comparison time depends only on the size of T, not on the content and not
// on where the two operands first differ.

template <typename T>
    requires RequiresCT<T>
[[nodiscard]] bool eq(CTPayload<T> const& a, CTPayload<T> const& b) noexcept {
    return crucible::safety::ct::eq(a.bytes(), b.bytes());
}

template <typename T>
struct is_ct_payload : std::false_type {};

template <typename T>
struct is_ct_payload<CTPayload<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_ct_payload_v = is_ct_payload<T>::value;

template <typename T>
concept CTPayloadType = is_ct_payload_v<T>;

template <typename T>
struct ct_payload_value_type {
    using type = T;
};

template <typename T>
struct ct_payload_value_type<CTPayload<T>> {
    using type = T;
};

template <typename T>
using ct_payload_value_type_t = typename ct_payload_value_type<T>::type;

#ifdef CRUCIBLE_SESSION_SELF_TESTS
namespace detail::ct_self_test {

struct TestTag {
    std::byte bytes[16]{};
};

}  // namespace detail::ct_self_test

template <>
struct requires_ct<detail::ct_self_test::TestTag> : std::true_type {};

namespace detail::ct_self_test {

static_assert(requires_ct_v<TestTag>);
static_assert(RequiresCT<TestTag>);

static_assert(!requires_ct_v<int>);
static_assert(!RequiresCT<int>);

using TagPayload = CTPayload<TestTag>;

static_assert(is_ct_payload_v<TagPayload>);
static_assert(!is_ct_payload_v<TestTag>);
static_assert(!is_ct_payload_v<int>);

static_assert(CTPayloadType<TagPayload>);
static_assert(!CTPayloadType<TestTag>);

static_assert(std::is_same_v<ct_payload_value_type_t<TagPayload>, TestTag>);
static_assert(std::is_same_v<ct_payload_value_type_t<int>, int>);

static_assert(!std::is_copy_constructible_v<TagPayload>);
static_assert(std::is_move_constructible_v<TagPayload>);

static_assert(sizeof(TagPayload) == sizeof(TestTag));

template <typename A, typename B>
concept has_operator_eq = requires(A a, B b) { a == b; };

static_assert(!has_operator_eq<TagPayload const&, TagPayload const&>);
static_assert(!has_operator_eq<TagPayload, TagPayload>);

using namespace crucible::safety::proto;

static_assert(is_subtype_sync_v<Send<TagPayload, End>, Send<TagPayload, End>>);

// No payload subsort axiom relates the wrapper to the bare payload, in either
// direction, and that absence is deliberate. A protocol that could shed the
// wrapper at a subtype boundary would hide a comparison that must stay constant
// time.
static_assert(!is_subtype_sync_v<Send<TagPayload, End>, Send<TestTag, End>>);

static_assert(!is_subtype_sync_v<Send<TestTag, End>, Send<TagPayload, End>>);

static_assert(!is_subtype_sync_v<Recv<TagPayload, End>, Recv<TestTag, End>>);

static_assert(!is_subtype_sync_v<Recv<TestTag, End>, Recv<TagPayload, End>>);

}  // namespace detail::ct_self_test
#endif  // CRUCIBLE_SESSION_SELF_TESTS

}  // namespace crucible::safety::ct
