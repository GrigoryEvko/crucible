// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// offer_sender is an alias template that reads the sender that an Offer
// names, so no user specialization changes the role that the crash
// transport watches.
//
// Expected diagnostic: a specialization of an alias template.
#include <fixy/session/Protocol.h>

namespace neg_sess_trait_spec_offer_sender_types {
struct Mallory {};
}  // namespace neg_sess_trait_spec_offer_sender_types

namespace fixy::session {
template <>
struct offer_sender<Offer<Recv<int, End>>> {
    using type = neg_sess_trait_spec_offer_sender_types::Mallory;
};
}  // namespace fixy::session

int main() { return 0; }
