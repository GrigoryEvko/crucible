// The compile-time checks of foundation/core/Id.h.

#include <foundation/core/Id.h>

#include <foundation/core/Scalar.h>

#include <compare>
#include <concepts>
#include <cstdint>
#include <type_traits>

namespace foundation::core::detail::id_checks {

struct SlotTag;
struct OpTag;
using SlotId = Id<SlotTag, std::uint32_t>;
using OpId = Id<OpTag, std::uint32_t>;
using MetaId = Id<struct MetaTag, std::uint64_t>;
using SchemaDigest = Digest<struct SchemaTag>;

// An Id is its word, and the highest word is the niche of its Option.  A
// Digest has no niche, so its Option holds a tag.
static_assert(sizeof(SlotId) == 4 && sizeof(MetaId) == 8 && sizeof(Id<SlotTag, std::uint8_t>) == 1);
static_assert(sizeof(Option<SlotId>) == 4 && sizeof(Option<MetaId>) == 8);
static_assert(sizeof(SchemaDigest) == 8 && sizeof(Option<SchemaDigest>) > 8);
static_assert(std::is_trivially_copyable_v<SlotId> && std::is_trivially_copyable_v<SchemaDigest>);

// No default constructor, no conversion from or to a word, and no compare
// across two tags.
static_assert(!std::is_default_constructible_v<SlotId> && !std::is_default_constructible_v<SchemaDigest>);
static_assert(!std::is_convertible_v<std::uint32_t, SlotId> && std::is_constructible_v<SlotId, std::uint32_t>);
static_assert(!std::is_convertible_v<SlotId, std::uint32_t> && !std::is_convertible_v<SchemaDigest, std::uint64_t>);
static_assert(!std::equality_comparable_with<SlotId, OpId> && !std::equality_comparable_with<SlotId, std::uint32_t>);
static_assert(SlotId{3} == SlotId{3} && std::is_lt(SlotId{3} <=> SlotId{4}) && SlotId{7}.raw() == 7);
static_assert(SchemaDigest{9} == SchemaDigest{9} && std::is_gt(SchemaDigest{9} <=> SchemaDigest{1}));

// No arithmetic: a sum, a difference and an increment do not compile.
template <class T>
inline constexpr bool adds = requires(T value) { value + value; };
template <class T>
inline constexpr bool increments = requires(T value) { ++value; };
static_assert(!adds<SlotId> && !increments<SlotId> && !adds<SchemaDigest> && adds<std::uint32_t>);

// The highest word gives an empty Option through decode, and each other word
// gives the id.  BitValid refuses the niche.
static_assert(decode<SlotId>(std::uint32_t{0xFFFFFFFFu}).is_none());
static_assert(decode<SlotId>(std::uint32_t{0xFFFFFFFEu}).is_some() && decode<SlotId>(std::uint32_t{0}).is_some());
static_assert(decode<SchemaDigest>(std::uint64_t{0xFFFFFFFFFFFFFFFFu}).is_some());
static_assert(!BitValid<SlotId> && BitValid<SchemaDigest>);
static_assert(Option<SlotId>::some(SlotId{5}).is_some() && Option<SlotId>{none}.is_none());

}  // namespace foundation::core::detail::id_checks
