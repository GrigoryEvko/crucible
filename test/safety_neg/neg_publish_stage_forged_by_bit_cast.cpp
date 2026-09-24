// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A proof that bit_cast can produce from a byte is forgeable by anyone.
// The publish-stage proof is not trivially copyable, so bit_cast refuses
// it.
//
// Expected diagnostic: bit_cast needs a trivially copyable type.

#include <crucible/BackgroundThread.h>

#include <bit>

int main() {
    const auto forged = std::bit_cast<crucible::BackgroundThread::PublishStage>(char{0});
    (void)forged;
    return 0;
}
