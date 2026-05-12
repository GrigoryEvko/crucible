// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-116 fixture #3: IntegrityWrappedMessage stores a refined non-zero
// IntegrityHash.  A raw uint64_t cannot bypass hash admission.

#include <crucible/cntp/Integrity.h>

#include <array>
#include <cstddef>

int main() {
    using Payload = std::array<std::byte, 1>;
    crucible::cntp::IntegrityWrappedMessage<
        crucible::cntp::IntegrityOwnedPayload<Payload>> message{
        .payload = crucible::cntp::IntegrityOwnedPayload<Payload>{
            Payload{std::byte{0x42}}},
        .hash = 1,
    };
    (void)message;
    return 0;
}
