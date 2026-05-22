#include <crucible/fixy/Cap.h>

// FIXY-V-212 fixture #1: the fixy::cap::cntp::mint_cc_choice re-export
// MUST preserve the substrate's CcCompatible<Algorithm, Link> concept gate.
// DCTCP is lossless-fabric only — selecting it for CrossDatacenter
// through the fixy:: surface must fail compilation with the same
// requires-clause as the bare substrate call.

int main() {
    auto choice =
        crucible::fixy::cap::cntp::mint_cc_choice<
            crucible::cntp::CcAlgorithm::Dctcp,
            crucible::cntp::LinkClass::CrossDatacenter>();
    (void)choice;
    return 0;
}
