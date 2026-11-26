// update of a HeapArray copies the element out of the heap storage and puts
// the copy back while the array holds the storage.  The copy constructor of
// this element is code, and such code could reach the array through a second
// name and free the storage under the slot.  So update takes only a trivially
// copyable element, and this call binds to the deleted twin.

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

namespace {

struct CopiedWithCode {
    int value = 0;
    CopiedWithCode() noexcept = default;
    CopiedWithCode(CopiedWithCode const& other) noexcept : value{other.value + 1} {}
    CopiedWithCode(CopiedWithCode&&) noexcept = default;
    CopiedWithCode& operator=(CopiedWithCode const&) noexcept = default;
    CopiedWithCode& operator=(CopiedWithCode&&) noexcept = default;
    ~CopiedWithCode() = default;
};

}  // namespace

int main() {
    ::foundation::core::HeapArray<CopiedWithCode> array =
        ::foundation::core::mint_heap_array(::foundation::effects::testing::test().alloc, 4, CopiedWithCode{});
    array.update(0, [](CopiedWithCode& element) noexcept { element.value = 1; });
    return 0;
}
