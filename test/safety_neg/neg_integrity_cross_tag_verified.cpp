// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-116 fixture #2: externally-tagged bytes are not integrity-verified
// bytes.  The receiver must unwrap a matching hash first.

#include <crucible/cntp/Integrity.h>

#include <array>
#include <cstddef>
#include <utility>

using Payload = std::array<std::byte, 8>;

void requires_verified(
    crucible::cntp::IntegrityVerifiedPayload<Payload> payload) noexcept {
    (void)payload;
}

int main() {
    crucible::safety::Tagged<
        crucible::cntp::IntegrityOwnedPayload<Payload>,
        crucible::safety::source::External>
        external{crucible::cntp::IntegrityOwnedPayload<Payload>{Payload{}}};
    requires_verified(std::move(external));
    return 0;
}
