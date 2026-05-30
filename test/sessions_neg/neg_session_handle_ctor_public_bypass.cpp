// SPDX-License-Identifier: Apache-2.0
//
// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture for fix-04 (§XXI Universal Mint Pattern compliance).
//
// Premise: SessionHandle<Proto, Resource, LoopCtx> exists ONLY to be
// the product of mint_session_handle<Proto>(res) (which gates
// well-formedness + Loop unrolling) and the typestate transitions
// (send/recv/select/offer/delegate/accept), all of which route through
// detail::step_to_next and ultimately detail::make_session_handle.
// A production-side `SessionHandle<Send<int, End>, Res, void>{res}`
// direct construction would emit a handle WITHOUT passing
// mint_session_handle's WellFormedRunnableProtocol / SessionResource
// admission nor step_to_next's Continue/Loop head resolution — the
// §XXI bypass fix-04 closes.
//
// Fix shape (fix-04): every SessionHandle specialization's value ctor
// `SessionHandle(Resource, std::source_location)` moved to `private:`,
// and detail::make_session_handle (the sole authorized, unconstrained
// inner factory; mint_session_handle / step_to_next route through it)
// friended.  Any direct construction site is now ill-formed.
//
// Sibling fixture (other-header closure): neg_session_crash_handle_
// ctor_public_bypass.cpp pins the SessionCrash.h Stop_g specialization.
//
// This fixture targets the Session.h Send<T, R> specialization.
// Build MUST fail; diagnostic MUST contain "is private".

#include <crucible/sessions/Session.h>

#include <utility>

namespace proto = crucible::safety::proto;

namespace {

// Plain value type satisfies SessionResource (handle owns it by value).
struct FakeChannel {
    int sentinel = 0;
};

}  // namespace

int main() {
    // ── The §XXI bypass attempt ────────────────────────────────────
    //
    // Direct construction of SessionHandle<Send<int, End>, FakeChannel,
    // void> bypasses mint_session_handle and therefore bypasses its
    // WellFormedRunnableProtocol / SessionResource admission.  With the
    // fix-04 fix, the value ctor is private and this line is ill-formed.
    // Before fix-04 this would have compiled silently (the friend grant
    // was decorative AND signature-mismatched; the public ctor let any
    // call site through).
    proto::SessionHandle<proto::Send<int, proto::End>, FakeChannel, void>
        handle{FakeChannel{.sentinel = 42}};
    (void)handle;

    return 0;
}
