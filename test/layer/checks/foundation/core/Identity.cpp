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

// An Option of an identity keeps the plain slot: its copy, move and end are
// those of its bytes, so the ABI gives it back in two registers.  The seal
// of the identity seals the Option.
static_assert(detail::TrivialCopyShape<Option<Identity>> && std::is_nothrow_move_assignable_v<Option<Identity>>);
static_assert(sizeof(Option<Identity>) == 16 && !std::is_trivially_copyable_v<Option<Identity>>);

// An identity compares for equality and has no order.
static_assert(std::equality_comparable<Identity>);
static_assert(!std::totally_ordered<Identity>);

// The mint gives an identity and throws nothing.
static_assert(std::is_same_v<decltype(mint_identity()), Identity>);
static_assert(noexcept(mint_identity()));

// The right of one holder is one word.  It does not copy, it moves, and it has
// no trivial constructor, so no byte route builds one.  It comes only from an
// Identity, and it compares with no right.
static_assert(sizeof(UniqueIdentity) == sizeof(std::uint64_t));
static_assert(!std::is_default_constructible_v<UniqueIdentity> && !std::is_copy_constructible_v<UniqueIdentity>);
static_assert(!std::is_copy_assignable_v<UniqueIdentity>);
static_assert(std::is_nothrow_move_constructible_v<UniqueIdentity>
              && std::is_nothrow_move_assignable_v<UniqueIdentity>);
static_assert(!std::is_trivially_move_constructible_v<UniqueIdentity> && !std::is_trivially_copyable_v<UniqueIdentity>);
static_assert(std::is_constructible_v<UniqueIdentity, Identity const&>);
static_assert(!std::is_constructible_v<UniqueIdentity, std::uint64_t>);
static_assert(!std::is_convertible_v<Identity, UniqueIdentity> && !std::equality_comparable<UniqueIdentity>);

// The word of an emptied right is 0, and the mints start above it.
static_assert(detail::first_identity_word != 0);

// Identity is a type of the Ref family (plan section 11.10).  Its only public
// members are language protocols.
inline constexpr TextView identity_names{"Identity mint_identity"};
CRUCIBLE_CLOSED_SURFACE(identity_names, Identity);
inline constexpr TextView unique_identity_names{"UniqueIdentity names is_held pairs_with"};
CRUCIBLE_CLOSED_SURFACE(unique_identity_names, UniqueIdentity);

}  // namespace detail::identity_checks

}  // namespace foundation::core
