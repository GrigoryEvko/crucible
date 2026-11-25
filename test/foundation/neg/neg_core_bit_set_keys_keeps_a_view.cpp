// The walk of the keys of a bit set lends the keys to a body for the call.  A
// body that captures an Option of a View by reference could keep a borrow of
// the walk there, and the caller could read it after the walk.  with_keys
// refuses a body that can store a borrow outside the walk
// (StoresNoBorrowOutside of foundation/core/CaptureStore.h).

#include <foundation/core/Array.h>
#include <foundation/core/BitSet.h>
#include <foundation/core/Choice.h>

int main() {
    ::foundation::core::BitSet<8> const set{};
    ::foundation::core::Option<::foundation::core::View<int const>> kept{::foundation::core::none};
    int const count = set.with_keys([&kept](auto const& /*keys*/) noexcept { return kept.is_some() ? 1 : 0; });
    return count;
}
