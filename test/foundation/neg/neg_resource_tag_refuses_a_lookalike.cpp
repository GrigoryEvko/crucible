// A type that carries the three members of a resource tag and names a
// real axis is still no tag.  It is no specialization of the tag
// template of its axis, so a row refuses it and it names no budget of
// the catalog.

#include <foundation/effects/Resources.h>

#include <cstdint>
#include <string_view>

namespace fe = ::foundation::effects;

struct LookalikeSmBudget {
    static constexpr fe::ResourceKind kind = fe::ResourceKind::Sm;
    static constexpr std::uint64_t value = 32;
    static constexpr std::string_view name = "SmBudget";
};

template <fe::ResourceTag Tag>
void admit_tag() noexcept {}

int main() {
    admit_tag<LookalikeSmBudget>();
    return 0;
}
