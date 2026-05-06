#include <crucible/sessions/SessionSubtype.h>

#include <type_traits>

namespace alg = crucible::algebra::lattices;
namespace proto = crucible::safety::proto;
namespace saf = crucible::safety;

struct Tensor {};

using Provided = saf::NumericalTier<alg::Tolerance::RELAXED, Tensor>;
using Required = saf::NumericalTier<alg::Tolerance::BITEXACT, Tensor>;

namespace crucible::safety::proto {
template <>
struct is_subsort<::Provided, ::Required> : std::true_type {};
}  // namespace crucible::safety::proto

int main() {
    proto::assert_subtype_sync<
        proto::Send<Provided, proto::End>,
        proto::Send<Required, proto::End>>();
}
