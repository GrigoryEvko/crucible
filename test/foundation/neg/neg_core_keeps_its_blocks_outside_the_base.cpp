// A class outside the base states keeps_its_blocks{}, and a member of it
// replaces its storage.  The rule view-escape trusts the mark only on a class of
// the base, so it reads the call of the member as an end of the storage, and it
// refuses the read of the View after the call.

#include <foundation/core/Array.h>
#include <foundation/core/Owner.h>
#include <foundation/core/Region.h>

namespace {

class[[= ::foundation::core::keeps_its_blocks{}]] Renewed final {
public:
    [[nodiscard]] ::foundation::core::View<int const> view() const& noexcept { return items_.view(); }
    void renew() noexcept { items_ = ::foundation::core::HeapArray<int>{}; }

private:
    ::foundation::core::HeapArray<int> items_{};
};

}  // namespace

int main() {
    Renewed renewed;
    auto const view = renewed.view();
    renewed.renew();
    return static_cast<int>(view.size());
}
