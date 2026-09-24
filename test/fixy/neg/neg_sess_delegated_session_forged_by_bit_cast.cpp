// A DelegatedSession built from bytes.  The type holds a live endpoint and
// is not trivially copyable, so std::bit_cast refuses it.

#include <fixy/session/Delegate.h>

#include <array>
#include <bit>

namespace neg_sess_delegated_session_forged_by_bit_cast_types {

namespace s = ::fixy::session;
namespace fp = ::foundation::permissions;

struct Wire {};
using Carried = s::DelegatedSession<s::End, Wire, s::DefaultAbandonmentPolicy, fp::EmptyPermSet>;

}  // namespace neg_sess_delegated_session_forged_by_bit_cast_types

int main() {
    namespace t = neg_sess_delegated_session_forged_by_bit_cast_types;
    const std::array<unsigned char, sizeof(t::Carried)> bytes{};
    auto forged = std::bit_cast<t::Carried>(bytes);
    return forged.holds_endpoint() ? 0 : 1;
}
