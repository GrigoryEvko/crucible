#include <crucible/cntp/CongestionControl.h>

// GAPS-120 fixture #2: DCTCP requires a lossless datacenter fabric.
// Cross-DC and public-Internet flows must not select it by policy.

int main() {
    auto choice =
        crucible::cntp::mint_cc_choice<
            crucible::cntp::CcAlgorithm::Dctcp,
            crucible::cntp::LinkClass::CrossDatacenter>();
    (void)choice;
    return 0;
}
