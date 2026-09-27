// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A translation unit says that a hand-off delegates nothing, so that a
// crash session admits it.  The query is a concept over the facts of the
// payload walk, and a concept has no explicit specialization.
//
// Expected diagnostic: the specialization is refused, and it names the
// concept.
#include <fixy/session/Delegate.h>

namespace neg_sess_seal_payload_conveys_delegation_types {
struct Wire {
    [[no_unique_address]] ::fixy::session::MoveOnlyResource one_holder{};
};
using Handed = ::fixy::session::DelegatedSession<::fixy::session::End, Wire, ::fixy::session::DefaultAbandonmentPolicy,
                                                 ::foundation::permissions::EmptyPermSet>;
}  // namespace neg_sess_seal_payload_conveys_delegation_types

namespace fixy::session {
template <>
inline constexpr bool payload_conveys_delegation_v<neg_sess_seal_payload_conveys_delegation_types::Handed> = false;
}  // namespace fixy::session

int main() { return 0; }
