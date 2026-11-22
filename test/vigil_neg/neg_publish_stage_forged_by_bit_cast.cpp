// Negative-compile test.  This file must fail to compile.
//
// A proof that bit_cast can produce from a byte is forgeable by anyone.
// The publish-stage proof is not trivially copyable, so bit_cast refuses
// it.
//
// Expected diagnostic: bit_cast needs a trivially copyable type.

#include <crucible/BackgroundThread.h>
#include <foundation/Quarantine.h>

#include <bit>

int main() {
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture forges an object from bytes with std::bit_cast")
    const auto forged = std::bit_cast<crucible::BackgroundThread::PublishStage>(char{0});
    (void)forged;
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    return 0;
}
