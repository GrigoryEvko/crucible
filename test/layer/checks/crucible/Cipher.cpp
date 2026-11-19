// The compile-time checks of crucible/Cipher.h.

#include <crucible/Cipher.h>

namespace crucible {

static_assert(::fixy::no_scoped_view_field_check<Cipher>());
static_assert(sizeof(Cipher::ContentAddressedRegionPayload) == sizeof(const RegionNode*));
static_assert(::fixy::session::ContentAddressedType<typename Cipher::ContentAddressedRegionPayload::payload_type>);
static_assert(
    ::fixy::session::ContentAddressedType<typename Cipher::LoadedContentAddressedRegionPayload::payload_type>);

}  // namespace crucible
