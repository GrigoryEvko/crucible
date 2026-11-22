// A unit that is no owner on the core, such as a door of another group of the
// base or a file of src/, asks OwnerCore for the key of a View from raw parts
// and makes a View of any pointer.  run_key_() is a protected member of the
// core, so only a class that derives from OwnerCore reaches it.

#include <foundation/core/Region.h>

namespace {

struct LoneState {
    using owner_state_mark = ::foundation::core::OwnerStateMark;
    std::size_t count = 0;
    [[nodiscard]] static constexpr LoneState emptied(LoneState const& /*state*/) noexcept { return {}; }
    static constexpr void release(LoneState const& /*state*/) noexcept {}
};

}  // namespace

int main() {
    using Core = ::foundation::core::OwnerCore<LoneState>;
    ::foundation::core::View<int> const forged{Core::run_key_(), static_cast<int*>(nullptr), std::size_t{4}};
    return static_cast<int>(forged.size());
}
