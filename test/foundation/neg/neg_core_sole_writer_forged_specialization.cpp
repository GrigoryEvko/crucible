// An explicit specialization of mint_sole_writer for a second role, with the
// brand of a head writer that the program already holds.  The declared type
// would set the brand by deduction, and the body, a friend of the key, would
// build a second head writer of that brand: a second thread could then do
// the relaxed write of the head cell.  The result type holds the brand in
// undeducible_brand_t, so the declared type sets no brand, and the
// specialization matches no template declaration.

#include <foundation/core/Atomic.h>

namespace {
struct HeadRole {};
struct OtherRole {};
}  // namespace

using HeadWriter = decltype(foundation::core::mint_sole_writer<HeadRole>());
using HeldBrand = HeadWriter::brand_type;

template <>
constexpr foundation::core::SoleWriter<OtherRole, HeldBrand> foundation::core::mint_sole_writer<OtherRole>() noexcept {
    auto second_head = foundation::core::SoleWriter<HeadRole, HeldBrand>{foundation::core::sole_writer_key{}};
    ::foundation::linear::drop(static_cast<HeadWriter&&>(second_head));
    return foundation::core::SoleWriter<OtherRole, HeldBrand>{foundation::core::sole_writer_key{}};
}

int main() { return 0; }
