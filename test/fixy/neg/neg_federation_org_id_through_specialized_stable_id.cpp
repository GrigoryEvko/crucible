// The handshake of a federation sends the org id of each peer, and the org
// id is the stable id of the class that names the organization.  A unit
// that could specialize the stable id would let one organization claim the
// id of another.  The stable id is a function that is not a template, so
// the specialization does not compile.

#include <fixy/Federation.h>

#include <cstdint>

namespace forged_org {
struct Intruder {};
}  // namespace forged_org

template <>
inline constexpr std::uint64_t foundation::reflect::stable_type_id<forged_org::Intruder> = 1;

[[maybe_unused]] inline constexpr fixy::federation::OrgId read_id =
    fixy::federation::federation_org_id<forged_org::Intruder>;

int main() { return 0; }
