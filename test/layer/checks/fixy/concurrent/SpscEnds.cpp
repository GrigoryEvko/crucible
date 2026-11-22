// The compile-time checks of fixy/concurrent/SpscEnds.h.

#include <fixy/concurrent/SpscEnds.h>

#include <meta>
#include <type_traits>

namespace {

namespace cc = ::fixy::concurrent;

// An owner with two empty states, pinned as each owner of a split is.
struct CheckOwner final {
    struct producer_state {};
    struct consumer_state {};
    CheckOwner() noexcept = default;
    CheckOwner(CheckOwner const&) = delete("pinned");
    CheckOwner(CheckOwner&&) = delete("pinned");
};

struct CheckBrand final {};

using CheckSplit = cc::SpscSplit<CheckOwner, CheckBrand>;
using CheckRef = ::foundation::core::Ref<CheckOwner, cc::SpscSplitBrand<CheckOwner>>;

// THE SPLIT holds the Box first and the two ends after it, so the ends end
// before the owner that their Ref names.  The order is in the type, and no
// holder of a split can change it.
[[nodiscard]] consteval bool holds_the_box_before_the_ends() {
    auto const members = std::meta::nonstatic_data_members_of(^^CheckSplit, std::meta::access_context::unchecked());
    return members.size() == 3
        && std::meta::dealias(std::meta::type_of(members[0]))
               == std::meta::dealias(^^::foundation::core::Box<CheckOwner>)
        && std::meta::dealias(std::meta::type_of(members[1])) == std::meta::dealias(^^cc::ProducerEnd<CheckOwner>)
        && std::meta::dealias(std::meta::type_of(members[2])) == std::meta::dealias(^^cc::ConsumerEnd<CheckOwner>);
}
static_assert(holds_the_box_before_the_ends());

// A split does not copy and does not move, so its Box stays in it.
static_assert(!std::is_copy_constructible_v<CheckSplit> && !std::is_move_constructible_v<CheckSplit>);
static_assert(!std::is_copy_assignable_v<CheckSplit> && !std::is_move_assignable_v<CheckSplit>);

// An end does not copy and does not move, so no end leaves its split.
static_assert(!std::is_copy_constructible_v<cc::ProducerEnd<CheckOwner>>);
static_assert(!std::is_move_constructible_v<cc::ProducerEnd<CheckOwner>>);
static_assert(!std::is_move_assignable_v<cc::ProducerEnd<CheckOwner>>);
static_assert(!std::is_move_constructible_v<cc::ConsumerEnd<CheckOwner>>);

// No code outside a split builds an end or the key of an end.
static_assert(!std::is_constructible_v<cc::ProducerEnd<CheckOwner>, CheckRef>);
static_assert(!std::is_constructible_v<cc::ProducerEnd<CheckOwner>, cc::SpscEndKey<CheckOwner> const&, CheckRef>);
static_assert(!std::is_default_constructible_v<cc::SpscEndKey<CheckOwner>>);
static_assert(!std::is_copy_constructible_v<cc::SpscEndKey<CheckOwner>>);

// The key of each side is empty, and the two sides have two key types.
static_assert(std::is_empty_v<cc::ProducerKey<CheckBrand>> && std::is_empty_v<cc::ConsumerKey<CheckBrand>>);
static_assert(!std::is_same_v<cc::ProducerKey<CheckBrand>, cc::ConsumerKey<CheckBrand>>);
static_assert(!std::is_copy_constructible_v<cc::ProducerKey<CheckBrand>>);

}  // namespace
