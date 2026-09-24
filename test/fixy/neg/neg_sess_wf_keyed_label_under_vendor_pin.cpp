// A label branch of a keyed Offer starts with a VendorPinned.  The label
// word is the whole message of the branch, so the handle enters the
// branch past its label step, and the pin above that step has no place
// on the wire.  The gate refuses the branch.

#include <fixy/session/Projection.h>
#include <fixy/session/Protocol.h>

namespace s = fixy::session;

// These types have external linkage.  The session folds their stable
// ids, and a stable id refuses a type with internal linkage.
namespace neg_sess_wf_keyed_label_under_vendor_pin_types {
struct Bob {};
struct Hello {};
struct Bye {};
}  // namespace neg_sess_wf_keyed_label_under_vendor_pin_types

using namespace neg_sess_wf_keyed_label_under_vendor_pin_types;

using PinnedFirst = s::Offer<s::Sender<Bob>, s::VendorPinned<s::VendorBackend::NV, s::Recv<s::PeerMsg<Bob, Hello, int>, s::End>>,
                             s::Recv<s::PeerMsg<Bob, Bye, int>, s::End>>;

int main() {
    s::ensure_choices_well_formed<PinnedFirst>();
    return 0;
}
