// The compile-time checks of crucible/cipher/CipherTierPromotion.h.

#include <crucible/cipher/CipherTierPromotion.h>

namespace crucible::cipher {

namespace detail::cipher_tier_promotion_self_test {

using HotHash = HotTierHandle<ContentHash>;
using WarmHash = WarmTierHandle<ContentHash>;
using ColdHash = ColdTierHandle<ContentHash>;

static_assert(can_promote_tier_v<CipherTierTag_v::Cold, CipherTierTag_v::Warm>);
static_assert(can_promote_tier_v<CipherTierTag_v::Cold, CipherTierTag_v::Hot>);
static_assert(can_promote_tier_v<CipherTierTag_v::Warm, CipherTierTag_v::Hot>);
static_assert(!can_promote_tier_v<CipherTierTag_v::Hot, CipherTierTag_v::Cold>);

static_assert(can_demote_tier_v<CipherTierTag_v::Hot, CipherTierTag_v::Warm>);
static_assert(can_demote_tier_v<CipherTierTag_v::Hot, CipherTierTag_v::Cold>);
static_assert(can_demote_tier_v<CipherTierTag_v::Warm, CipherTierTag_v::Cold>);
static_assert(!can_demote_tier_v<CipherTierTag_v::Cold, CipherTierTag_v::Hot>);

static_assert(std::is_same_v<decltype(mint_promote<CipherTierTag_v::Cold, CipherTierTag_v::Warm>(
                                 ::fixy::mint_band<ColdHash>(ContentHash{1}))),
                             WarmHash>);
static_assert(std::is_same_v<decltype(mint_demote<CipherTierTag_v::Hot, CipherTierTag_v::Cold>(
                                 ::fixy::mint_band<HotHash>(ContentHash{2}))),
                             ColdHash>);
static_assert(
    mint_promote<CipherTierTag_v::Cold, CipherTierTag_v::Hot>(::fixy::mint_band<ColdHash>(ContentHash{3})).peek()
    == ContentHash{3});

static_assert(::foundation::reflect::enum_name(RestoreError::ContentHashMismatch) == "ContentHashMismatch");

using HotPromoteHash = HotPromote<ContentHash>;
using HotPromoteCarrier = HotPromoteDelegate<ContentHash>;
using HotPromotePeer = HotPromoteAccept<ContentHash>;

static_assert(::fixy::session::is_well_formed_v<HotPromoteHash>);
static_assert(::fixy::session::DelegatesTo<HotPromoteCarrier, HotPromoteHash>);
static_assert(::fixy::session::AcceptsFrom<HotPromotePeer, HotPromoteHash>);
static_assert(std::is_same_v<::fixy::session::dual_of_t<HotPromoteCarrier>, HotPromotePeer>);

}  // namespace detail::cipher_tier_promotion_self_test

}  // namespace crucible::cipher
