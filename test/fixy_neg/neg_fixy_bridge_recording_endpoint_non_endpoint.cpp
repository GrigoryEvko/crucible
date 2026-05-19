// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-HS14-03 negative fixture #1 (HS14 ≥2 floor):
// mint_recording_endpoint first-parameter SFINAE route via the
// `fixy::bridge::` re-export.
//
// `mint_recording_endpoint(ep, log, self, peer)` wraps an Endpoint's
// bare session view in a RecordingSessionHandle.  The first parameter
// is `Endpoint<Substr, Dir, Ctx>&&` — both a deduced-template-pattern
// gate AND an rvalue-reference binding.  Passing `int` fails template
// argument deduction because no Endpoint<Substr, Dir, Ctx>
// specialization matches `int`.
//
// This fixture routes through `fixy::bridge::mint_recording_endpoint`
// (Bridge.h:153) — the §XXI canonical re-export.  Witnesses that the
// using-decl preserves the template-deduction gate.  A regression
// where the using-decl accidentally exposed a wider function-template
// forwarder accepting non-Endpoint args would slip past the
// substrate-side fixture while THIS fixture still rejected.
//
// Reject sequence: `fbridge::mint_recording_endpoint(int, ...)` →
// template argument deduction attempts to deduce Substr / Dir / Ctx
// from `int&&` → no Endpoint<...> matches → deduction fails →
// "no matching function for call to".
//
// Distinct from fixture #2 (wrong_log_type, the sibling): #1
// exercises the first-parameter template-deduction gate; #2
// exercises the second-parameter reference-binding gate after the
// first parameter binds successfully to a valid Endpoint.  Different
// parameter slots, different failure mechanisms, different
// diagnostic shapes.
//
// Expected diagnostic: "no matching function for call to
// 'mint_recording_endpoint'" / "could not deduce" /
// "no matching template".

#include <crucible/fixy/Bridge.h>
#include <crucible/sessions/SessionEventLog.h>

#include <utility>

namespace fbridge = ::crucible::fixy::bridge;
namespace proto   = ::crucible::safety::proto;

int main() {
    proto::SessionEventLog log{};
    int                    not_an_endpoint = 42;
    proto::RoleTagId       self{1};
    proto::RoleTagId       peer{2};

    // First argument is `int` — fails Endpoint<Substr, Dir, Ctx>
    // template deduction.  fixy::bridge:: re-export must reject
    // identically — the using-decl preserves the template signature.
    [[maybe_unused]] auto bad =
        fbridge::mint_recording_endpoint(
            std::move(not_an_endpoint), log, self, peer);

    return 0;
}
