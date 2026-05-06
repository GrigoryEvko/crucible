// GAPS-067: VendorCtx pins PermissionedSessionHandle chains to a
// VendorLattice backend.  NV and AMD are mutually incomparable, so an
// NV provider cannot satisfy an AMD consumer.

#include <crucible/sessions/PermissionedSession.h>

namespace proto = ::crucible::safety::proto;

struct Channel {
    int value = 0;
};

using NvHandle = proto::PermissionedSessionHandle<
    proto::End,
    proto::EmptyPermSet,
    Channel,
    proto::VendorCtx<proto::VendorBackend::NV>>;

using AmdHandle = proto::PermissionedSessionHandle<
    proto::End,
    proto::EmptyPermSet,
    Channel,
    proto::VendorCtx<proto::VendorBackend::AMD>>;

int main() {
    proto::assert_permissioned_session_vendor_compatible<NvHandle, AmdHandle>();
    return 0;
}
