// update_masked of a MaskedArray copies the element out of the heap storage
// and puts the copy back while the array holds the storage.  The move
// assignment of this element is code, and such code could reach the array
// through a second name and free the storage under the slot.  So
// update_masked takes only a trivially copyable element, and this call binds
// to the deleted twin.

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

namespace {

struct AssignedWithCode {
    int value = 0;
    AssignedWithCode() noexcept = default;
    AssignedWithCode(AssignedWithCode const&) noexcept = default;
    AssignedWithCode(AssignedWithCode&&) noexcept = default;
    AssignedWithCode& operator=(AssignedWithCode const&) noexcept = default;
    AssignedWithCode& operator=(AssignedWithCode&& other) noexcept {
        value = other.value + 1;
        return *this;
    }
    ~AssignedWithCode() = default;
};

}  // namespace

int main() {
    ::foundation::core::MaskedArray<AssignedWithCode> array =
        ::foundation::core::mint_masked_array<AssignedWithCode>(::foundation::effects::testing::test().alloc, 4);
    array.update_masked(0, [](AssignedWithCode& element) noexcept { element.value = 1; });
    return 0;
}
