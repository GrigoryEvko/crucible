// The vendor value of VendorPinned is invariant.  A Portable protocol
// does not stand for an NV-pinned one, because the dual of a pinned
// endpoint is pinned to the same vendor, and an order on the value would
// not be closed under duality.

#include <fixy/session/Subtype.h>

namespace {

namespace s = ::fixy::session;

using Portable = s::VendorPinned<s::VendorBackend::Portable, s::Send<int, s::End>>;
using Nv = s::VendorPinned<s::VendorBackend::NV, s::Send<int, s::End>>;

}  // namespace

int main() {
    s::assert_subtype_sync<Portable, Nv>();
    return 0;
}
