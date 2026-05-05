// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-049: custom delegated protocol shapes must not silently pass
// delegated-recipient crash bonding.  Until a delegated type teaches
// delegated_crash_propagation how to classify it, the consteval
// assertion rejects it at the declaration boundary.

#include <crucible/sessions/SessionDelegate.h>

using namespace crucible::safety::proto;

namespace {

struct Recipient {};

struct CustomDelegatedEndpoint {};

using CarrierK = Offer<Recv<Crash<Recipient>, End>>;

consteval bool probe() {
    assert_delegated_crash_propagates<
        CustomDelegatedEndpoint,
        Recipient,
        CarrierK>();
    return true;
}

static_assert(probe());

}  // namespace
