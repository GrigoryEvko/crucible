// WriteOnceNonNull reads a null pointer as the unset state, so a set of
// null would store a value that the slot can never report back.
// WriteOnceNonNull::set carries CRUCIBLE_PRE(p != nullptr).  A set of null
// in a constant expression reaches the consteval trap of the macro, so the
// static_assert below has a non-constant condition, and the expansion note
// names WriteOnceNonNull::set.

#include <fixy/Mutation.h>

namespace {

[[nodiscard]] constexpr int under_test() noexcept {
    fixy::WriteOnceNonNull<int*> slot = fixy::mint_write_once_non_null<int*>();
    slot.set(nullptr);
    return 0;
}

static_assert(under_test() == 0);

}  // namespace

int main() { return 0; }
