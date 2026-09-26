// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// IntegrityWrappedMessage stores a refined non-zero IntegrityHash.  A raw
// uint64_t cannot bypass hash admission.  The payload goes through its own
// door, so the hash field is the only reason for the refusal.

#include <crucible/cntp/Integrity.h>
#include <fixy/Qtt.h>

#include <array>
#include <cstddef>

int main() {
    using Payload = std::array<std::byte, 1>;
    crucible::cntp::IntegrityWrappedMessage<crucible::cntp::IntegrityOwnedPayload<Payload>> message{
        .payload = ::fixy::mint_linear<Payload>(Payload{std::byte{0x42}}),
        .hash = 1,
    };
    (void)message;
    return 0;
}
