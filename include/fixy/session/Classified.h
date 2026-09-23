#pragma once

// Classified values on a session channel.
//
// A value that goes onto a channel leaves the endpoint that holds it.
// For a classified value that is a declassification, and a
// declassification needs a named policy.  For a value whose comparison
// must take constant time, the channel must carry it in a form that
// offers no comparison except the constant-time one.  This header gives
// the two carriers, and the payload walk of fixy/session/Payload.h
// refuses each value that travels without its carrier:
//
//   DeclassifyOnSend<T, Policy>   a fixy::Secret<T> that leaves
//                                 classification under Policy, at the
//                                 transport, and nowhere else
//   CTPayload<T>                  a value marked constant_time_value,
//                                 with no == and no element access
//
// A type is marked on its own declaration, with an annotation:
//
//   struct [[=fixy::session::constant_time_value{}]] AuthTag {
//       std::byte bytes[16];
//   };
//
// The mark is on the declaration, so it cannot arrive after the first
// check, and it cannot be separated from the type.  A trait
// specialization or a roster entry could do both.
//
// Old spellings: include/crucible/sessions/SessionDeclassify.h and
// include/crucible/sessions/SessionCT.h, which accepted any policy that
// derived from the policy base.  Here the policy must be admitted, as
// Secret::declassify requires.

#include <fixy/ConstantTime.h>
#include <fixy/Secret.h>

#include <cstddef>
#include <meta>
#include <span>
#include <type_traits>
#include <utility>

namespace fixy::session {

// The annotation that marks a constant-time value.
struct constant_time_value {};

namespace detail {
[[nodiscard]] consteval bool carries_constant_time_mark(std::meta::info type) {
    const std::meta::info bare = std::meta::dealias(type);
    if (!std::meta::is_class_type(bare) || !std::meta::is_complete_type(bare)) return false;
    return !std::meta::annotations_of_with_type(bare, ^^constant_time_value).empty();
}
}  // namespace detail

// True when T is a class whose declaration carries the mark.
template <class T>
concept ConstantTimeValue = detail::carries_constant_time_mark(^^T);

// A classified value that leaves classification at the transport.  The
// policy is part of the type, so a search for the carrier finds every
// point where a value leaves classification on a channel, and the policy
// says why.  The framework never calls declassify_for_wire.  The
// transport calls it, at one named site.
template <class T, AdmittedDeclassification Policy>
class [[nodiscard]] DeclassifyOnSend {
public:
    using value_type = T;
    using policy_type = Policy;

    constexpr explicit DeclassifyOnSend(Secret<T> secret) noexcept(std::is_nothrow_move_constructible_v<Secret<T>>)
        : secret_{std::move(secret)} {}

    DeclassifyOnSend(const DeclassifyOnSend&) =
        delete("DeclassifyOnSend holds a classified value. A copy is a second copy of the secret");
    DeclassifyOnSend& operator=(const DeclassifyOnSend&) =
        delete("DeclassifyOnSend holds a classified value. A copy is a second copy of the secret");
    constexpr DeclassifyOnSend(DeclassifyOnSend&&) noexcept(std::is_nothrow_move_constructible_v<Secret<T>>) = default;
    constexpr DeclassifyOnSend& operator=(DeclassifyOnSend&&) noexcept(
        std::is_nothrow_move_assignable_v<Secret<T>>) = default;
    ~DeclassifyOnSend() = default;

    [[nodiscard]] constexpr T declassify_for_wire() && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return std::move(secret_).template declassify<Policy>();
    }

private:
    Secret<T> secret_;
};

// A constant-time value on a channel.  There is no == and no element
// access: an element read invites a branch on the content, which the
// carrier exists to prevent.  The only comparison is eq below, whose
// time depends only on the size of T.
template <class T>
    requires(ConstantTimeValue<T> && std::is_trivially_copyable_v<T>)
class [[nodiscard]] CTPayload {
public:
    using value_type = T;

    constexpr explicit CTPayload(T value) noexcept : value_{value} {}

    CTPayload(const CTPayload&) = delete("CTPayload holds a constant-time value. A copy is a second copy of it");
    CTPayload& operator=(const CTPayload&) =
        delete("CTPayload holds a constant-time value. A copy is a second copy of it");
    constexpr CTPayload(CTPayload&&) noexcept = default;
    constexpr CTPayload& operator=(CTPayload&&) noexcept = default;
    ~CTPayload() = default;

    bool operator==(const CTPayload&) const =
        delete("a CTPayload compares only through fixy::session::eq, whose time does not depend on the content");

    // The object representation, for the constant-time comparison.  This
    // is the only read that does not consume the carrier.
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return std::as_bytes(std::span<const T, 1>{&value_, 1});
    }

    // The value leaves the carrier under an admitted policy, at a named
    // site.
    template <AdmittedDeclassification Policy>
    [[nodiscard]] constexpr T declassify_ct() && noexcept {
        return value_;
    }

private:
    T value_;
};

// Equality whose time depends only on the size of T.
template <class T>
[[nodiscard]] bool eq(const CTPayload<T>& lhs, const CTPayload<T>& rhs) noexcept {
    return ::fixy::ct::eq(lhs.bytes(), rhs.bytes());
}

}  // namespace fixy::session
