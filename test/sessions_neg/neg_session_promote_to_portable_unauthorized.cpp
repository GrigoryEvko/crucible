// GAPS-068 fixture #6: a vendor-specific session cannot be promoted to
// a Portable-required consumer without an explicit portable provider.

#include <crucible/sessions/PermissionedSession.h>

namespace proto = ::crucible::safety::proto;

struct Wire {};

using NvHandle = proto::PermissionedSessionHandle<
    proto::End,
    proto::EmptyPermSet,
    Wire,
    proto::VendorCtx<proto::VendorBackend::NV>>;

using PortableRequired = proto::PermissionedSessionHandle<
    proto::End,
    proto::EmptyPermSet,
    Wire,
    proto::VendorCtx<proto::VendorBackend::Portable>>;

int main() {
    proto::assert_permissioned_session_vendor_compatible<NvHandle,
                                                         PortableRequired>();
    return 0;
}
