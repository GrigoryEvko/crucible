// A class outside the base states owns_its_storage, the mark of an owner whose
// members address only its own storage.  The borrow walk trusts the mark only
// in a class of the base (foundation/core/TypeParts.h), so it reads the
// members of this class, and its View is a borrow.  An arm of an rvalue match
// that gives the class out is refused (HoldsNoBorrow of
// foundation/core/Choice.h).

#include <foundation/core/Choice.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Region.h>

#include <utility>

namespace {
struct[[= ::foundation::core::owns_its_storage{}]] Stash {
    ::foundation::core::View<int const> held{};
};
}  // namespace

int main() {
    ::foundation::core::Option<int> held{::foundation::core::none};
    auto const kept = std::move(held).match([](int) noexcept { return Stash{}; }, []() noexcept { return Stash{}; });
    static_cast<void>(kept);
    return 0;
}
