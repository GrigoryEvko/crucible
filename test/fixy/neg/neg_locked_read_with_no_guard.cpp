// A class reads the data of a Locked with no guard.  The read would race with
// each holder of the lock that writes the data.  The data is a private
// member of the Locked, and only a body inside the lock gets it.

#include <fixy/os/SpinLock.h>

#include <cstdint>

namespace {

struct CountTag final {};

}  // namespace

int main() {
    ::fixy::spin::BlockingLocked<std::uint64_t, CountTag> locked{};
    return static_cast<int>(locked.data_);
}
