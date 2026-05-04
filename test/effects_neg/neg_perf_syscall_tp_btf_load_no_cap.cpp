// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-004f (#1282): SyscallTpBtf::load() takes a mandatory
// `effects::Init` capability tag.  Same gate as SyscallLatency::load.
//
// Violation: calls load() with no argument.
// Expected diagnostic: "too few arguments|no matching function|
// candidate expects 1 argument".

#include <crucible/perf/SyscallTpBtf.h>

#include <optional>

int main() {
    // <-- this line must NOT compile (missing Init argument)
    std::optional<crucible::perf::SyscallTpBtf> hub =
        crucible::perf::SyscallTpBtf::load();

    (void)hub;
    return 0;
}
