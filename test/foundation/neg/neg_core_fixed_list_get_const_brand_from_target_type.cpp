// The address of the const FixedList::get with a target type does not choose the brand of
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
    core::FixedList<int, 4> const values{};
    using Pick = core::Option<core::Ref<int const, Held>> (core::FixedList<int, 4>::*)(std::size_t) const& noexcept;
    auto result = (values.*static_cast<Pick>(&core::FixedList<int, 4>::get))(0);
    static_cast<void>(result);
    return 0;
}
