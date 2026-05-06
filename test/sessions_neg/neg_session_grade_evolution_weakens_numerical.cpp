#include <crucible/sessions/SessionSubtype.h>

#include <type_traits>

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
