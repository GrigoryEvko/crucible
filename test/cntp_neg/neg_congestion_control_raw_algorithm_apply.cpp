#include <crucible/cntp/CongestionControl.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

// A raw CcAlgorithm value cannot drive TCP_CONGESTION.  The socket call
// takes a tagged DeclaredCcChoice from a mint that checks the link class.
// The context fits the socket option gate, so the choice is the one thing
// the call refuses.

int main() {
    namespace cntp = crucible::cntp;

    ::fixy::InitLoadCtx load{::foundation::effects::testing::init()};
    auto fd = cntp::admit_socket_fd(0).value();
    auto result = cntp::set_cc_for_socket(load, fd, cntp::CcAlgorithm::Cubic);
    (void)result;
    return 0;
}
