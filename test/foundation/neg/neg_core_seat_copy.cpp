// A Seat holds one payload.  A copy of the Seat would give the payload a
// second holder, so the copy constructor is deleted.

#include <foundation/core/Seat.h>

#include <cstdint>
#include <utility>

int main() {
    ::foundation::core::Seat<std::uint64_t> seat = ::foundation::core::mint_seat(std::uint64_t{1});
    ::foundation::core::Seat<std::uint64_t> const& held = seat;
    ::foundation::core::Seat<std::uint64_t> copy = held;
    std::uint64_t const sum = std::move(copy).take() + std::move(seat).take();
    return static_cast<int>(sum);
}
