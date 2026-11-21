// A step of a temporary Seat builds the next payload in a Seat that the full
// expression ends, so the next payload is lost.  The step of an rvalue Seat is
// deleted.

#include <foundation/core/Seat.h>

#include <cstdint>
#include <utility>

int main() {
    ::foundation::core::Seat<std::uint64_t> seat = ::foundation::core::mint_seat(std::uint64_t{1});
    std::move(seat).step([](std::uint64_t&& value) noexcept { return value + 1; });
    return static_cast<int>(std::move(seat).take());
}
