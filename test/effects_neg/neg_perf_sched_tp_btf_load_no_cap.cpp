// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-004f (#1282): SchedTpBtf::load() takes a mandatory
// `effects::Init` capability tag.  Same gate as SchedSwitch::load —
// hot-path code holds no Init token, so the cap-typing prevents
// accidental hot-path SchedTpBtf::load() calls structurally.
//
// Violation: calls load() with no argument.
// Expected diagnostic: "too few arguments|no matching function|
// candidate expects 1 argument".

#include <crucible/perf/SchedTpBtf.h>

#include <optional>

int main() {
    // <-- this line must NOT compile (missing Init argument)
    std::optional<crucible::perf::SchedTpBtf> hub =
        crucible::perf::SchedTpBtf::load();

    (void)hub;
    return 0;
}
