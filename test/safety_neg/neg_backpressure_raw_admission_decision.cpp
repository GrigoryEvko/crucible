#include <crucible/cntp/Backpressure.h>

// GAPS-137 fixture #1: admission decisions crossing runtime boundaries
// must be tagged with source::AdmissionDecision. Raw decisions cannot
// substitute for the declared provenance lane.

int main() {
    namespace cntp = crucible::cntp;
    cntp::AdmissionDecision raw{};
    cntp::DeclaredAdmissionDecision declared = raw;
    (void)declared;
    return 0;
}
