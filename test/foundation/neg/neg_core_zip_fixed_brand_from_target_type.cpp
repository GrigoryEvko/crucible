// The address of zip of two Views of one fixed extent, with a target type,
// does not choose the brand of the Refs.  The brand of the return type is
// in a context that deduction does not read, so the brand parameter keeps
// its default, the fresh brand.  A target type with a different brand then
// matches no function.

#include <foundation/core/Array.h>

#include <cstddef>

namespace {

struct Held final {};

}  // namespace

int main() {
    namespace core = ::foundation::core;
    core::FixedArray<int, 4> values{1, 2, 3, 4};
    using Pick = core::detail::ZipRange<int, int, Held> (*)(core::View<int, 4>, core::View<int, 4>) noexcept;
    return static_cast<int>(static_cast<Pick>(&core::zip)(values.view(), values.view()).size());
}
