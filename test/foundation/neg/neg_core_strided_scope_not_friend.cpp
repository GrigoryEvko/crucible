// A class that is not the friend of a branded strided view takes the name of
// the mint in its own namespace, and builds a branded strided view of a brand
// that it names.  The friends that build one are the two forms of
// foundation::core::mint_strided_scope and each branded strided view, so the
// constructor is private here.

#include <foundation/core/Strided.h>

namespace lookalike {

struct ForgedBrand {};

struct StridedScopes {
    [[nodiscard]] static auto mint_strided_scope(foundation::core::StridedView<int, 2> view) noexcept {
        return foundation::core::BrandedStridedView<int, 2, ForgedBrand>{view};
    }
};

}  // namespace lookalike

int main() { return 0; }
