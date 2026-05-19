// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-01 negative fixture #2 (HS14 ≥2 floor):
// mint_recording_session second-parameter reference-binding route via
// the `fixy::bridge::` re-export — distinct mismatch class from
// fixture #1 (non_handle / first-param SFINAE).
//
// `mint_recording_session(handle, log, self, peer)` takes
// `SessionEventLog&` as its second parameter.  After the first
// parameter binds successfully (the inbound handle IS a
// SessionHandle), the second parameter's reference type fails to
// bind against a non-SessionEventLog argument.  The diagnostic
// specifically names the SessionEventLog requirement rather than
// the blanket "no overload matches" produced by fixture #1's
// first-parameter rejection.
//
// Why this is a DISTINCT mismatch class from fixture #1:
//   - Fixture #1 (non_handle): first-parameter overload-set rejection.
//     ALL THREE overloads fail substitution simultaneously because
//     the first parameter type pattern-matches no overload.
//     Diagnostic: "no matching function for call to" + all candidates
//     listed.
//   - Fixture #2 (wrong_log_type, this file): first-parameter SELECTS
//     a specific overload (the bare SessionHandle overload binds),
//     THEN the second-parameter reference fails to convert.  Distinct
//     failure mode — narrower, post-overload-resolution diagnostic.
//     Diagnostic: "cannot bind non-const lvalue reference of type
//     'SessionEventLog&' to ..." OR "could not convert".
//
// The fixy:: re-export must reject identically — a regression where
// the using-decl accidentally exposed a function-template forwarder
// with weaker constraints would let the wrong-log-type call compile
// at the fixy:: layer while the substrate-side fixture still
// correctly rejected.  This fixture closes that gap.
//
// Expected diagnostic: "no matching function for call to
// 'mint_recording_session'" OR "cannot bind non-const lvalue
// reference of type 'SessionEventLog&'" OR "could not convert".

#include <crucible/fixy/Bridge.h>
#include <crucible/sessions/SessionEventLog.h>
#include <crucible/sessions/SessionMint.h>

#include <utility>

namespace fbridge = ::crucible::fixy::bridge;
namespace proto   = ::crucible::safety::proto;

struct ProbeResource { int value = 0; };

int main() {
    using P = proto::Send<int, proto::End>;
    ProbeResource res{};

    // Construct a VALID SessionHandle.  The first-param gate passes;
    // the failure is on the second parameter.
    auto bare = proto::mint_session_handle<P>(std::move(res));

    int                not_a_log = 0;     // wrong type — not SessionEventLog&
    proto::RoleTagId   self{1};
    proto::RoleTagId   peer{2};

    // Second argument must be `SessionEventLog&`; passing `int` fails
    // reference binding.  fixy::bridge:: re-export must reject
    // identically — the using-decl preserves the exact parameter
    // shape, not just the requires-clause.
    [[maybe_unused]] auto bad =
        fbridge::mint_recording_session(
            std::move(bare), not_a_log, self, peer);

    return 0;
}
