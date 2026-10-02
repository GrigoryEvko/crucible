// The bytes of a union do not tell its active member, so decode cannot
// check the member.  decode refuses a union.

#include <foundation/core/Scalar.h>

#include <cstdint>

enum class Phase : std::uint8_t {
    idle,
    busy
};

union PhaseOrByte {
    Phase phase;
    std::uint8_t byte;
};

int main() { return ::foundation::core::decode<PhaseOrByte>(std::uint8_t{7}).is_some() ? 0 : 1; }
