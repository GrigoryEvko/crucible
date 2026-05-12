// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-116 fixture #1: raw CNT-P payload bytes cannot enter an API that
// requires successful end-to-end integrity verification.

#include <crucible/cntp/Integrity.h>

#include <array>
#include <cstddef>

using Payload = std::array<std::byte, 8>;

void requires_verified(
    crucible::cntp::IntegrityVerifiedPayload<Payload> payload) noexcept {
    (void)payload;
}

int main() {
    Payload payload{};
    requires_verified(payload);
    return 0;
}
