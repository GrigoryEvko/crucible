// The one open of a /dev/ptpN node is a private member of PtpDeviceDoor,
// and its one friend is fixy::time::mint_ptp_clock_reader.  A direct call
// to the door, which names no context, is refused.

#include <fixy/os/Time.h>

#include <cstdint>

int main() {
    std::uint16_t device = 0;
    auto const index = fixy::mint_refined<fixy::bounded_above<std::uint16_t{255}>>(device);
    [[maybe_unused]] auto opened = fixy::time::PtpDeviceDoor::open_(index);
    return 0;
}
