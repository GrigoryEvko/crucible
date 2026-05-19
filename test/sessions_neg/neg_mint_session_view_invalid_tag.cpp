// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-M-17: HS14 floor #2/2 for mint_session_view<Tag>(handle).
//
// `mint_session_view<Tag>(handle)` carries a single requires-clause:
//   requires HandleIsAt<Handle, Tag>
//
// HandleIsAt is parameterized on BOTH the Handle and the position Tag.
// Recognized Tags are AtSend / AtRecv / AtSelect / AtOffer / AtEnd /
// AtStop / AtTerminal (declared in SessionView.h:126-130 + 134-135).
// When the Tag is a user-defined type not in this taxonomy, the
// handle_is_at primary template returns false_type for ALL handle
// specializations.  HandleIsAt is therefore false and the requires-
// clause excludes the factory.
//
// This fixture exercises the TAG-side mismatch: the handle is at a
// valid position (Send) but the caller passes an unrecognized Tag.
// The compiler walks every handle_is_at specialization, finds none
// match the bogus Tag, falls back to the false_type primary template.
//
// Distinct mismatch class from fixture #1 (wrong position): #1
// exercises HANDLE-side rejection (valid Tag, wrong protocol
// position); #2 exercises TAG-side rejection (valid handle position,
// unrecognized Tag).  Both witness mint_session_view's soundness
// gate at distinct layers.
//
// Expected diagnostic:
//   "constraints not satisfied" / "HandleIsAt" / "handle_is_at" /
//   "no matching function" / "mint_session_view"

#include <crucible/sessions/SessionView.h>

namespace neg_mint_session_view_invalid_tag {

namespace proto = ::crucible::safety::proto;

struct FakeRes { int sentinel = 7; };
struct Msg     {};

// Bogus position tag — not one of AtSend / AtRecv / AtSelect / AtOffer
// / AtEnd / AtStop / AtTerminal.  The handle_is_at primary template
// returns false_type for any unrecognized Tag.
struct BogusPositionTag {};

}  // namespace neg_mint_session_view_invalid_tag

int main() {
    using namespace neg_mint_session_view_invalid_tag;

    // Handle is at Send<Msg, End> — a valid protocol position.  But
    // the requested Tag (BogusPositionTag) is not a member of the
    // SessionView position-tag taxonomy.
    auto h = proto::mint_session_handle<proto::Send<Msg, proto::End>>(
        FakeRes{11});

    // The forbidden call: handle is at a valid position, but the Tag
    // is unrecognized.  HandleIsAt<Handle, BogusPositionTag> resolves
    // through the primary template (false_type), so the requires-
    // clause excludes the factory.
    [[maybe_unused]] auto view =
        proto::mint_session_view<BogusPositionTag>(h);
    return 0;
}
