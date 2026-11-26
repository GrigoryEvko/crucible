// Negative compile test.  This file must fail to compile.
//
// A class states synchronized_by_proof{} of foundation/core/TypeParts.h, and
// it states no synchronized{} of foundation/Transfer.h.  The class holds only
// plain values, so it has no borrow slot, but a class that states the mark is
// no plain class (foundation/core/TypeParts.h).  So the walk reaches the mark
// and runs its check, which stops the build and names the class.

#include <foundation/core/CaptureStore.h>

namespace neg_core_proof_mark_plain_without_synchronized_types {
namespace core = ::foundation::core;

class[[= core::synchronized_by_proof{}]] Counter {
public:
    [[nodiscard]] int read() const noexcept { return count_; }

private:
    int count_ = 0;
};

struct CapturesCounter {
    Counter& counter;
};
}  // namespace neg_core_proof_mark_plain_without_synchronized_types

static_assert(
    ::foundation::core::StoresNoBorrowOutside<neg_core_proof_mark_plain_without_synchronized_types::CapturesCounter>);

int main() { return 0; }
