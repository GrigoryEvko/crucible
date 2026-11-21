// A step is one turn of a loop over one payload type.  A step whose function
// gives another type would change the type of the payload in the storage of
// the Seat, so the constraint of step refuses it.

#include <foundation/core/Seat.h>

#include <cstdint>
#include <utility>

int main() {
    ::foundation::core::Seat<std::uint64_t> seat = ::foundation::core::mint_seat(std::uint64_t{1});
    seat.step([](std::uint64_t&& value) noexcept { return static_cast<std::uint32_t>(value); });
    return static_cast<int>(std::move(seat).take());
}
