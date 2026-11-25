// copy_at of a GrowList copies an element with no lend.  A copy constructor
// that a class writes could change the list while the copy reads its slot.  So
// copy_at asks for a trivial copy, and the call binds to the deleted overload.

#include <foundation/core/Grow.h>
#include <foundation/effects/Effect.h>

struct CountsItsCopies {
    int copies = 0;
    CountsItsCopies() noexcept = default;
    CountsItsCopies(CountsItsCopies const& other) noexcept : copies{other.copies + 1} {}
};

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    auto list = ::foundation::core::mint_grow_list<CountsItsCopies>(8);
    list.append(alloc, CountsItsCopies{}).expect("the list is below its maximum");
    return list.copy_at(0).is_some() ? 0 : 1;
}
