// The content-addressed marker is a subsort of its payload and the payload
// is a subsort of the marker, at any depth.  These cells check that the
// payload order and the synchronous relation read the axiom in both
// directions, and that duality and well-formedness leave the marker where
// it is.

#include <fixy/session/ContentAddressed.h>
#include <fixy/session/Handle.h>
#include <fixy/session/Protocol.h>
#include <fixy/session/Subtype.h>

#include <cstdio>
#include <type_traits>

namespace {

namespace fs = ::fixy::session;

struct Msg {};
struct Ack {};

template <typename T>
using Ca = fs::ContentAddressed<T>;

using CaCa = Ca<Ca<Msg>>;
using CaCaCa = Ca<Ca<Ca<Msg>>>;
using CaDepth4 = Ca<Ca<Ca<Ca<Msg>>>>;
using CaDepth5 = Ca<Ca<Ca<Ca<Ca<Msg>>>>>;

// The payload order.
static_assert(fs::is_payload_subsort_v<Ca<Msg>, Msg>);
static_assert(fs::is_payload_subsort_v<Msg, Ca<Msg>>);
static_assert(fs::is_payload_subsort_v<Ca<Msg>, Ca<Msg>>);
static_assert(fs::is_payload_subsort_v<Msg, Msg>);
static_assert(!fs::is_payload_subsort_v<Ca<Msg>, Ack>);
static_assert(!fs::is_payload_subsort_v<Msg, Ack>);
static_assert(!fs::is_payload_subsort_v<Ca<Msg>, Ca<Ack>>);

static_assert(fs::is_payload_subsort_v<CaCa, Msg>);
static_assert(fs::is_payload_subsort_v<Msg, CaCa>);
static_assert(fs::is_payload_subsort_v<CaCaCa, Msg>);
static_assert(fs::is_payload_subsort_v<Msg, CaCaCa>);
static_assert(fs::is_payload_subsort_v<CaDepth5, Msg>);
static_assert(fs::is_payload_subsort_v<Msg, CaDepth5>);
static_assert(fs::is_payload_subsort_v<CaCaCa, CaCa>);
static_assert(fs::is_payload_subsort_v<CaCa, CaCaCa>);

// Nesting changes no content, so two depths of the same payload are
// related whatever their distance.
static_assert(fs::is_payload_subsort_v<CaDepth4, CaCa>);
static_assert(fs::is_payload_subsort_v<CaCa, CaDepth4>);

static_assert(!fs::is_payload_subsort_v<CaCa, Ack>);
static_assert(!fs::is_payload_subsort_v<Ack, CaCa>);
static_assert(!fs::is_payload_subsort_v<CaDepth5, Ack>);
static_assert(!fs::is_payload_subsort_v<Ca<Ca<Msg>>, Ca<Ca<Ack>>>);

// The synchronous relation, on a send and on a receive.  A one-way payload
// relation would give only one direction on a receive.  This one is
// two-way, so both directions hold.
static_assert(fs::is_subtype_sync_v<fs::Send<Ca<Msg>, fs::End>, fs::Send<Msg, fs::End>>);
static_assert(fs::is_subtype_sync_v<fs::Send<Msg, fs::End>, fs::Send<Ca<Msg>, fs::End>>);
static_assert(fs::equivalent_sync_v<fs::Send<Ca<Msg>, fs::End>, fs::Send<Msg, fs::End>>);
static_assert(fs::is_subtype_sync_v<fs::Recv<Ca<Msg>, fs::End>, fs::Recv<Msg, fs::End>>);
static_assert(fs::is_subtype_sync_v<fs::Recv<Msg, fs::End>, fs::Recv<Ca<Msg>, fs::End>>);
static_assert(fs::equivalent_sync_v<fs::Recv<Ca<Msg>, fs::End>, fs::Recv<Msg, fs::End>>);
static_assert(fs::equivalent_sync_v<fs::Send<CaCa, fs::End>, fs::Send<Msg, fs::End>>);
static_assert(fs::equivalent_sync_v<fs::Recv<CaCa, fs::End>, fs::Recv<Msg, fs::End>>);

using CaLoopSend = fs::Loop<fs::Send<Ca<Msg>, fs::Continue>>;
using RawLoopSend = fs::Loop<fs::Send<Msg, fs::Continue>>;
static_assert(fs::equivalent_sync_v<CaLoopSend, RawLoopSend>);

using SelectMixed = fs::Select<fs::Send<Ca<Msg>, fs::End>, fs::Send<Ack, fs::End>>;
using SelectRaw = fs::Select<fs::Send<Msg, fs::End>, fs::Send<Ack, fs::End>>;
static_assert(fs::is_subtype_sync_v<SelectMixed, SelectRaw>);
static_assert(fs::is_subtype_sync_v<SelectRaw, SelectMixed>);

// The marker is a payload, not a combinator, so duality leaves it where
// it is.  Both peers see the payload marked, or neither does.
using CaProto = fs::Send<Ca<Msg>, fs::End>;
using CaProtoDual = fs::dual_of_t<CaProto>;
static_assert(std::is_same_v<CaProtoDual, fs::Recv<Ca<Msg>, fs::End>>);
static_assert(std::is_same_v<fs::dual_of_t<CaProtoDual>, CaProto>);

static_assert(fs::is_well_formed_v<fs::Send<Ca<Msg>, fs::End>>);
static_assert(fs::is_well_formed_v<fs::Loop<fs::Send<Ca<Msg>, fs::Continue>>>);
static_assert(fs::is_well_formed_v<fs::Recv<Ca<Ack>, fs::End>>);

// The payload walk reads through the marker to the value it carries.
static_assert(std::is_same_v<fs::protocol_payload_row_t<fs::Send<Ca<Msg>, fs::End>>,
                             fs::protocol_payload_row_t<fs::Send<Msg, fs::End>>>);

}  // namespace

int main() {
    std::puts("test_session_content_addressed: all static checks hold");
    return 0;
}
