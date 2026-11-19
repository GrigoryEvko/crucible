// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Integrity fixture #1: raw CNT-P payload bytes cannot enter an API that
// requires successful end-to-end integrity verification.

#include <crucible/cntp/Integrity.h>
#include <fixy/Core.h>

#include <cstddef>

using Payload = ::fixy::FixedArray<std::byte, 8>;

namespace {

void requires_verified(crucible::cntp::IntegrityVerifiedPayload<Payload> payload) noexcept { (void)payload; }

}  // namespace

int main() {
    Payload payload{};
    requires_verified(payload);
    return 0;
}
