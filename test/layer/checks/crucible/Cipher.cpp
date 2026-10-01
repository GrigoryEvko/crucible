// The compile-time checks of crucible/Cipher.h.

#include <crucible/Cipher.h>

namespace crucible {

static_assert(::fixy::no_scoped_view_field_check<Cipher>());
static_assert(sizeof(Cipher::ContentAddressedRegionPayload) == sizeof(const RegionNode*));
static_assert(::fixy::session::is_content_addressed_v<typename Cipher::ContentAddressedRegionPayload::payload_type>);
static_assert(
    ::fixy::session::is_content_addressed_v<typename Cipher::LoadedContentAddressedRegionPayload::payload_type>);

}  // namespace crucible
