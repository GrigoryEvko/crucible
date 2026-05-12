#include <crucible/cntp/Pacing.h>

// GAPS-121 fixture #2: SO_MAX_PACING_RATE writes require an admitted
// positive pacing rate. Raw integers cannot drive the mutating socket
// path.

int main() {
    auto fd = crucible::cntp::admit_socket_fd(0).value();
    auto result = crucible::cntp::set_socket_pacing_rate(fd, 1'000'000ULL);
    (void)result;
    return 0;
}
