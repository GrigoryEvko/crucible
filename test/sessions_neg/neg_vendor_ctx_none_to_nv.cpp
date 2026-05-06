// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-067 fixture #2: VendorCtx<None> is an uninitialized sentinel, not a
// weak provider. The PSH vendor-composition gate must reject it before any
// lattice bottom rule can make it satisfy an NV consumer.

#include <crucible/sessions/PermissionedSession.h>

namespace proto = ::crucible::safety::proto;

struct Channel {
    int value = 0;
};

using NoneHandle = proto::PermissionedSessionHandle<
    proto::End,
    proto::EmptyPermSet,
    Channel,
    proto::VendorCtx<proto::VendorBackend::None>>;

using NvHandle = proto::PermissionedSessionHandle<
    proto::End,
    proto::EmptyPermSet,
    Channel,
    proto::VendorCtx<proto::VendorBackend::NV>>;

int main() {
    proto::assert_permissioned_session_vendor_compatible<NoneHandle, NvHandle>();
    return 0;
}
