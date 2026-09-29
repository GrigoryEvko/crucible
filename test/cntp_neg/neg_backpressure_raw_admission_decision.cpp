// An admission decision crosses a runtime boundary only as a declared
// decision, and mint_admission_decision is the one door to that type.  A
// raw decision does not convert.

#include <crucible/cntp/Backpressure.h>

int main() {
    namespace cntp = crucible::cntp;
    cntp::AdmissionDecision const raw{.socket = cntp::admit_socket_fd(3).value()};
    cntp::DeclaredAdmissionDecision declared = raw;
    return static_cast<int>(declared.value().retry_after_ms);
}
