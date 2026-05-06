#include <crucible/sessions/SessionSubtype.h>

#include <type_traits>

// GAPS-070 evolution fixture: protocol evolution means an old local session
// type is being replaced by a new one. Even if a local is_subsort axiom claims
// the payload transition is acceptable, the ProductLattice grade filter must
// continue to fire when the new protocol weakens the numerical determinism axis.

namespace alg = crucible::algebra::lattices;
namespace proto = crucible::safety::proto;
namespace saf = crucible::safety;

struct Tensor {};

using NewPayload = saf::NumericalTier<alg::Tolerance::RELAXED, Tensor>;
using OldPayload = saf::NumericalTier<alg::Tolerance::BITEXACT, Tensor>;

using OldProto = proto::Send<OldPayload, proto::End>;
using NewProto = proto::Send<NewPayload, proto::End>;

namespace crucible::safety::proto {
template <>
struct is_subsort<::NewPayload, ::OldPayload> : std::true_type {};
}  // namespace crucible::safety::proto

int main() {
    proto::check_protocol_evolution<OldProto, NewProto>();
}
