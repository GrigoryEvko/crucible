// A step replaces the payload of a Seat, so a const payload cannot sit in a
// Seat.  The constraint of the mint refuses a const payload type.

#include <foundation/core/Seat.h>

#include <cstdint>

int main() {
    static_cast<void>(::foundation::core::mint_seat<std::uint64_t const>(std::uint64_t{1}));
    return 0;
}
