// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-M-17: HS14 floor #1/2 for mint_session_view<Tag>(handle).
//
// `mint_session_view<Tag>(handle)` carries a single requires-clause:
//   requires HandleIsAt<Handle, Tag>
//
// HandleIsAt walks the handle's Proto template parameter and checks
// it matches the requested position Tag.  When the handle is at a
// DIFFERENT protocol position than the requested Tag, the requires-
// clause rejects.
//
// This fixture exercises the HANDLE-side mismatch: the handle is at
// Send<Msg, End> but the caller requests AtRecv.  The handle_is_at
// specialization for SessionHandle<Send<...>, ...> matches only AtSend
// (defined at SessionView.h:151).  AtRecv lookup falls through to the
// primary template that returns false_type, so HandleIsAt is false and
// the requires-clause excludes the factory.
//
// Distinct mismatch class from fixture #2 (invalid Tag): #1 exercises
// HANDLE-side rejection (valid Tag, wrong protocol position); #2
// exercises TAG-side rejection (valid handle position, unrecognized
// Tag).  Both witness mint_session_view's soundness gate at distinct
// layers.
//
// Expected diagnostic:
//   "constraints not satisfied" / "HandleIsAt" / "handle_is_at" /
//   "no matching function" / "mint_session_view"

#include <crucible/sessions/SessionView.h>

namespace neg_mint_session_view_wrong_position {

namespace proto = ::crucible::safety::proto;
namespace safety = ::crucible::safety;

struct FakeRes { int sentinel = 7; };
struct Msg     {};

}  // namespace neg_mint_session_view_wrong_position

int main() {
    using namespace neg_mint_session_view_wrong_position;

    // Handle is at Send<Msg, End> — handle_is_at_v<Send-handle, AtSend>
    // is true, but handle_is_at_v<Send-handle, AtRecv> is FALSE.
    auto h = proto::mint_session_handle<proto::Send<Msg, proto::End>>(
        FakeRes{99});

    // The forbidden call: handle is at Send but caller requests AtRecv.
    // mint_session_view's `requires HandleIsAt<Handle, AtRecv>` excludes
    // this overload from the candidate set.
    [[maybe_unused]] auto view = proto::mint_session_view<proto::AtRecv>(h);
    return 0;
}
