// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A translation unit says that a loan state is a plain tag, so that a
// hold hands out the parked token of a region that it lent.  The
// predicate is a concept over the element, and a concept has no explicit
// specialization.
//
// Expected diagnostic: the specialization is refused, and it names the
// concept.
#include <fixy/session/Payload.h>

namespace neg_sess_seal_plain_tag_types {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace neg_sess_seal_plain_tag_types

namespace fixy::session::detail {
template <>
inline constexpr bool PlainTag<::fixy::session::LentOut<neg_sess_seal_plain_tag_types::Region>> = true;
}  // namespace fixy::session::detail

int main() { return 0; }
