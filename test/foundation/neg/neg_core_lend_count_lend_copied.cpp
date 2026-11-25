// A lend counts one open body.  A copy of it would step the count down two
// times, and a change door would find no open lend in the body of another one.

#include <foundation/core/LendCount.h>

#include <cstdint>

int main() {
    ::foundation::core::LendCount<std::uint32_t> count{};
    auto const lend = count.lend();
    auto const copied = lend;
    static_cast<void>(copied);
    return 0;
}
