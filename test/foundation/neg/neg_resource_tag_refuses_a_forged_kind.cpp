// A type that carries the three members of a resource tag is still not
// a tag unless its kind is an axis of the catalog.  A kind cast from an
// unnamed value fails the IsResourceKind half of the concept.

#include <foundation/effects/Resources.h>

#include <cstdint>
#include <string_view>

namespace fe = ::foundation::effects;

struct ForgedResourceTag {
    static constexpr fe::ResourceKind kind = static_cast<fe::ResourceKind>(std::uint8_t{0xFF});
    static constexpr std::uint64_t value = 42;
    static constexpr std::string_view name = "ForgedResourceTag";
};

template <fe::ResourceTag Tag>
void admit_tag() noexcept {}

int main() {
    admit_tag<ForgedResourceTag>();
    return 0;
}
