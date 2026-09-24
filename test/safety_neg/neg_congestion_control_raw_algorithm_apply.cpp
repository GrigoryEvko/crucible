#include <crucible/cntp/CongestionControl.h>

// GAPS-120 fixture #1: raw CcAlgorithm values cannot drive
// TCP_CONGESTION. The mutating socket path requires a tagged
// DeclaredCcChoice produced by a compatibility-checked mint.

int main() {
    namespace cntp = crucible::cntp;

    auto fd = cntp::admit_socket_fd(0).value();
    auto result = cntp::set_cc_for_socket(fd, cntp::CcAlgorithm::Cubic);
    (void)result;
    return 0;
}
