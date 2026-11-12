// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A translation unit specializes SessionHandleBase and marks the base of
// a live handle consumed from the specialization.  The handle would then
// drop at a Send under check::Enforced and report nothing.  No
// specialization of SessionHandleBase is a friend of another, so the
// member stays protected.
//
// Expected diagnostic: the member is protected in this context.
#include <fixy/session/Handle.h>
#include <foundation/Quarantine.h>

namespace neg_sess_handle_base_sibling_specialized_types {
struct ForgeTag {};
struct Wire {
    int words = 0;
};
namespace s = ::fixy::session;
using Live =
    s::SessionHandle<s::Send<int, s::End>, Wire, void, s::check::Enforced, ::foundation::permissions::EmptyPermSet>;
using LiveBase = s::SessionHandleBase<s::Send<int, s::End>, Live, s::check::Enforced>;
}  // namespace neg_sess_handle_base_sibling_specialized_types

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture forges a SessionHandleBase that marks a live handle consumed")
namespace fixy::session {
template <>
class SessionHandleBase<neg_sess_handle_base_sibling_specialized_types::ForgeTag, void, check::Enforced> {
public:
    static void silently_consume(neg_sess_handle_base_sibling_specialized_types::LiveBase& base) {
        base.mark_consumed_();
    }
};
}  // namespace fixy::session
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
