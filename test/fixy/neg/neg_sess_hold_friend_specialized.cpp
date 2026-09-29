// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A translation unit specializes PermHold for a set of its own, and reads
// the tokens that a real hold keeps from the specialization.  A hold
// befriends only HoldFactory, which is not a template, so a
// specialization gets no access and the tokens stay private.  With access,
// the specialization could take a token that the set of the hold still
// claims.
//
// Expected diagnostic: the slots of the real hold are private in this
// context.
#include <fixy/session/Payload.h>

namespace neg_sess_hold_friend_specialized_types {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
struct ForgeTag {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace neg_sess_hold_friend_specialized_types

namespace fixy::session {
template <>
class PermHold<::foundation::permissions::PermSet<neg_sess_hold_friend_specialized_types::ForgeTag>> {
public:
    static auto
    steal(PermHold<::foundation::permissions::PermSet<neg_sess_hold_friend_specialized_types::Region>>& held) {
        return std::move(std::get<0>(held.slots_));
    }
};
}  // namespace fixy::session

int main() { return 0; }
