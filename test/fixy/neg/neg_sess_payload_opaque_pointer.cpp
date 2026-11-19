// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A payload that holds a pointer to void can point at a token or at a
// session endpoint, and the payload walk of fixy/session/Classification.h
// cannot read what it points at.  So the walk fails closed, also for a
// plain session: the permission delta of the payload, which each send and
// each mint reads, refuses it unless the class that holds the pointer
// states that it is data with a payload rule of its header.
//
// Expected diagnostic: the payload is refused, because it holds a pointer
// to void.

#include <fixy/Tagged.h>
#include <fixy/session/Flow.h>

#include <cstddef>

namespace neg_sess_payload_opaque_pointer_types {

// The address is a pointer to void from outside, in the Tagged of the base.
struct Buffer {
    ::fixy::Tagged<void*, ::fixy::tags::source::External> base{};
    std::size_t size = 0;
};

}  // namespace neg_sess_payload_opaque_pointer_types

using Delta = ::fixy::session::payload_perm_delta<::neg_sess_payload_opaque_pointer_types::Buffer>;

int main() { return sizeof(typename Delta::sender_requires) == 0 ? 1 : 0; }
