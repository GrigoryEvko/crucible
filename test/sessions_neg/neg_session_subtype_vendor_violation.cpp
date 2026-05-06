// GAPS-068 fixture #4: protocol-level subtyping rejects incomparable
// vendor pins.

#include <crucible/sessions/SessionSubtype.h>

namespace proto = ::crucible::safety::proto;

using NvProto = proto::VendorPinned<
    proto::VendorBackend::NV,
    proto::Send<int, proto::End>>;

using AmdProto = proto::VendorPinned<
    proto::VendorBackend::AMD,
    proto::Send<int, proto::End>>;

int main() {
    proto::assert_vendor_subtype_sync<NvProto, AmdProto>();
    return 0;
}
