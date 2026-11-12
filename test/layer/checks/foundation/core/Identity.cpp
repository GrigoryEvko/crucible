// The compile-time checks of foundation/core/Identity.h.

#include <foundation/core/Identity.h>

#include <foundation/core/Text.h>
#include <foundation/core/Verbs.h>

#include <concepts>
#include <cstdint>
#include <type_traits>

namespace foundation::core {

namespace detail::identity_checks {

// One word, and no identity from an integer or with no mint.
static_assert(sizeof(Identity) == sizeof(std::uint64_t));
static_assert(!std::is_default_constructible_v<Identity>);
static_assert(!std::is_constructible_v<Identity, std::uint64_t>);
static_assert(!std::is_convertible_v<std::uint64_t, Identity>);

// An identity copies, and the seal makes it not trivially copyable, so
// std::bit_cast builds no identity from the bytes of an integer.
static_assert(std::is_trivially_copy_constructible_v<Identity>);
static_assert(std::is_copy_assignable_v<Identity>);
static_assert(!std::is_trivially_copyable_v<Identity>);

// An identity compares for equality and has no order.
static_assert(std::equality_comparable<Identity>);
static_assert(!std::totally_ordered<Identity>);

// The mint gives an identity and throws nothing.
static_assert(std::is_same_v<decltype(mint_identity()), Identity>);
static_assert(noexcept(mint_identity()));

// The counter is one atomic word.
static_assert(std::is_same_v<decltype(detail::next_identity_), Atomic<std::uint64_t>>);

// Identity is a type of the Ref family (plan section 11.10).  Its only public
// members are language protocols.
inline constexpr TextView identity_names{"Identity mint_identity"};
CRUCIBLE_CLOSED_SURFACE(identity_names, Identity);

}  // namespace detail::identity_checks

}  // namespace foundation::core
