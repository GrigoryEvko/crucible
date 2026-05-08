// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// SEPLOG-H2 fixture #3 — MpmcChannelSessionSurface concept gate fires
// when a type that has nested ProducerHandle / ConsumerHandle aliases
// but lacks the FULL surface (producer()/consumer() returning
// optional<Handle>, value_type, user_tag/producer_tag/consumer_tag) is
// passed as the Channel template argument to mint_mpmc_producer_session.
//
// The concept structurally requires the full shape; a partial shape
// fails the concept-evaluation requires-clause at substitution time
// rather than punting to a later "no member named X" diagnostic.  This
// is the LOAD-BEARING soundness gate the universal mint pattern
// (CLAUDE.md §XXI) demands a witness for: the requires-clause MUST be
// the first failure point, so users see the concept name in the
// diagnostic and know which contract they violated.

#include <crucible/concurrent/PermissionedMpmcChannel.h>
#include <crucible/permissions/Permission.h>
#include <crucible/sessions/MpmcChannelSession.h>

#include <optional>

namespace ses = ::crucible::safety::proto::mpmc_channel_session;

namespace {
// Synthetic "almost-channel" — has nested handle types and a few of
// the required type aliases, but ch.producer() returns the wrong
// shape (bare Handle, not optional<Handle>) and there is no value_type.
// The concept's requires-expression rejects on the first missing
// member; this fixture pins that the concept gate fires before any
// downstream substitution would happen.
struct AlmostChannel {
    struct ProducerHandle {
        // No try_push / value_type — concept rejects.
    };
    struct ConsumerHandle {
        // No try_pop / value_type — concept rejects.
    };
    // No value_type, user_tag, producer_tag, consumer_tag.
    // No producer() / consumer() factory methods.
};
}

int main() {
    AlmostChannel ch;
    AlmostChannel::ProducerHandle handle;
    // Concept gate fails here — MpmcChannelSessionSurface<AlmostChannel>
    // is not satisfied because AlmostChannel lacks value_type, user_tag,
    // producer_tag, consumer_tag, ch.producer(), ch.consumer(),
    // try_push, try_pop.
    [[maybe_unused]] auto bad = ses::mint_mpmc_producer_session<AlmostChannel>(
        ::crucible::effects::HotFgCtx{}, handle);
    (void)ch;
    return 0;
}
