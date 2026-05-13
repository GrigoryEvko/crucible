#include <crucible/cntp/Tcam.h>

int main() {
    namespace tcam = crucible::cntp::tcam;

    tcam::OwnedTcamRule handle{tcam::TcamRuleHandle{}};
    auto copy = handle;
    (void)copy;
    return 0;
}
