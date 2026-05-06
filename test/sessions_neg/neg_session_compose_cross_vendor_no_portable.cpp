// GAPS-068 fixture #5: composing two permissioned sessions with
// distinct vendor pins is rejected unless one side is genuinely
// Portable in the provider position.

#include <crucible/sessions/PermissionedSession.h>

namespace proto = ::crucible::safety::proto;

struct Wire {};

using NvHandle = proto::PermissionedSessionHandle<
    proto::Send<int, proto::End>,
    proto::EmptyPermSet,
    Wire,
    proto::VendorCtx<proto::VendorBackend::NV>>;

using AmdHandle = proto::PermissionedSessionHandle<
    proto::Send<int, proto::End>,
    proto::EmptyPermSet,
    Wire,
    proto::VendorCtx<proto::VendorBackend::AMD>>;

int main() {
    proto::assert_permissioned_session_vendor_compatible<NvHandle, AmdHandle>();
    return 0;
}
