// WriteOnceNonNull::set carries CRUCIBLE_PRE(ptr_ == nullptr).  A second
// set would overwrite the first pointer while its readers still hold it.
// A second set in a constant expression reaches the consteval trap of the
// macro, so the static_assert below has a non-constant condition, and the
// expansion note names WriteOnceNonNull::set.

#include <fixy/Mutation.h>

namespace {

[[nodiscard]] constexpr int under_test() noexcept {
    int first = 1;
    int second = 2;
    fixy::WriteOnceNonNull<int*> slot = fixy::mint_write_once_non_null<int*>();
    slot.set(&first);
    slot.set(&second);
    return 0;
}

static_assert(under_test() == 0);

}  // namespace

int main() { return 0; }
