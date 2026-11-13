// The address of FixedList::push with a target type does not choose the brand of
// the Ref.  The brand of the return type is in a context that deduction
// does not read, so the brand parameter keeps its default, the fresh brand.
// A target type with a different brand then matches no function.

#include <foundation/core/List.h>

#include <cstddef>

namespace {

struct Held final {};

}  // namespace

int main() {
    namespace core = ::foundation::core;
    core::FixedList<int, 4> values{};
    using Pick = core::Result<core::Ref<int, Held>, core::Full<int>> (core::FixedList<int, 4>::*)(int) & noexcept;
    auto result = (values.*static_cast<Pick>(&core::FixedList<int, 4>::push))(1);
    static_cast<void>(result);
    return 0;
}
