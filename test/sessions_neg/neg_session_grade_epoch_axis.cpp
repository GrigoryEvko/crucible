#include <crucible/sessions/SessionSubtype.h>

#include <type_traits>

namespace proto = crucible::safety::proto;
namespace saf = crucible::safety;

struct View {};

using Provided = View;
using Required = saf::EpochVersioned<View>;

namespace crucible::safety::proto {
template <>
struct is_subsort<::Provided, ::Required> : std::true_type {};
}  // namespace crucible::safety::proto

int main() {
    proto::assert_subtype_sync<
        proto::Send<Provided, proto::End>,
        proto::Send<Required, proto::End>>();
}
