// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// SEPLOG-H2 fixture #4 — copying a ProducerHandle is rejected by the
// underlying PermissionedMpmcChannel's = delete with reason string.
// This test pins the load-bearing pool-refcount discipline at the
// SESSION layer (vs neg_mpmc_consumer_copy which pins it at the
// channel layer): when a session-typed call site refactors handle
// passing (e.g. accidentally captures by-value into a closure), the
// copy attempt is caught at the channel boundary before any session
// machinery is constructed.
//
// This is the structural complement to fixture #3's concept gate —
// fixture #3 catches "wrong type"; fixture #4 catches "right type,
// wrong ownership discipline."  Together with fixtures #1 and #2
// they cover the four canonical mismatch classes for the typed-
// session MPMC facade.

#include <crucible/concurrent/PermissionedMpmcChannel.h>
#include <crucible/sessions/MpmcChannelSession.h>

#include <utility>

namespace conc = ::crucible::concurrent;

namespace {
struct Tag {};
using Channel = conc::PermissionedMpmcChannel<int, 64, Tag>;
}

int main() {
    Channel ch;
    auto p_opt = ch.producer();
    if (!p_opt) return 1;
    auto prod_handle = std::move(*p_opt);
    // Copy attempt — deleted with reason string at channel layer.
    auto prod_handle_copy = prod_handle;
    (void)prod_handle_copy;
    return 0;
}
